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
	void onAckNotReceivedEv(const std::string &, RS::MessageType) override { }
	void onAckReceivedEv(const std::string &, RS::MessageType, RS::Result) override { }
	void onCommandResultEv(const std::string &, RS::Result) override { }
	void onRequestErrorEv(const std::string &, RS::Result) override { }
	RS::Result blobAnswerEvReceived(const std::string &, uint8_t, const void *, size_t) override { return RS::Result::Ok; }
	void deviceLostEv(const std::string &) override { }
	RS::Result fileWriteResultEv(const std::string &, RS::Result aReturn) override { return aReturn; }
	void deviceHealthReceivedEv(const std::string &, RS::Health, uint16_t) override { }

	void deviceRegisteredEv(const std::string &aName, RS::DeviceVersion) override
	{
		names.push_back(aName);
	}

	std::vector<std::string> names;
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
	Hub hub(version, serial);
	hub.probeAll();
	assert(hub.isScanning());
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
	assert(!hub.isScanning());
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
		nodes.push_back(std::make_unique<Node>(names[uid - 1].c_str(), version, uid, nodeSerial));
	}

	// Пересоздание мастера не меняет адреса существующих нод
	for (int restart = 0; restart < 2; ++restart) {
		ScanSerial masterSerial;
		Hub hub(version, masterSerial);
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
			assert(observer.names.size() == uid);
			assert(observer.names.back() == names[uid - 1]);
			assert(nodes[uid - 1]->getUid() == uid);
			// Ответ на информацию подтверждается до следующего запроса
			assert(readHeader(masterSerial).messageType == RS::MessageType::Ack);
			nodes[uid - 1]->update(masterSerial.data.data(), masterSerial.data.size());
			masterSerial.data.clear();
		}
		assert(!hub.isScanning());
		assert(hub.sendCmdToDevice("node32", 1, 0));
	}
}

void testUnexpectedResponses()
{
	ScanTime::now = std::chrono::milliseconds{0};
	RS::DeviceVersion version{};
	ScanSerial masterSerial;
	ScanSerial nodeSerial;
	Hub hub(version, masterSerial);
	ScanObserver observer;
	hub.registerObserver(&observer);
	hub.probeAll();
	hub.process(ScanTime::milliseconds());
	RS::Header request = readHeader(masterSerial);
	masterSerial.data.clear();

	// Информация с другого адреса не регистрирует устройство
	Node other("other", version, 2, nodeSerial);
	Hub sender(version, masterSerial);
	sender.sendDeviceInfoRequest(2);
	other.update(masterSerial.data.data(), masterSerial.data.size());
	masterSerial.data.clear();
	hub.update(nodeSerial.data.data(), nodeSerial.data.size());
	nodeSerial.data.clear();
	assert(observer.names.empty());

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

	Node node("node1", version, 1, nodeSerial);
	RS::DeviceInfoReqMessage wrong{};
	wrong.receiverUID = 1;
	wrong.transmitUID = 0;
	wrong.messageType = RS::MessageType::DeviceInfoReq;
	wrong.number = static_cast<uint8_t>(request.number + 1);
	length = parser.create(buffer, &wrong, sizeof(wrong));
	node.update(buffer, length);
	hub.update(nodeSerial.data.data(), nodeSerial.data.size());
	nodeSerial.data.clear();
	assert(observer.names.empty());
	masterSerial.data.clear();
	hub.process(ScanTime::milliseconds());
	assert(masterSerial.data.empty());
	assert(!hub.sendCmdToDevice("node1", 1, 0));
}

int main()
{
	testEmptyScan();
	testAllocatedScan();
	testUnexpectedResponses();
	return 0;
}
