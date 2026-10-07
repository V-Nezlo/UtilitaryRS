/*!
\file
\brief Хаб устройств
\author V-Nezlo (vlladimirka@gmail.com)
\date 23.09.2025
\version 2.0
*/

#ifndef LIB_DEVICEHUB_HPP_
#define LIB_DEVICEHUB_HPP_

#include "RsHandler.hpp"
#include "RsTypes.hpp"
#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <queue>
#include <vector>

namespace RS {

/// \brief События DeviceHub с постоянным UID ноды и текущим адресом deviceId (1...32)
class DeviceHubObserver {
public:
	virtual void onAckNotReceivedEv(const NodeUid &aUID, uint8_t aDeviceId, MessageType aMessage) = 0;
	virtual void onAckReceivedEv(const NodeUid &aUID, uint8_t aDeviceId, MessageType aMessage, Result aCode) = 0;

	virtual void onCommandResultEv(const NodeUid &aUID, uint8_t aDeviceId, Result aReturn) = 0;
	virtual void onRequestErrorEv(const NodeUid &aUID, uint8_t aDeviceId, Result aReturn) = 0;

	virtual Result blobAnswerEvReceived(const NodeUid &aUID, uint8_t aDeviceId, uint8_t Request, const void *aData, size_t aSize) = 0;

	virtual void deviceRegisteredEv(const NodeUid &aUID, uint8_t aDeviceId, DeviceVersion aVersion) = 0;
	virtual void deviceLostEv(const NodeUid &aUID, uint8_t aDeviceId) = 0;

	virtual Result fileWriteResultEv(const NodeUid &aUID, uint8_t aDeviceId, Result aReturn) = 0;
	virtual void deviceHealthReceivedEv(const NodeUid &aUID, uint8_t aDeviceId, Health aHealth, uint16_t aFlags) = 0;

};

template<class Interface, typename Time, typename Crc8, typename CrcFile, size_t ParserSize>
class DeviceHub : public RsHandler<Interface, Crc8, ParserSize> {
public:
	enum class State { Starting, Waiting, Scanning, Allocating, Running };

private:
	using Base = RsHandler<Interface, Crc8, ParserSize>;

	static constexpr size_t kTimeoutErrorForLost{20};
	static constexpr auto kHealthTimeout{std::chrono::milliseconds{1000}};
	static constexpr auto kResponseTimeout{std::chrono::milliseconds{200}};
	static constexpr auto kAllocationProbeInterval{std::chrono::milliseconds{10000}};
	static constexpr uint8_t kAllocationAttempts{3};

	struct PendingTrans {
		uint8_t messageNumber; // Номер сообщения, который был отправлен
		MessageType msgType; // Тип сообщения которое отправили
		std::chrono::milliseconds timestamp; // время отправления
	};

	struct AllocationContext {
		enum class State { Discovering, Registering } state{State::Discovering};
		uint32_t roundNonce{0};
		uint8_t roundsLeft{0};
		uint8_t bucket{0};
		std::vector<NodeUid> discovered;
		std::optional<PendingTrans> pending;
	};

	enum class DeviceState : uint8_t { Assigning, Probing, InfoRequest, Running, FileTransfer, Suspended, Lost };

	struct TelemetryUnit {
		uint8_t req;
		uint8_t reqSize;
		std::chrono::milliseconds updateTime;
		std::chrono::milliseconds lastUpdateTime;
	};

	struct FileTransferContext {
		uint8_t file{0};
		const void *data{nullptr};
		size_t totalSize{0};
		size_t sentOffset{0};
		size_t chunkSent{0};
		size_t chunkSize{0};
		std::optional<Result> packetAck;
		bool firstPacket{true};

		enum class State { Request, Sending, Finalize, Cancel } state;
	};

	struct DeviceWrapper {
		NodeUid uid{};
		DeviceVersion version;
		DeviceState state{DeviceState::InfoRequest};
		std::optional<PendingTrans> pending;

		std::chrono::milliseconds nextCall{std::chrono::milliseconds{0}};
		std::chrono::milliseconds lastAck{std::chrono::milliseconds{0}};
		std::chrono::milliseconds lastHealthReq{std::chrono::milliseconds{0}};

		std::queue<std::pair<uint8_t, uint8_t>> commandQueue;
		std::queue<std::pair<std::uint8_t, uint8_t>> requestQueue;

		std::vector<TelemetryUnit> telemSched;
		size_t timeoutCounter{0};
		uint8_t allocationAttempts{0};

		FileTransferContext fileTransContext;
	};

public:

	/// \brief Конструктор хаба
	/// \param aHubVersion версия устройства хаба
	/// \param aUID постоянный UID мастера
	/// \param aIface интерфейс связи
	/// \param aName имя хаба, по умолчанию Master
	DeviceHub(const DeviceVersion &aHubVersion, const NodeUid &aUID, Interface &aIface, const char *aName = "Master") :
		Base(aName, aHubVersion, aUID, aIface, 0),
		hub{},
		observer{nullptr},
		scanUid{1},
		scanPending{std::nullopt},
		nextAllocationProbe{Time::milliseconds() + kAllocationProbeInterval}
	{ }

	/// \brief Зарегистрировать наблюдателя
	/// \param aObserver
	void registerObserver(DeviceHubObserver *aObserver)
	{
		observer = aObserver;
	}

	/// \brief Начать последовательный опрос аллоцированных нод с адресами 1...32
	bool probeAll()
	{
		if ((hubState != State::Starting && hubState != State::Running) || hasFileTransfer()) {
			return false;
		}
		beginAllocation(State::Scanning, 0);
		return true;
	}

	/// \brief Начать аллокацию после сканирования занятых адресов
	/// \param aRounds число полных проходов хеш-групп с разными nonce
	bool allocateNodes(uint8_t aRounds = 4)
	{
		if (aRounds == 0 || (hubState != State::Starting && hubState != State::Running) || hasFileTransfer()) {
			return false;
		}
		beginAllocation(State::Scanning, aRounds);
		return true;
	}

	State state() const
	{
		return hubState;
	}

	/// \brief Получить все резервирования, включая назначения без подтверждения
	const std::array<std::optional<NodeAllocation>, kMaxNodeCount> &getNodeAllocations() const
	{
		return nodeAllocations;
	}

	/// \brief Базовая функция, вызывать в планировщике
	/// \param aTime текущее время
	void process(std::chrono::milliseconds aTime)
	{
		if (hubState == State::Starting) {
			beginAllocation(State::Scanning, 1);
		}
		if (hubState == State::Running) {
			processPending(aTime);
			if (aTime >= nextAllocationProbe && !hasFileTransfer()) {
				beginAllocation(State::Allocating, 1);
			} else {
				processDevices(aTime);
				return;
			}
		}
		if (hubState == State::Waiting) {
			// Закончим предыдущий обмен, не запуская обычную работу хаба
			processPending(aTime);
			for (const auto &pos : hub) {
				if (pos.second.pending) {
					return;
				}
			}
			hubState = nextState;
		}

		switch (hubState) {
			case State::Scanning:
				processScan(aTime);
				break;
			case State::Allocating:
				processAllocation(aTime);
				break;
			default:
				break;
		}
	}

	/// \brief Отправить команду на устройство - обработка через очередь
	/// \param aDeviceId текущий адрес устройства на шине (1...32)
	/// \param aCommand команда
	/// \param aValue аргумент
	/// \return true если успех
	bool sendCmdToDevice(uint8_t aDeviceId, uint8_t aCommand, uint8_t aValue)
	{
		DeviceWrapper *device = getDevice(aDeviceId);
		if (device == nullptr) {
			return false;
		}

		DeviceWrapper &dev = *device;

		if (dev.state != DeviceState::Running) {
			return false;
		}

		dev.commandQueue.push(std::make_pair(aCommand, aValue));
		return true;
	}

	/// \brief Отправить разовый реквест на устройство, очередь
	/// \param aDeviceId текущий адрес устройства на шине (1...32)
	/// \param aBlobRequest номер запроса
	/// \return true если успех
	bool sendBlobRequestToDevice(uint8_t aDeviceId, uint8_t aBlobRequest, uint8_t aBlobSize)
	{
		DeviceWrapper *device = getDevice(aDeviceId);
		if (device == nullptr) {
			return false;
		}

		DeviceWrapper &dev = *device;

		if (dev.state != DeviceState::Running) {
			return false;
		}

		dev.requestQueue.push(std::make_pair(aBlobRequest, aBlobSize));
		return true;
	}

	/// \brief Создать запрос по расписанию для устройства
	/// \param aDeviceId текущий адрес устройства на шине (1...32)
	/// \param aReq запрос
	/// \param aReqSize длина запроса
	/// \param aTimeout период опроса
	/// \return true если успех
	bool createSchedRequest(
		uint8_t aDeviceId, uint8_t aReq, uint8_t aReqSize, std::chrono::milliseconds aTimeout)
	{
		DeviceWrapper *device = getDevice(aDeviceId);
		if (device == nullptr) {
			return false;
		}

		DeviceWrapper &dev = *device;

		TelemetryUnit entry{aReq, aReqSize, aTimeout, std::chrono::milliseconds{0}};
		dev.telemSched.push_back(entry);
		return true;
	}

	/// \brief Отправить файл
	/// \param aDeviceId текущий адрес устройства на шине (1...32)
	/// \param aFile номер файла
	/// \param aData данные
	/// \param aSize длина данных
	/// \param aChunkSize размер чанка
	/// \return true если команда принята
	bool sendFile(uint8_t aDeviceId, uint8_t aFile, const void *aData, size_t aSize, size_t aChunkSize)
	{
		if (hubState != State::Running) {
			return false;
		}
		DeviceWrapper *device = getDevice(aDeviceId);
		if (device == nullptr) {
			return false;
		}

		DeviceWrapper &dev = *device;
		if (dev.state != DeviceState::Running) {
			return false;
		}

		dev.state = DeviceState::FileTransfer;
		dev.fileTransContext.state = FileTransferContext::State::Request;
		dev.fileTransContext.chunkSize = aChunkSize;
		dev.fileTransContext.data = aData;
		dev.fileTransContext.totalSize = aSize;
		dev.fileTransContext.sentOffset = 0;
		dev.fileTransContext.file = aFile;
		dev.fileTransContext.firstPacket = true;

		return true;
	}

private:
	std::map<uint8_t, DeviceWrapper> hub;
	DeviceHubObserver *observer;
	uint8_t scanUid;
	std::optional<PendingTrans> scanPending;
	AllocationContext allocation;
	std::array<std::optional<NodeAllocation>, kMaxNodeCount> nodeAllocations{};
	uint32_t allocationNonce{0};
	uint32_t allocationTransaction{0};
	State hubState{State::Starting};
	State nextState{State::Scanning};
	uint8_t lastDevice{0};
	bool resetNodesDuringScan{true};
	std::chrono::milliseconds nextAllocationProbe;

	bool hasFileTransfer() const
	{
		for (const auto &pos : hub) {
			if (pos.second.state == DeviceState::FileTransfer) {
				return true;
			}
		}
		return false;
	}

	void beginAllocation(State aFirstState, uint8_t aRounds)
	{
		allocation = AllocationContext{};
		allocation.roundsLeft = aRounds;
		allocation.roundNonce = ++allocationNonce;
		scanUid = 1;
		nextState = aFirstState;
		hubState = State::Waiting;
		nextAllocationProbe = Time::milliseconds() + kAllocationProbeInterval;
	}

	/// \brief Проверить только завершение уже отправленных запросов
	void processPending(std::chrono::milliseconds aTime)
	{
		for (auto &pos : hub) {
			DeviceWrapper &dev = pos.second;
			if (!dev.pending || aTime - dev.pending.value().timestamp < kResponseTimeout) {
				continue;
			}
			if (hubState == State::Allocating &&
				(dev.state == DeviceState::Assigning || dev.state == DeviceState::InfoRequest)) {
				dev.pending.reset();
				if (dev.allocationAttempts >= kAllocationAttempts) {
					dev.state = DeviceState::Suspended;
				}
				continue;
			}
			if (++dev.timeoutCounter >= kTimeoutErrorForLost) {
				dev.timeoutCounter = 0;
				dev.state = DeviceState::Lost;
			}
			if (observer) {
				observer->onAckNotReceivedEv(dev.uid, pos.first, dev.pending.value().msgType);
			}
			if (dev.state == DeviceState::FileTransfer) {
				dev.fileTransContext.state = FileTransferContext::State::Cancel;
			}
			dev.pending.reset();
		}
	}

	/// \brief Продолжить обнаружение после обработки ответов одной группы
	void advanceAllocationBucket()
	{
		allocation.pending.reset();
		allocation.discovered.clear();
		if (++allocation.bucket == kAllocationBucketCount) {
			allocation.bucket = 0;
			if (--allocation.roundsLeft == 0) {
				hubState = State::Running;
				return;
			}
			allocation.roundNonce = ++allocationNonce;
		}
		allocation.state = AllocationContext::State::Discovering;
	}

	uint8_t reserveNodeAddress(const NodeUid &aUID)
	{
		for (const auto &entry : nodeAllocations) {
			if (entry && entry.value().uid == aUID) {
				return entry.value().nodeId;
			}
		}
		for (uint8_t uid = 1; uid <= kMaxNodeCount; ++uid) {
			if (!nodeAllocations[uid - 1] && hub.find(uid) == hub.end()) {
				// Адрес остается зарезервированным даже при потере всех подтверждений
				nodeAllocations[uid - 1] = NodeAllocation{aUID, ++allocationTransaction, uid};
				return uid;
			}
		}
		return kReservedUID;
	}

	/// \brief Обнаружение нод и обслуживание их автоматов регистрации
	void processAllocation(std::chrono::milliseconds aTime)
	{
		if (allocation.state == AllocationContext::State::Discovering) {
			if (!allocation.pending) {
				const uint8_t number = Base::sendDiscoverRequest(allocation.roundNonce, allocation.bucket);
				allocation.pending = PendingTrans{number, MessageType::DiscoverReq, aTime};
				return;
			}
			if (aTime - allocation.pending.value().timestamp < kResponseTimeout) {
				return;
			}
			allocation.pending.reset();
			// Ответы всей группы собраны; дальнейший обмен хранится в каждой ноде
			for (const NodeUid &uid : allocation.discovered) {
				const uint8_t address = reserveNodeAddress(uid);
				if (address == kReservedUID) {
					continue;
				}
				DeviceWrapper &dev = hub[address];
				dev.uid = uid;
				dev.state = DeviceState::Assigning;
				dev.allocationAttempts = 0;
				dev.nextCall = aTime;
			}
			allocation.state = AllocationContext::State::Registering;
		}

		processPending(aTime);
		processDevices(aTime);
		for (const auto &pos : hub) {
			if (pos.second.state == DeviceState::Assigning || pos.second.state == DeviceState::InfoRequest) {
				return;
			}
		}
		advanceAllocationBucket();
		if (hubState == State::Allocating) {
			const uint8_t number = Base::sendDiscoverRequest(allocation.roundNonce, allocation.bucket);
			allocation.pending = PendingTrans{number, MessageType::DiscoverReq, aTime};
		}
	}

	/// \brief Последовательно обслуживать устройства без блокирующего ожидания ответа
	void processDevices(std::chrono::milliseconds aTime)
	{
		for (const auto &pos : hub) {
			if (pos.second.pending) {
				return; // На общей шине одновременно ожидаем ответ на один запрос
			}
		}
		auto pos = hub.upper_bound(lastDevice);
		for (size_t count = 0; count < hub.size(); ++count) {
			if (pos == hub.end()) {
				pos = hub.begin();
			}
			lastDevice = pos->first;
			DeviceWrapper &dev = pos->second;
			if (hubState == State::Running || dev.state == DeviceState::Assigning || dev.state == DeviceState::InfoRequest) {
				processDevice(pos->first, dev, aTime);
				if (dev.pending) {
					return;
				}
			}
			++pos;
		}
	}

	/// \brief Опросить следующий адрес после ответа или таймаута предыдущего
	void processScan(std::chrono::milliseconds aTime)
	{
		if (scanPending) {
			if (aTime - scanPending.value().timestamp < kResponseTimeout) {
				return;
			}
			nextScanAddress();
			if (hubState != State::Scanning) {
				return;
			}
		}

		scanPending = PendingTrans{Base::sendDeviceInfoRequest(scanUid, resetNodesDuringScan), MessageType::DeviceInfoReq, aTime};
	}

	/// \brief Перейти к следующему адресу после ответа или таймаута
	void nextScanAddress()
	{
		scanPending.reset();
		if (++scanUid > kMaxNodeCount) {
			resetNodesDuringScan = false;
			hubState = allocation.roundsLeft ? State::Allocating : State::Running;
		}
	}

protected:
	// RsHandler interface
	void handleDiscoverAnswer(uint8_t aMessageNumber, uint32_t aRoundNonce, const NodeUid &aUID) override
	{
		if (hubState != State::Allocating || allocation.state != AllocationContext::State::Discovering || !allocation.pending
			|| allocation.pending.value().messageNumber != aMessageNumber
			|| aRoundNonce != allocation.roundNonce || Helpers::getAllocationBucket(aUID, aRoundNonce) != allocation.bucket) {
			return;
		}
		for (const NodeUid &uid : allocation.discovered) {
			if (uid == aUID) {
				return;
			}
		}
		if (allocation.discovered.size() < kMaxNodeCount) {
			allocation.discovered.push_back(aUID);
		}
	}

	void handleAssignAddressAnswer(uint8_t aMessageNumber, const NodeUid &aUID,
		uint32_t aTransactionId, uint8_t aNodeId) override
	{
		DeviceWrapper *dev = getDevice(aNodeId);
		if (hubState != State::Allocating || dev == nullptr || dev->state != DeviceState::Assigning
			|| !dev->pending || dev->pending.value().msgType != MessageType::AssignAddressReq
			|| dev->pending.value().messageNumber != aMessageNumber) {
			return;
		}
		const NodeAllocation &entry = nodeAllocations[aNodeId - 1].value();
		if (entry.uid == aUID && entry.transactionId == aTransactionId) {
			dev->pending.reset();
			dev->allocationAttempts = 0;
			dev->nextCall = Time::milliseconds();
			dev->state = DeviceState::InfoRequest;
		}
	}

private:
	void handleDeviceInfoAnswer(uint8_t aTranceiverUID, uint8_t aMessageNumber, DeviceVersion aVersion,
		const NodeUid &aUID, const void * /*aName*/, size_t /*aNameLen*/) override
	{

		if (hubState == State::Scanning && scanPending && aTranceiverUID == scanUid && scanPending.value().messageNumber == aMessageNumber) {
			DeviceWrapper &dev = hub[aTranceiverUID];
			registerDevice(dev, aTranceiverUID, aVersion, aUID);
			nextScanAddress();
			return;
		}

		DeviceWrapper *dev = getDevice(aTranceiverUID);

		// Если устройства нет - то это не нам ответили
		if (dev == nullptr) {
			return;
		}

		if (dev->pending && dev->pending.value().msgType == MessageType::DeviceInfoReq
			&& dev->pending.value().messageNumber == aMessageNumber && dev->state == DeviceState::InfoRequest) {
			if (hubState == State::Allocating && nodeAllocations[aTranceiverUID - 1].value().uid != aUID) {
				return;
			}
			registerDevice(*dev, aTranceiverUID, aVersion, aUID);
		}
	}

	void registerDevice(DeviceWrapper &aDevice, uint8_t aDeviceId, DeviceVersion aVersion,
		const NodeUid &aPermanentUID)
	{
		// Восстановим связь постоянного UID и адреса непосредственно из ответа ноды
		auto &entry = nodeAllocations[aDeviceId - 1];
		if (!entry || entry.value().uid != aPermanentUID) {
			entry = NodeAllocation{aPermanentUID, ++allocationTransaction, aDeviceId};
		}

		aDevice.pending.reset();
		aDevice.uid = aPermanentUID;
		aDevice.version = aVersion;
		aDevice.state = DeviceState::Running;
		aDevice.timeoutCounter = 0;
		aDevice.allocationAttempts = 0;

		if (observer)
			observer->deviceRegisteredEv(aDevice.uid, aDeviceId, aDevice.version);
	}

	void handleAck(uint8_t aTranceiverUID, uint8_t aMessageNumber, Result aReturnCode) override
	{
		DeviceWrapper *dev = getDevice(aTranceiverUID);

		// Не регистрируем устройства по неподтвержденному ACK
		if (dev == nullptr) {
			return;
		}

		if (hubState == State::Allocating) {
			return; // Назначение и регистрация завершаются только своими ответами
		}

		// Иначе разбираемся что это за ответ
		dev->lastAck = Time::milliseconds();
		// Если мы ожидаем ответа и получаем ответ с правильным номером сообщения
		if (dev->pending && dev->pending.value().messageNumber == aMessageNumber) {
			if (observer) {
				observer->onAckReceivedEv(dev->uid, aTranceiverUID, dev->pending.value().msgType, aReturnCode);
			}

			switch (dev->state) {
				case DeviceState::Probing:
					// Однозначно пришел ответ на Probe, например в случае потери устройства и перерегистрации
					dev->state = DeviceState::InfoRequest;
					break;

				case DeviceState::Running: {
					// Если ответ пришел в рабочем режиме - смотрим что мы отправляли
					switch (dev->pending.value().msgType) {
						case MessageType::Command:
							if (observer)
								observer->onCommandResultEv(dev->uid, aTranceiverUID, aReturnCode);
							break;

						case MessageType::Reboot:
							// Пришел ответ на ребут, не знаю что с этим делать
							break;
						case MessageType::BlobRequest:
							if (observer)
								observer->onRequestErrorEv(dev->uid, aTranceiverUID, aReturnCode);
							break;

						default:
							// Странный ACK, обработать как ошибку
							break;
					}
				} break;

				case DeviceState::FileTransfer: {
					switch (dev->pending.value().msgType) {
						case MessageType::FileWriteChunk:
							dev->fileTransContext.packetAck = aReturnCode;

							break;
						case MessageType::FileWriteRequest:
							if (aReturnCode == Result::Ok) {
								dev->fileTransContext.state = FileTransferContext::State::Sending;
							} else {
								dev->fileTransContext.state = FileTransferContext::State::Cancel;
							}
							break;
						case MessageType::FileWriteFinalize:
							dev->state = DeviceState::Running;
							if (observer)
								observer->fileWriteResultEv(dev->uid, aTranceiverUID, aReturnCode);
							break;
						// Недопустимо или слейв сам иницирует взаимодействие
						default:
							break;
					}
				}

				default:
					break;
			}

			dev->pending.reset();
		}
	}

	Result handleBlobAnswer(uint8_t aTranceiverUID, uint8_t aMessageNumber, uint8_t aRequest, const uint8_t *aData,
		uint8_t aLength) override
	{
		DeviceWrapper *dev = getDevice(aTranceiverUID);

		// Если устройства нет - непонятно кто там отправляет блобы без реквеста
		if (dev == nullptr) {
			return Result::Error;
		}

		// Проверим что спрашивали мы
		if (dev->pending.has_value() && dev->pending.value().messageNumber == aMessageNumber
			&& dev->pending.value().msgType == MessageType::BlobRequest) {

			dev->pending.reset();
			if (observer)  {
				return observer->blobAnswerEvReceived(dev->uid, aTranceiverUID, aRequest, aData, aLength);
			}
		}

		return Result::Error;
	}

	void handleDeviceHealth(uint8_t aTransmitUID, uint8_t aMessageNumber, Health aHealth, uint16_t aFlags) override
	{
		DeviceWrapper *dev = getDevice(aTransmitUID);

		// Если устройства нет - непонятно кто там отправляет блобы без реквеста
		if (dev == nullptr) {
			return;
		}

		if (dev->pending.has_value() && dev->pending.value().messageNumber == aMessageNumber
			&& dev->pending.value().msgType == MessageType::HealthReq) {
			if (observer)
				observer->deviceHealthReceivedEv(dev->uid, aTransmitUID, aHealth, aFlags);
			dev->pending.reset();
		}
	}

	void cmdToDeviceImpl(uint8_t aDeviceId, uint8_t aCommand, uint8_t aValue)
	{
		DeviceWrapper *device = getDevice(aDeviceId);
		if (device == nullptr) {
			return;
		}

		DeviceWrapper &dev = *device;
		updateDevicePending(dev, Base::sendCommand(aDeviceId, aCommand, aValue), MessageType::Command);
	}

	void deviceRequestImpl(uint8_t aDeviceId, uint8_t aRequest, uint8_t aRequestSize)
	{
		DeviceWrapper *device = getDevice(aDeviceId);
		if (device == nullptr) {
			return;
		}

		DeviceWrapper &dev = *device;
		updateDevicePending(dev, Base::sendBlobRequest(aDeviceId, aRequest, aRequestSize), MessageType::BlobRequest);
	}

	void deviceFileWriteRequestImpl(uint8_t aDeviceId, uint8_t aFile, size_t aSize)
	{
		DeviceWrapper *device = getDevice(aDeviceId);
		if (device == nullptr) {
			return;
		}

		DeviceWrapper &dev = *device;
		updateDevicePending(dev, Base::fileWriteRequest(aDeviceId, aFile, aSize), MessageType::FileWriteRequest);
	}

	void sendChunkImpl(uint8_t aDeviceId, uint8_t aFileNum, const void *aChunk, uint8_t aChunkSize)
	{
		DeviceWrapper *device = getDevice(aDeviceId);
		if (device == nullptr) {
			return;
		}

		DeviceWrapper &dev = *device;

		if (aFileNum != dev.fileTransContext.file) {
			return;
		}

		updateDevicePending(dev, Base::fileWriteChunk(aDeviceId, dev.fileTransContext.file, aChunk, aChunkSize), MessageType::FileWriteChunk);
	}

	void fileWriteFinalizeImpl(uint8_t aDeviceId, uint8_t aFileNum, uint16_t aChunkNumber, uint64_t aCrc)
	{
		DeviceWrapper *device = getDevice(aDeviceId);
		if (device == nullptr) {
			return;
		}

		DeviceWrapper &dev = *device;
		updateDevicePending(dev, Base::fileWriteFinalize(aDeviceId, aFileNum, aChunkNumber, aCrc), MessageType::FileWriteFinalize);
	}

	void deviceHealthReqImpl(uint8_t aDeviceId)
	{
		DeviceWrapper *device = getDevice(aDeviceId);
		if (device == nullptr) {
			return;
		}

		DeviceWrapper &dev = *device;
		updateDevicePending(dev, Base::sendHealthRequest(aDeviceId), MessageType::HealthReq);
	}

	void processDevice(uint8_t aDeviceId, DeviceWrapper &aDevice, std::chrono::milliseconds aTime)
	{
		if (aTime >= aDevice.nextCall) {
			// Базовое время следующего действия
			auto updateTime = std::chrono::milliseconds{100};

			switch (aDevice.state) {
				case DeviceState::Assigning: {
					const NodeAllocation &entry = nodeAllocations[aDeviceId - 1].value();
					updateDevicePending(aDevice, Base::sendAssignAddressRequest(entry.uid, entry.transactionId, aDeviceId), MessageType::AssignAddressReq);
					++aDevice.allocationAttempts;
					updateTime = std::chrono::milliseconds{0};
				} break;
				case DeviceState::Probing: {
					updateDevicePending(aDevice, Base::sendProbe(aDeviceId), MessageType::Probe);
					updateTime = std::chrono::milliseconds{1000};
				} break;
				case DeviceState::InfoRequest: {
					updateDevicePending(aDevice, Base::sendDeviceInfoRequest(aDeviceId), MessageType::DeviceInfoReq);
					if (hubState == State::Allocating) {
						++aDevice.allocationAttempts;
						updateTime = std::chrono::milliseconds{0};
					} else {
						updateTime = std::chrono::milliseconds{1000};
					}
				} break;
				case DeviceState::Running: {
					// Сначала посмотрим в очередь команд
					if (!aDevice.commandQueue.empty()) {
						const auto val = aDevice.commandQueue.front();
						aDevice.commandQueue.pop();
						cmdToDeviceImpl(aDeviceId, val.first, val.second);
						// Потом в очередь запросов (ручных)
					} else if (!aDevice.requestQueue.empty()) {
						const auto request = aDevice.requestQueue.front();
						aDevice.requestQueue.pop();
						deviceRequestImpl(aDeviceId, request.first, request.second);
						// Потом посмотрим, не пора ли спросить флаги и health
					} else if (aTime - aDevice.lastHealthReq >= kHealthTimeout) {
						aDevice.lastHealthReq = aTime;
						deviceHealthReqImpl(aDeviceId);
						// Потом в очередь расписаний телеметрии
					} else if (!aDevice.telemSched.empty()) {
						TelemetryUnit *telem = nullptr;
						for (auto &pos : aDevice.telemSched) {
							if (aTime - pos.lastUpdateTime >= pos.updateTime) {
								pos.lastUpdateTime = aTime;
								telem = &pos;
							}
						}
						if (telem != nullptr) {
							deviceRequestImpl(aDeviceId, telem->req, telem->reqSize);
						}
					} else {
						// Делать нечего
					}
				} break;
				case DeviceState::FileTransfer: {
					switch (aDevice.fileTransContext.state) {
						case FileTransferContext::State::Request: {
							deviceFileWriteRequestImpl(
								aDeviceId, aDevice.fileTransContext.file, aDevice.fileTransContext.totalSize);
							// Раньше будет или ответ или ошибка таймаута
							updateTime = std::chrono::milliseconds{50};
						} break;

						case FileTransferContext::State::Sending: {
							// Первый чанк шлем без проверок
							if (aDevice.fileTransContext.firstPacket) {
								const size_t chunk = std::min(aDevice.fileTransContext.chunkSize,
									aDevice.fileTransContext.totalSize - aDevice.fileTransContext.sentOffset);
								const uint8_t *ptr = static_cast<const uint8_t *>(aDevice.fileTransContext.data)
									+ aDevice.fileTransContext.sentOffset;
								sendChunkImpl(aDeviceId, aDevice.fileTransContext.file, ptr, chunk);
								aDevice.fileTransContext.firstPacket = false;
							} else {
								// Теперь можно уже оформлять event-based с переповторами
								// Вышел таймаут - сброс отправки, чето сломалось
								if (!aDevice.fileTransContext.packetAck) {
									aDevice.fileTransContext.state = FileTransferContext::State::Cancel;
								} else {
									const uint8_t lastChunk = static_cast<uint8_t>(std::min(aDevice.fileTransContext.chunkSize,
										aDevice.fileTransContext.totalSize - aDevice.fileTransContext.sentOffset));

									// Ответ пришел,смотрим что там устройство сообщило
									if (aDevice.fileTransContext.packetAck.value() == Result::Busy) {
										// Было занято, переотправим последний пакет
										const uint8_t *ptr = static_cast<const uint8_t *>(aDevice.fileTransContext.data)
											+ aDevice.fileTransContext.sentOffset;
										sendChunkImpl(aDeviceId, aDevice.fileTransContext.file, ptr, lastChunk);
									} else if (aDevice.fileTransContext.packetAck.value() == Result::Wait) {
										// Подождем немножко
										updateTime = std::chrono::milliseconds{200};
									} else if (aDevice.fileTransContext.packetAck.value() == Result::Ok) {
										// Пометим чанк как отправленный
										aDevice.fileTransContext.sentOffset += lastChunk;
										++aDevice.fileTransContext.chunkSent;

										// если отправили все чанки - выходим
										if (aDevice.fileTransContext.sentOffset == aDevice.fileTransContext.totalSize) {
											// Отправили всё, теперь пора финализировать
											aDevice.fileTransContext.state = FileTransferContext::State::Finalize;
											aDevice.fileTransContext.packetAck.reset();
											updateTime = std::chrono::milliseconds{500};
										} else {
											const uint8_t nextChunk = static_cast<uint8_t>(std::min(aDevice.fileTransContext.chunkSize,
												aDevice.fileTransContext.totalSize - aDevice.fileTransContext.sentOffset));

											const uint8_t *ptr = static_cast<const uint8_t *>(aDevice.fileTransContext.data)
												+ aDevice.fileTransContext.sentOffset;
											sendChunkImpl(aDeviceId, aDevice.fileTransContext.file, ptr, nextChunk);
										}
									} else {
										// Во всех других случаях пишем ошибку
										aDevice.fileTransContext.state = FileTransferContext::State::Cancel;
									}

									aDevice.fileTransContext.packetAck.reset();
								}
							}
						} break;

						case FileTransferContext::State::Finalize: {
							// Посчитаем CRC64 от нашего буффера
							const auto crc
								= CrcFile::calculate(aDevice.fileTransContext.data, aDevice.fileTransContext.totalSize);
							fileWriteFinalizeImpl(
								aDeviceId, aDevice.fileTransContext.file, static_cast<uint16_t>(aDevice.fileTransContext.chunkSent), crc);
							updateTime = std::chrono::milliseconds{500};
						} break;

						case FileTransferContext::State::Cancel: {
							// Сбросим режим если вернулась ошибка
							aDevice.fileTransContext = FileTransferContext{};
							aDevice.state = DeviceState::Running;
							Result result = aDevice.fileTransContext.packetAck ? aDevice.fileTransContext.packetAck.value() : Result::Error;
							if (observer) observer->fileWriteResultEv(aDevice.uid, aDeviceId, result);
						} break;
					}
				} break;

				case DeviceState::Suspended:
					break;
				case DeviceState::Lost:
					if (observer) observer->deviceLostEv(aDevice.uid, aDeviceId);
					aDevice.state = DeviceState::Probing;
					break;
			}

			aDevice.nextCall = aTime + updateTime;
		}
	}

	DeviceWrapper *getDevice(uint8_t aDeviceId)
	{
		auto it = hub.find(aDeviceId);
		if (it == hub.end())
			return nullptr;
		return &it->second;
	}

	static void updateDevicePending(DeviceWrapper &aDevice, uint8_t aMessageNumber, MessageType aMessageType)
	{
		PendingTrans pending;
		pending.messageNumber = aMessageNumber;
		pending.msgType = aMessageType;
		pending.timestamp = Time::milliseconds();
		aDevice.pending.emplace(pending);
	}
};
} // namespace RS

#endif // LIB_DEVICEHUB_HPP_
