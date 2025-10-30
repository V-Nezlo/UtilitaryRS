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
#include <chrono>
#include <map>
#include <optional>
#include <queue>
#include <string>

namespace RS {

/// \brief Интерфейс наблюдателя за DeviceHub
class DeviceHubObserver {
public:
	virtual void onAckNotReceivedEv(const std::string &aName, MessageType aMessage) = 0;
	virtual void onAckReceivedEv(const std::string &aName, MessageType aMessage, Result aCode) = 0;

	virtual void onCommandResultEv(const std::string &aName, Result aReturn) = 0;
	virtual void onRequestErrorEv(const std::string &aName, Result aReturn) = 0;

	virtual Result blobAnswerEvReceived(const std::string &aName, uint8_t Request, const void *aData, size_t aSize) = 0;

	virtual void deviceRegisteredEv(const std::string &aName, DeviceVersion aVersion) = 0;
	virtual void deviceLostEv(const std::string &aName) = 0;

	virtual Result fileWriteResultEv(const std::string &aName, Result aReturn) = 0;
	virtual void deviceHealthReceivedEv(const std::string &aName, Health aHealth, uint16_t aFlags) = 0;
};

template<uint8_t MaxDeviceCount, class Interface, typename Time, typename Crc8, typename CrcFile, size_t ParserSize>
class DeviceHub : public RsHandler<Interface, Crc8, ParserSize> {
	using Base = RsHandler<Interface, Crc8, ParserSize>;

	static constexpr size_t kTimeoutErrorForLost{20};
	static constexpr auto kHealthTimeout{std::chrono::milliseconds{1000}};

	struct PendingTrans {
		uint8_t messageNumber; // Номер сообщения, который был отправлен
		MessageType msgType; // Тип сообщения которое отправили
		std::chrono::milliseconds timestamp; // время отправления
	};

	enum class DeviceState : uint8_t { Probing, InfoRequest, Running, FileTransfer, Suspended, Lost };

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
		std::string name;
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

		FileTransferContext fileTransContext;

		DeviceWrapper()
		{
			name.reserve(16);
		}
	};

public:

	/// \brief Конструктор хаба
	/// \param aHubVersion версия устройства хаба
	/// \param aIface интерфейс связи
	/// \param aName имя хаба, по умолчанию Master
	/// \param aUID uid хаба, по умолчанию 0
	DeviceHub(const DeviceVersion &aHubVersion, Interface &aIface, std::string aName = "Master", uint8_t aUID = 0) :
		Base(aName.c_str(), aHubVersion, aUID, aIface),
		hub{},
		observer{nullptr},
		nameToUid{},
		temporaryUid{std::nullopt}
	{ }

	/// \brief Зарегистрировать наблюдателя
	/// \param aObserver
	void registerObserver(DeviceHubObserver *aObserver)
	{
		observer = aObserver;
	}

	/// \brief Просканировать шину
	/// \param aBroadcast - широковещательный запрос, для шин с арбитражем или систем с селективной адресацией
	/// Для шин без арбитража строго prohibited, вызовет конфликты
	void probeAll(bool aBroadcast = false)
	{
		for (uint8_t uid = 1; uid < MaxDeviceCount; ++uid) { Base::sendProbe(aBroadcast ? kReservedUID : uid); }
	}

	/// \brief Базовая функция, вызывать в планировщике
	/// \param aTime текущее время
	void process(std::chrono::milliseconds aTime)
	{
		for (auto &pos : hub) {
			// Базовая обработка
			DeviceWrapper &dev = pos.second;
			processDevice(dev, aTime);

			// Проверим таймауты
			if (dev.pending.has_value() && aTime - dev.pending.value().timestamp >= std::chrono::milliseconds{200}) {
				++dev.timeoutCounter;

				if (dev.timeoutCounter >= kTimeoutErrorForLost) {
					dev.timeoutCounter = 0;
					dev.state = DeviceState::Lost;
				}

				if (observer) {
					observer->onAckNotReceivedEv(dev.name, dev.pending.value().msgType);
				}

				// Сбросим процедуру отправки файла если зафакапились
				if (dev.state == DeviceState::FileTransfer) {
					dev.fileTransContext.state = FileTransferContext::State::Cancel;
				}

				dev.pending.reset();
			}
		}
	}

	/// \brief Отправить команду на устройство - обработка через очередь
	/// \param aDeviceName имя устройства
	/// \param aCommand команда
	/// \param aValue аргумент
	/// \return true если успех
	bool sendCmdToDevice(const std::string &aDeviceName, uint8_t aCommand, uint8_t aValue)
	{
		uint8_t devUid = getUIDFromName(aDeviceName);
		if (devUid == kReservedUID) {
			return false;
		}

		DeviceWrapper &dev = hub[devUid];

		if (dev.state != DeviceState::Running) {
			return false;
		}

		dev.commandQueue.push(std::make_pair(aCommand, aValue));
		return true;
	}

	/// \brief Отправить разовый реквест на устройство, очередь
	/// \param aDeviceName имя устройства
	/// \param aBlobRequest номер запроса
	/// \return true если успех
	bool sendBlobRequestToDevice(const std::string &aDeviceName, uint8_t aBlobRequest, uint8_t aBlobSize)
	{
		uint8_t devUid = getUIDFromName(aDeviceName);
		if (devUid == kReservedUID) {
			return false;
		}

		DeviceWrapper &dev = hub[devUid];

		if (dev.state != DeviceState::Running) {
			return false;
		}

		dev.requestQueue.push(std::make_pair(aBlobRequest, aBlobSize));
		return true;
	}

	/// \brief Создать запрос по расписанию для устройства
	/// \param aDeviceName имя устройства
	/// \param aReq запрос
	/// \param aReqSize длина запроса
	/// \param aTimeout период опроса
	/// \return true если успех
	bool createSchedRequest(
		const std::string &aDeviceName, uint8_t aReq, uint8_t aReqSize, std::chrono::milliseconds aTimeout)
	{
		uint8_t devUid = getUIDFromName(aDeviceName);
		if (devUid == kReservedUID) {
			return false;
		}

		DeviceWrapper &dev = hub[devUid];

		TelemetryUnit entry{aReq, aReqSize, aTimeout, std::chrono::milliseconds{0}};
		dev.telemSched.push_back(entry);
		return true;
	}

	/// \brief Отправить файл
	/// \param aDeviceName имя устройства
	/// \param aFile номер файла
	/// \param aData данные
	/// \param aSize длина данных
	/// \param aChunkSize размер чанка
	/// \return true если команда принята
	bool sendFile(const std::string &aDeviceName, uint8_t aFile, const void *aData, size_t aSize, size_t aChunkSize)
	{
		uint8_t devUid = getUIDFromName(aDeviceName);
		if (devUid == kReservedUID) {
			return false;
		}

		DeviceWrapper &dev = hub[devUid];
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
	std::map<std::string, uint8_t> nameToUid;
	std::optional<uint8_t> temporaryUid;

	// RsHandler interface
	void handleDeviceInfoAnswer(uint8_t aTranceiverUID, uint8_t aMessageNumber, DeviceVersion aVersion,
		const void *aName, size_t aNameLen) override
	{
		DeviceWrapper *dev = getDevice(aTranceiverUID);

		// Если устройства нет - то походу это не нам ответили
		if (dev == nullptr) {
			return;
		}

		if (dev->state == DeviceState::InfoRequest) {
			// Заполним дескриптор
			dev->name.clear();
			dev->name.assign(static_cast<const char *>(aName), aNameLen);
			dev->version = aVersion;
			dev->state = DeviceState::Running;

			nameToUid[dev->name] = aTranceiverUID;

			if (observer)
				observer->deviceRegisteredEv(dev->name, dev->version);
		}
	}

	void handleAck(uint8_t aTranceiverUID, uint8_t aMessageNumber, Result aReturnCode) override
	{
		DeviceWrapper *dev = getDevice(aTranceiverUID);

		// Если устройства нет - создаем его и выходим
		if (dev == nullptr) {
			hub[aTranceiverUID] = DeviceWrapper{};
			temporaryUid = aTranceiverUID;
			return;
		}

		// Иначе разбираемся что это за ответ
		dev->lastAck = Time::milliseconds();
		// Если мы ожидаем ответа и получаем ответ с правильным номером сообщения
		if (dev->pending.has_value() && dev->pending.value().messageNumber == aMessageNumber) {
			if (observer) {
				observer->onAckReceivedEv(dev->name, dev->pending.value().msgType, aReturnCode);
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
								observer->onCommandResultEv(dev->name, aReturnCode);
							break;

						case MessageType::Reboot:
							// Пришел ответ на ребут, не знаю что с этим делать
							break;
						case MessageType::BlobRequest:
							if (observer)
								observer->onRequestErrorEv(dev->name, aReturnCode);
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
								observer->fileWriteResultEv(dev->name, aReturnCode);
							break;
					}
				}
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


			if (observer) return observer->blobAnswerEvReceived(dev->name, aRequest, aData, aLength);
			dev->pending.reset();
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
				observer->deviceHealthReceivedEv(dev->name, aHealth, aFlags);
			dev->pending.reset();
		}
	}

	void cmdToDeviceImpl(const std::string &aDeviceName, uint8_t aCommand, uint8_t aValue)
	{
		uint8_t devUid = getUIDFromName(aDeviceName);

		if (devUid == kReservedUID) {
			return;
		}

		DeviceWrapper &dev = hub[devUid];
		updateDevicePending(dev, Base::sendCommand(devUid, aCommand, aValue), MessageType::Command);
	}

	void deviceRequestImpl(const std::string &aDeviceName, uint8_t aRequest, uint8_t aRequestSize)
	{
		uint8_t devUid = getUIDFromName(aDeviceName);

		if (devUid == kReservedUID) {
			return;
		}

		DeviceWrapper &dev = hub[devUid];
		updateDevicePending(dev, Base::sendBlobRequest(devUid, aRequest, aRequestSize), MessageType::BlobRequest);
	}

	void deviceFileWriteRequestImpl(const std::string &aDeviceName, uint8_t aFile, size_t aSize)
	{
		uint8_t devUid = getUIDFromName(aDeviceName);

		if (devUid == kReservedUID) {
			return;
		}

		DeviceWrapper &dev = hub[devUid];
		updateDevicePending(dev, Base::fileWriteRequest(devUid, aFile, aSize), MessageType::FileWriteRequest);
	}

	void sendChunkImpl(const std::string &aDeviceName, uint8_t aFileNum, const void *aChunk, uint8_t aChunkSize)
	{
		uint8_t devUid = getUIDFromName(aDeviceName);

		if (devUid == kReservedUID) {
			return;
		}

		DeviceWrapper &dev = hub[devUid];
		updateDevicePending(dev, Base::fileWriteChunk(devUid, dev.fileTransContext.file, aChunk, aChunkSize), MessageType::FileWriteChunk);
	}

	void fileWriteFinalizeImpl(const std::string &aDeviceName, uint8_t aFileNum, uint16_t aChunkNumber, uint64_t aCrc)
	{
		uint8_t devUid = getUIDFromName(aDeviceName);

		if (devUid == kReservedUID) {
			return;
		}

		DeviceWrapper &dev = hub[devUid];
		updateDevicePending(dev, Base::fileWriteFinalize(devUid, aFileNum, aChunkNumber, aCrc), MessageType::FileWriteFinalize);
	}

	void deviceHealthReqImpl(const std::string &aDeviceName)
	{
		uint8_t devUid = getUIDFromName(aDeviceName);

		if (devUid == kReservedUID) {
			return;
		}

		DeviceWrapper &dev = hub[devUid];
		updateDevicePending(dev, Base::sendHealthRequest(devUid), MessageType::HealthReq);
	}

	uint8_t getUIDFromName(const std::string &aName)
	{
		auto it = nameToUid.find(aName);
		if (it == nameToUid.end()) {
			return kReservedUID; // если имя не найдено
		}
		return it->second;
	}

	void processDevice(DeviceWrapper &aDevice, std::chrono::milliseconds aTime)
	{
		if (aTime >= aDevice.nextCall) {
			// Базовое время следующего действия
			auto updateTime = std::chrono::milliseconds{50};

			switch (aDevice.state) {
				case DeviceState::Probing: {
					updateDevicePending(aDevice, Base::sendProbe(getUIDFromName(aDevice.name)), MessageType::Probe);
					updateTime = std::chrono::milliseconds{1000};
				} break;
				case DeviceState::InfoRequest: {
					if (temporaryUid) {
						updateDevicePending(aDevice, Base::sendDeviceInfoRequest(temporaryUid.value()), MessageType::DeviceInfoReq);
						temporaryUid.reset();
					} else {
						updateDevicePending(aDevice, Base::sendDeviceInfoRequest(getUIDFromName(aDevice.name)), MessageType::DeviceInfoReq);
					}
					updateTime = std::chrono::milliseconds{1000};
				} break;
				case DeviceState::Running: {
					// Сначала посмотрим в очередь команд
					if (!aDevice.commandQueue.empty()) {
						const auto val = aDevice.commandQueue.front();
						aDevice.commandQueue.pop();
						cmdToDeviceImpl(aDevice.name, val.first, val.second);
						// Потом в очередь запросов
					} else if (!aDevice.requestQueue.empty()) {
						const auto request = aDevice.requestQueue.front();
						aDevice.requestQueue.pop();
						deviceRequestImpl(aDevice.name, request.first, request.second);
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
							deviceRequestImpl(aDevice.name, telem->req, telem->reqSize);
						}
						// Потом посмотрим, не пора ли спросить флаги и health
					} else if (aTime - aDevice.lastHealthReq >= kHealthTimeout) {
						aDevice.lastHealthReq = aTime;
						deviceHealthReqImpl(aDevice.name);
					} else {
						// Делать нечего
					}
				} break;
				case DeviceState::FileTransfer: {
					switch (aDevice.fileTransContext.state) {
						case FileTransferContext::State::Request: {
							deviceFileWriteRequestImpl(
								aDevice.name, aDevice.fileTransContext.file, aDevice.fileTransContext.totalSize);
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
								sendChunkImpl(aDevice.name, aDevice.fileTransContext.file, ptr, chunk);
								aDevice.fileTransContext.firstPacket = false;
							} else {
								// Теперь можно уже оформлять event-based с переповторами
								// Вышел таймаут - сброс отправки, чето сломалось
								if (!aDevice.fileTransContext.packetAck) {
									aDevice.fileTransContext.state = FileTransferContext::State::Cancel;
								} else {
									const size_t lastChunk = std::min(aDevice.fileTransContext.chunkSize,
										aDevice.fileTransContext.totalSize - aDevice.fileTransContext.sentOffset);

									// Ответ пришел,смотрим что там устройство сообщило
									if (aDevice.fileTransContext.packetAck.value() == Result::Busy) {
										// Было занято, переотправим последний пакет
										const uint8_t *ptr = static_cast<const uint8_t *>(aDevice.fileTransContext.data)
											+ aDevice.fileTransContext.sentOffset;
										sendChunkImpl(aDevice.name, aDevice.fileTransContext.file, ptr, lastChunk);
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
											const size_t nextChunk = std::min(aDevice.fileTransContext.chunkSize,
												aDevice.fileTransContext.totalSize - aDevice.fileTransContext.sentOffset);

											const uint8_t *ptr = static_cast<const uint8_t *>(aDevice.fileTransContext.data)
												+ aDevice.fileTransContext.sentOffset;
											sendChunkImpl(aDevice.name, aDevice.fileTransContext.file, ptr, nextChunk);
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
								aDevice.name, aDevice.fileTransContext.file, aDevice.fileTransContext.chunkSent, crc);
							updateTime = std::chrono::milliseconds{500};
						} break;

						case FileTransferContext::State::Cancel: {
							// Сбросим режим если вернулась ошибка
							aDevice.fileTransContext = FileTransferContext{};
							aDevice.state == DeviceState::Running;
							Result result = aDevice.fileTransContext.packetAck ? aDevice.fileTransContext.packetAck.value() : Result::Error;
							if (observer) observer->fileWriteResultEv(aDevice.name, result);
						} break;
					}
				} break;

				case DeviceState::Suspended:
					break;
				case DeviceState::Lost:
					if (observer) observer->deviceLostEv(aDevice.name);
					aDevice.state = DeviceState::Probing;
					break;
			}

			aDevice.nextCall = aTime + updateTime;
		}
	}

	DeviceWrapper *getDevice(uint8_t uid)
	{
		auto it = hub.find(uid);
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
