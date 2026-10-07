#include <UtilitaryRS/DeviceHub.hpp>
#include <UtilitaryRS/Crc8.hpp>
#include <UtilitaryRS/Crc64.hpp>

#include <array>
#include <cassert>
#include <memory>
#include <vector>

class ScanTime {
public:
	static std::chrono::milliseconds milliseconds()
	{
		return now;
	}

	inline static std::chrono::milliseconds now{0};
};

class ScanSerial {
public:
	void write(const uint8_t *aData, size_t aLength)
	{
		data.insert(data.end(), aData, aData + aLength);
	}

	std::vector<uint8_t> data;
};

class ScanObserver : public RS::DeviceHubObserver {
public:
	void onAckNotReceivedEv(const RS::NodeUid &, uint8_t, RS::MessageType) override { }
	void onAckReceivedEv(const RS::NodeUid &, uint8_t, RS::MessageType, RS::Result) override { }
	void onCommandResultEv(const RS::NodeUid &aUID, uint8_t aDeviceId, RS::Result) override
	{ commands.emplace_back(aUID, aDeviceId); }
	void onRequestErrorEv(const RS::NodeUid &aUID, uint8_t aDeviceId, RS::Result) override
	{ requestErrors.emplace_back(aUID, aDeviceId); }
	RS::Result blobAnswerEvReceived(const RS::NodeUid &, uint8_t, uint8_t, const void *, size_t) override { return RS::Result::Ok; }
	void deviceLostEv(const RS::NodeUid &, uint8_t) override { }
	RS::Result fileWriteResultEv(const RS::NodeUid &, uint8_t, RS::Result aReturn) override { return aReturn; }
	void deviceHealthReceivedEv(const RS::NodeUid &, uint8_t, RS::Health, uint16_t) override { }

	void deviceRegisteredEv(const RS::NodeUid &aUID, uint8_t aDeviceId, RS::DeviceVersion) override
	{
		registered.emplace_back(aUID, aDeviceId);
	}

	std::vector<std::pair<RS::NodeUid, uint8_t>> registered;
	std::vector<std::pair<RS::NodeUid, uint8_t>> commands;
	std::vector<std::pair<RS::NodeUid, uint8_t>> requestErrors;
};

using Hub = RS::DeviceHub<ScanSerial, ScanTime, Crc8, Crc64, 256>;
using Node = RS::RsHandler<ScanSerial, Crc8, 256>;
using Parser = RS::RsParser<256, Crc8>;

RS::Header readHeader(const ScanSerial &aSerial)
{
	Parser parser;
	assert(parser.update(aSerial.data.data(), aSerial.data.size()) == aSerial.data.size());
	assert(parser.state() == Parser::State::Done);
	RS::Header header;
	memcpy(&header, parser.data(), sizeof(header));
	return header;
}

void testEmptyScan()
{
	ScanTime::now = std::chrono::milliseconds{0};
	RS::DeviceVersion version{};
	ScanSerial serial;
	Hub hub(version, RS::NodeUid{0x80}, serial);
	hub.probeAll();
	assert(hub.state() == Hub::State::Waiting);
	assert(serial.data.empty());

	for (uint8_t uid = 1; uid <= RS::kMaxNodeCount; ++uid) {
		hub.process(ScanTime::milliseconds());
		RS::Header header = readHeader(serial);
		assert(header.receiverUID == uid);
		assert(header.messageType == RS::MessageType::DeviceInfoReq);
		serial.data.clear();

		// Повторный запуск и process до таймаута не отправляют новый запрос
		hub.probeAll();
		ScanTime::now += std::chrono::milliseconds{199};
		hub.process(ScanTime::milliseconds());
		assert(serial.data.empty());
		ScanTime::now += std::chrono::milliseconds{1};
	}

	hub.process(ScanTime::milliseconds());
	assert(hub.state() == Hub::State::Running);
	assert(serial.data.empty());
}

void testAllocatedScan()
{
	ScanTime::now = std::chrono::milliseconds{0};
	RS::DeviceVersion version{};
	ScanSerial nodeSerial;
	std::array<std::string, RS::kMaxNodeCount> names;
	std::vector<std::unique_ptr<Node>> nodes;
	for (uint8_t uid = 1; uid <= RS::kMaxNodeCount; ++uid) {
		names[uid - 1] = "node" + std::to_string(uid);
		nodes.push_back(std::make_unique<Node>(names[uid - 1].c_str(), version, RS::NodeUid{uid}, nodeSerial, uid));
	}

	// Пересоздание мастера не меняет адреса существующих нод
	for (int restart = 0; restart < 2; ++restart) {
		ScanSerial masterSerial;
		Hub hub(version, RS::NodeUid{0x80}, masterSerial);
		ScanObserver observer;
		hub.registerObserver(&observer);
		hub.probeAll();
		for (uint8_t uid = 1; uid <= RS::kMaxNodeCount; ++uid) {
			hub.process(ScanTime::milliseconds());
			RS::Header request = readHeader(masterSerial);
			assert(request.receiverUID == uid);
			nodes[uid - 1]->update(masterSerial.data.data(), masterSerial.data.size());
			masterSerial.data.clear();
			hub.update(nodeSerial.data.data(), nodeSerial.data.size());
			nodeSerial.data.clear();
			assert(observer.registered.size() == uid);
			assert(observer.registered.back().first == RS::NodeUid{uid});
			assert(observer.registered.back().second == uid);
			assert(nodes[uid - 1]->getUid() == uid);
			// Ответ на информацию подтверждается до следующего запроса
			assert(readHeader(masterSerial).messageType == RS::MessageType::Ack);
			nodes[uid - 1]->update(masterSerial.data.data(), masterSerial.data.size());
			masterSerial.data.clear();
		}
		assert(hub.state() == Hub::State::Running);
		assert(hub.sendCmdToDevice(32, 1, 0));
	}
}

void testUnexpectedResponses()
{
	ScanTime::now = std::chrono::milliseconds{0};
	RS::DeviceVersion version{};
	ScanSerial masterSerial;
	ScanSerial nodeSerial;
	Hub hub(version, RS::NodeUid{0x80}, masterSerial);
	ScanObserver observer;
	hub.registerObserver(&observer);
	hub.probeAll();
	hub.process(ScanTime::milliseconds());
	RS::Header request = readHeader(masterSerial);
	masterSerial.data.clear();

	// Информация с другого адреса не регистрирует устройство
	Node other("other", version, RS::NodeUid{2}, nodeSerial, 2);
	Hub sender(version, RS::NodeUid{0x80}, masterSerial);
	sender.sendDeviceInfoRequest(2);
	other.update(masterSerial.data.data(), masterSerial.data.size());
	masterSerial.data.clear();
	hub.update(nodeSerial.data.data(), nodeSerial.data.size());
	nodeSerial.data.clear();
	assert(observer.registered.empty());

	// ACK от неизвестной ноды и ответ с неверным номером не завершают запрос
	Parser parser;
	uint8_t buffer[256];
	RS::AckMessage ack{};
	ack.receiverUID = 0;
	ack.transmitUID = 33;
	ack.messageType = RS::MessageType::Ack;
	ack.number = request.number;
	size_t length = parser.create(buffer, &ack, sizeof(ack));
	hub.update(buffer, length);

	Node node("node1", version, RS::NodeUid{1}, nodeSerial, 1);
	RS::DeviceInfoReqMessage wrong{};
	wrong.receiverUID = 1;
	wrong.transmitUID = 0;
	wrong.messageType = RS::MessageType::DeviceInfoReq;
	wrong.number = static_cast<uint8_t>(request.number + 1);
	length = parser.create(buffer, &wrong, sizeof(wrong));
	node.update(buffer, length);
	hub.update(nodeSerial.data.data(), nodeSerial.data.size());
	nodeSerial.data.clear();
	assert(observer.registered.empty());
	masterSerial.data.clear();
	hub.process(ScanTime::milliseconds());
	assert(masterSerial.data.empty());
	assert(!hub.sendCmdToDevice(1, 1, 0));
}


void testDeviceIdRouting()
{
	ScanTime::now = std::chrono::milliseconds{0};
	RS::DeviceVersion version{};
	ScanSerial masterSerial, nodeSerial;
	Hub hub(version, RS::NodeUid{0x80}, masterSerial);
	ScanObserver observer;
	hub.registerObserver(&observer);
	Node first("same", version, RS::NodeUid{0xA2}, nodeSerial, 2);
	Node second("same", version, RS::NodeUid{0xB7}, nodeSerial, 7);

	// Отсутствующие адреса не создают устройства при вызове API.
	auto checkUnknown = [&](uint8_t id) {
		assert(!hub.sendCmdToDevice(id, 1, 2));
		assert(!hub.sendBlobRequestToDevice(id, 3, 4));
		assert(!hub.createSchedRequest(id, 3, 4, std::chrono::milliseconds{100}));
		assert(!hub.sendFile(id, 0, "data", 4, 4));
	};
	for (uint8_t id : {uint8_t{0}, uint8_t{1}, uint8_t{33}, RS::kReservedUID}) {
		checkUnknown(id);
	}

	auto tick = [&]() {
		hub.process(ScanTime::milliseconds());
		RS::Header header{};
		if (!masterSerial.data.empty()) {
			header = readHeader(masterSerial);
			first.update(masterSerial.data.data(), masterSerial.data.size());
			second.update(masterSerial.data.data(), masterSerial.data.size());
			masterSerial.data.clear();
			hub.update(nodeSerial.data.data(), nodeSerial.data.size());
			nodeSerial.data.clear();
			// ACK на ответ с информацией не требует ответа.
			masterSerial.data.clear();
		}
		ScanTime::now += std::chrono::milliseconds{100};
		return header;
	};
	assert(hub.probeAll());
	for (unsigned steps = 0; hub.state() != Hub::State::Running; ++steps) {
		assert(steps < 100);
		tick();
	}
	assert(observer.registered.size() == 2);
	assert(observer.registered[0].first == first.getNodeUid() && observer.registered[0].second == 2);
	assert(observer.registered[1].first == second.getNodeUid() && observer.registered[1].second == 7);
	for (uint8_t id : {uint8_t{0}, uint8_t{1}, uint8_t{33}, RS::kReservedUID}) {
		checkUnknown(id);
	}

	auto expectRequest = [&](uint8_t id, RS::MessageType type) {
		for (unsigned steps = 0; steps < 100; ++steps) {
			const auto header = tick();
			if (header.messageType == type) {
				assert(header.receiverUID == id);
				return;
			}
		}
		assert(false && "Expected request was not sent");
	};
	assert(hub.sendCmdToDevice(2, 1, 2));
	expectRequest(2, RS::MessageType::Command);
	assert(hub.sendCmdToDevice(7, 3, 4));
	expectRequest(7, RS::MessageType::Command);
	assert(observer.commands.size() == 2);
	assert(observer.commands[0] == observer.registered[0]);
	assert(observer.commands[1] == observer.registered[1]);

	assert(hub.sendBlobRequestToDevice(2, 5, 4));
	expectRequest(2, RS::MessageType::BlobRequest);
	assert(hub.sendBlobRequestToDevice(7, 6, 4));
	expectRequest(7, RS::MessageType::BlobRequest);
	assert(observer.requestErrors.size() == 2); // Ноды возвращают Unsupported.
	assert(observer.requestErrors[0] == observer.registered[0]);
	assert(observer.requestErrors[1] == observer.registered[1]);

	assert(hub.createSchedRequest(7, 7, 4, std::chrono::milliseconds{100}));
	expectRequest(7, RS::MessageType::BlobRequest);
	assert(observer.requestErrors.back() == observer.registered[1]);
	assert(hub.sendFile(2, 0, "data", 4, 4));
	expectRequest(2, RS::MessageType::FileWriteRequest);
}

int main()
{
	testEmptyScan();
	testAllocatedScan();
	testUnexpectedResponses();
	testDeviceIdRouting();
	return 0;
}
