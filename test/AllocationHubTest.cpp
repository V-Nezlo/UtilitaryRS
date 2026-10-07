#include <UtilitaryRS/DeviceHub.hpp>
#include <UtilitaryRS/Crc8.hpp>
#include <UtilitaryRS/Crc64.hpp>

#include <cassert>
#include <memory>
#include <vector>

class AllocationTime {
public:
	static std::chrono::milliseconds milliseconds() { return now; }
	inline static std::chrono::milliseconds now{0};
};

class HubSerial {
public:
	void write(const uint8_t *aData, size_t aLength)
	{
		data.insert(data.end(), aData, aData + aLength);
	}
	std::vector<uint8_t> data;
};

class AllocationObserver : public RS::DeviceHubObserver {
public:
	void onAckNotReceivedEv(const RS::NodeUid &, uint8_t, RS::MessageType) override { }
	void onAckReceivedEv(const RS::NodeUid &, uint8_t, RS::MessageType, RS::Result) override { }
	void onCommandResultEv(const RS::NodeUid &, uint8_t, RS::Result) override { }
	void onRequestErrorEv(const RS::NodeUid &, uint8_t, RS::Result) override { }
	RS::Result blobAnswerEvReceived(const RS::NodeUid &, uint8_t, uint8_t, const void *, size_t) override { return RS::Result::Ok; }
	void deviceLostEv(const RS::NodeUid &, uint8_t) override { }
	RS::Result fileWriteResultEv(const RS::NodeUid &, uint8_t, RS::Result aReturn) override { return aReturn; }
	void deviceHealthReceivedEv(const RS::NodeUid &, uint8_t, RS::Health, uint16_t) override { }
	void deviceRegisteredEv(const RS::NodeUid &aUID, uint8_t aDeviceId, RS::DeviceVersion) override { registered.emplace_back(aUID, aDeviceId); }

	std::vector<std::pair<RS::NodeUid, uint8_t>> registered;
};

using Hub = RS::DeviceHub<HubSerial, AllocationTime, Crc8, Crc64, 100>;
class Node : public RS::RsHandler<HubSerial, Crc8, 100> {
	using Base = RS::RsHandler<HubSerial, Crc8, 100>;
public:
	using Base::Base;
	void handleExchangeReset() override
	{
		++resets;
		fileActive = false;
	}
	unsigned resets{0};
	bool fileActive{false};
};
using Parser = RS::RsParser<100, Crc8>;

RS::NodeUid makeUid(uint8_t aValue)
{
	RS::NodeUid uid{};
	uid[0] = aValue;
	return uid;
}

struct Bus {
	HubSerial masterSerial;
	HubSerial nodeSerial;
	RS::DeviceVersion version{resetTime()};
	AllocationObserver observer;
	Hub hub{version, RS::NodeUid{0x80}, masterSerial};
	Hub *activeHub{&hub};
	std::vector<std::unique_ptr<Node>> nodes;
	unsigned assignments{0};
	unsigned droppedConfirmations{0};
	unsigned collisions{0};
	unsigned discoveryQueries{0};
	unsigned commands{0};
	std::vector<std::pair<RS::MessageType, uint8_t>> registrationRequests;
	bool simulateCollisions{false};
	bool injectInvalidAnswers{false};
	unsigned confirmationsToDrop{0};

	static RS::DeviceVersion resetTime()
	{
		AllocationTime::now = std::chrono::milliseconds{0};
		return {};
	}

	Bus()
	{
		hub.registerObserver(&observer);
	}

	void addNode(const char *aName, const RS::NodeUid &aUID, uint8_t aAddress = RS::kUnallocatedUID)
	{
		nodes.push_back(std::make_unique<Node>(aName, version, aUID, nodeSerial, aAddress));
	}

	void exchange()
	{
		if (masterSerial.data.empty()) {
			return;
		}
		Parser parser;
		assert(parser.update(masterSerial.data.data(), masterSerial.data.size()) == masterSerial.data.size());
		assert(parser.isReady()); // В каждый момент отправлен один запрос или один ACK
		RS::Header header;
		memcpy(&header, parser.data(), sizeof(header));
		if (header.messageType == RS::MessageType::Command) {
			++commands;
		}
		if (header.messageType == RS::MessageType::AssignAddressReq) {
			++assignments;
			const auto *request = reinterpret_cast<const RS::AssignAddressReqMessage *>(parser.data());
			registrationRequests.emplace_back(header.messageType, request->payload.nodeId);
			assert(activeHub->getNodeAllocations()[request->payload.nodeId - 1]);
			if (injectInvalidAnswers) {
				RS::AssignAddressReqMessage request;
				memcpy(&request, parser.data(), sizeof(request));
				RS::AssignAddressAnwMessage answer{};
				answer.receiverUID = 0;
				answer.transmitUID = request.payload.nodeId;
				answer.messageType = RS::MessageType::AssignAddressAnw;
				answer.number = request.number;
				answer.payload.uid = request.payload.uid;
				answer.payload.uid[0] ^= 0xFF;
				answer.payload.transactionId = request.payload.transactionId;
				answer.payload.nodeId = request.payload.nodeId;
				inject(answer);
				answer.payload.uid = request.payload.uid;
				++answer.payload.transactionId;
				inject(answer);
				// Ложное подтверждение не должно разрешить регистрацию через ответ с информацией
				RS::DeviceInfoAnwMessage info{};
				info.receiverUID = 0;
				info.transmitUID = request.payload.nodeId;
				info.messageType = RS::MessageType::DeviceInfoAnw;
				info.number = static_cast<uint8_t>(request.number + 1);
				info.payload.nameLen = 1;
				uint8_t payload[sizeof(info) + 1];
				memcpy(payload, &info, sizeof(info));
				payload[sizeof(info)] = 'x';
				uint8_t buffer[100];
				const size_t length = parser.create(buffer, payload, sizeof(payload));
				const size_t registered = observer.registered.size();
				activeHub->update(buffer, length);
				assert(observer.registered.size() == registered);
				// ACK на проигнорированный DeviceInfoAnw не относится к запросу автомата
				masterSerial.data.resize(sizeof(request) + 2);
			}
		}
		if (header.messageType == RS::MessageType::DiscoverReq) {
			++discoveryQueries;
			if (injectInvalidAnswers) {
				RS::DiscoverReqMessage request;
				memcpy(&request, parser.data(), sizeof(request));
				RS::DiscoverAnwMessage answer{};
				answer.receiverUID = 0;
				answer.transmitUID = RS::kUnallocatedUID;
				answer.messageType = RS::MessageType::DiscoverAnw;
				answer.number = request.number;
				answer.payload.roundNonce = request.payload.roundNonce + 1;
				answer.payload.uid = makeUid(100);
				inject(answer);
				answer.payload.roundNonce = request.payload.roundNonce;
				answer.number = static_cast<uint8_t>(request.number + 1);
				inject(answer);
			}
		}
		if (header.messageType == RS::MessageType::DeviceInfoReq && assignments != 0) {
			registrationRequests.emplace_back(header.messageType, header.receiverUID);
		}
		unsigned responses = 0;
		for (auto &node : nodes) {
			const size_t before = nodeSerial.data.size();
			node->update(masterSerial.data.data(), masterSerial.data.size());
			responses += nodeSerial.data.size() != before;
		}
		masterSerial.data.clear();
		if (simulateCollisions && header.messageType == RS::MessageType::DiscoverReq && responses > 1) {
			nodeSerial.data.clear();
			++collisions;
		} else if (header.messageType == RS::MessageType::AssignAddressReq && confirmationsToDrop != 0) {
			nodeSerial.data.clear();
			--confirmationsToDrop;
			++droppedConfirmations;
		} else {
			activeHub->update(nodeSerial.data.data(), nodeSerial.data.size());
			nodeSerial.data.clear();
		}
		// Доставим ACK на DeviceInfoAnw до следующего шага автомата
		if (!masterSerial.data.empty()) {
			exchange();
		}
	}

	template<class T>
	void inject(const T &aMessage)
	{
		Parser parser;
		uint8_t buffer[100];
		const size_t length = parser.create(buffer, &aMessage, sizeof(aMessage));
		activeHub->update(buffer, length);
	}

	void step()
	{
		activeHub->process(AllocationTime::milliseconds());
		exchange();
		AllocationTime::now += std::chrono::milliseconds{200};
	}

	void run()
	{
		unsigned steps = 0;
		while (activeHub->state() != Hub::State::Running) {
			assert(++steps < 1000);
			step();
		}
	}
};

void testAllocationAndRestart()
{
	Bus bus;
	bus.addNode("allocated", makeUid(1), 32);
	bus.addNode("new1", makeUid(2));
	bus.addNode("new2", makeUid(3));
	bus.confirmationsToDrop = 1;
	bus.injectInvalidAnswers = true;
	assert(bus.hub.allocateNodes(1));
	assert(!bus.hub.allocateNodes(1));
	bus.run();
	assert(bus.nodes[0]->getUid() == 32);
	assert(bus.nodes[1]->getUid() != bus.nodes[2]->getUid());
	assert(bus.observer.registered.size() == 3);
	assert(bus.assignments == 3 && bus.droppedConfirmations == 1);
	const uint8_t firstAddress = bus.nodes[1]->getUid();
	const uint8_t secondAddress = bus.nodes[2]->getUid();
	bus.nodes[0]->fileActive = true;

	// Новый мастер не получает никаких сохраненных данных
	Hub restarted(bus.version, RS::NodeUid{0x80}, bus.masterSerial);
	bus.activeHub = &restarted;
	restarted.registerObserver(&bus.observer);
	bus.observer.registered.clear();
	bus.run();
	assert(!bus.nodes[0]->fileActive && bus.nodes[0]->resets == 2);
	assert(bus.observer.registered.size() == 3 && bus.assignments == 3);
	assert(bus.nodes[0]->getUid() == 32);
	assert(bus.nodes[1]->getUid() == firstAddress && bus.nodes[2]->getUid() == secondAddress);
	assert(restarted.getNodeAllocations()[firstAddress - 1].value().uid == makeUid(2));

	// Последующий перезапуск слейва восстанавливает адрес из таблицы, собранной сканированием
	bus.nodes[1] = std::make_unique<Node>("new1", bus.version, makeUid(2), bus.nodeSerial);
	bus.step();
	bus.run();
	assert(bus.nodes[1]->getUid() == firstAddress);
	assert(bus.observer.registered.size() == 4);
}

void testMasterRestartDuringAssignment()
{
	Bus bus;
	bus.addNode("allocated", makeUid(1), 32);
	bus.addNode("first", makeUid(2));
	bus.addNode("second", makeUid(3));
	bus.confirmationsToDrop = 3;
	assert(bus.hub.allocateNodes(1));
	while (bus.assignments == 0) {
		bus.step();
	}
	assert(bus.hub.state() != Hub::State::Running);
	const uint8_t firstAddress = bus.nodes[1]->getUid();
	const uint8_t secondAddress = bus.nodes[2]->getUid();
	assert((firstAddress == RS::kUnallocatedUID) != (secondAddress == RS::kUnallocatedUID));

	Hub restarted(bus.version, RS::NodeUid{0x80}, bus.masterSerial);
	bus.activeHub = &restarted;
	restarted.registerObserver(&bus.observer);
	bus.observer.registered.clear();
	bus.confirmationsToDrop = 0;
	bus.step();
	bus.run();
	assert(bus.observer.registered.size() == 3);
	assert(bus.nodes[0]->getUid() == 32);
	assert(bus.nodes[1]->getUid() != bus.nodes[2]->getUid());
	assert(firstAddress == RS::kUnallocatedUID || bus.nodes[1]->getUid() == firstAddress);
	assert(secondAddress == RS::kUnallocatedUID || bus.nodes[2]->getUid() == secondAddress);

}

void testLostConfirmations()
{
	Bus bus;
	bus.addNode("silent", makeUid(1));
	bus.confirmationsToDrop = 3;
	assert(bus.hub.allocateNodes(1));
	bus.run();
	assert(bus.nodes[0]->getUid() == 1 && bus.observer.registered.empty());
	assert(bus.assignments == 3 && bus.hub.getNodeAllocations()[0]);
	Hub restarted(bus.version, RS::NodeUid{0x80}, bus.masterSerial);
	bus.activeHub = &restarted;
	restarted.registerObserver(&bus.observer);
	bus.step();
	bus.run();
	assert(bus.assignments == 3 && bus.observer.registered.size() == 1);
	assert(restarted.getNodeAllocations()[0].value().uid == makeUid(1));
}

void testCollisionAndCapacity()
{
	RS::NodeUid first = makeUid(1);
	RS::NodeUid second{};
	bool found = false;
	for (unsigned candidate = 2; candidate < 256; ++candidate) {
		second = makeUid(static_cast<uint8_t>(candidate));
		if (RS::Helpers::getAllocationBucket(first, 1) == RS::Helpers::getAllocationBucket(second, 1)
			&& RS::Helpers::getAllocationBucket(first, 2) != RS::Helpers::getAllocationBucket(second, 2)) {
			found = true;
			break;
		}
	}
	assert(found);
	Bus collision;
	collision.simulateCollisions = true;
	collision.addNode("first", first);
	collision.addNode("second", second);
	assert(collision.hub.allocateNodes(2));
	collision.run();
	assert(collision.collisions == 1 && collision.observer.registered.size() == 2);
	assert(collision.nodes[0]->getUid() != collision.nodes[1]->getUid());

	Bus full;
	std::array<std::string, RS::kMaxNodeCount> names;
	for (uint8_t address = 1; address <= RS::kMaxNodeCount; ++address) {
		names[address - 1] = "node" + std::to_string(address);
		full.addNode(names[address - 1].c_str(), makeUid(address), address);
	}
	full.addNode("extra", makeUid(33));
	assert(full.hub.allocateNodes(1));
	full.run();
	assert(full.assignments == 0 && full.nodes.back()->getUid() == RS::kUnallocatedUID);
	assert(full.observer.registered.size() == RS::kMaxNodeCount);

	Bus empty;
	assert(!empty.hub.allocateNodes(0));
	assert(empty.hub.allocateNodes(2));
	empty.run();
	assert(empty.discoveryQueries == 64 && empty.assignments == 0);
}

void testPeriodicDiscovery()
{
	AllocationTime::now = std::chrono::milliseconds{0};
	Bus bus;
	// Сканирование существующих адресов завершается до первого фонового прохода
	bus.hub.probeAll();
	while (bus.hub.state() != Hub::State::Running) {
		bus.step();
	}
	AllocationTime::now = std::chrono::milliseconds{9999};
	bus.step();
	assert(bus.hub.state() == Hub::State::Running && bus.discoveryQueries == 0);
	AllocationTime::now = std::chrono::milliseconds{10000};
	bus.step();
	assert((bus.hub.state() != Hub::State::Running) && bus.hub.state() != Hub::State::Scanning);
	bus.run();
	assert(bus.discoveryQueries == 32 && bus.assignments == 0);

	// Новая нода обнаруживается без явного вызова allocateNodes()
	bus.addNode("new", makeUid(1));
	AllocationTime::now = std::chrono::milliseconds{19999};
	bus.step();
	assert(bus.hub.state() == Hub::State::Running && bus.nodes[0]->getUid() == RS::kUnallocatedUID);
	AllocationTime::now = std::chrono::milliseconds{20000};
	bus.step();
	bus.run();
	assert(bus.nodes[0]->getUid() == 1 && bus.observer.registered.size() == 1);
	const RS::NodeAllocation saved = bus.hub.getNodeAllocations()[0].value();

	// Перезапуск слейва с тем же UID восстанавливает прежний адрес и транзакцию
	bus.nodes[0] = std::make_unique<Node>("new", bus.version, saved.uid, bus.nodeSerial);
	AllocationTime::now = std::chrono::milliseconds{29999};
	bus.step(); // Адресный запрос к прежнему адресу остается без ответа
	assert(bus.hub.state() == Hub::State::Running && bus.nodes[0]->getUid() == RS::kUnallocatedUID);
	AllocationTime::now = std::chrono::milliseconds{30000};
	bus.step();
	assert(bus.hub.state() != Hub::State::Running);
	bus.run();
	assert(bus.nodes[0]->getUid() == saved.nodeId && bus.observer.registered.size() == 2);
	assert(bus.hub.getNodeAllocations()[0].value().transactionId == saved.transactionId);

	// Аллоцированная нода не мешает подключению еще одного устройства
	bus.addNode("another", makeUid(2));
	AllocationTime::now = std::chrono::milliseconds{40000};
	bus.step();
	bus.run();
	assert(bus.nodes[0]->getUid() == 1 && bus.nodes[1]->getUid() == 2);
	assert(bus.observer.registered.size() == 3);
}

void testAutomaticStartup()
{
	AllocationTime::now = std::chrono::milliseconds{0};
	Bus bus;
	bus.addNode("existing", makeUid(1), 32);
	bus.addNode("new", makeUid(2));
	bus.step();
	assert(bus.hub.state() == Hub::State::Scanning);
	bus.run();
	assert(bus.nodes[0]->getUid() == 32 && bus.nodes[1]->getUid() == 1);
	assert(bus.observer.registered.size() == 2);
}

void testAllocationPausesScheduling()
{
	Bus bus;
	bus.addNode("existing", makeUid(1), 1);
	assert(bus.hub.probeAll());
	bus.run();
	assert(bus.hub.sendCmdToDevice(1, 1, 2));
	// Уже отправленный запрос завершается в Waiting; следующий остается в очереди
	bus.hub.process(AllocationTime::milliseconds());
	assert(bus.hub.sendCmdToDevice(1, 3, 4));
	assert(bus.hub.probeAll());
	assert(bus.hub.state() == Hub::State::Waiting);
	bus.hub.process(AllocationTime::milliseconds());
	assert(bus.hub.state() == Hub::State::Waiting);
	bus.exchange();
	assert(bus.commands == 1);
	bus.run();
	assert(bus.commands == 1);
	bus.step();
	assert(bus.commands == 2);
	assert(bus.hub.sendCmdToDevice(1, 5, 6));
	AllocationTime::now = std::chrono::milliseconds{20000};
	bus.step();
	assert(bus.hub.state() == Hub::State::Allocating);
	bus.run();
	assert(bus.commands == 2);
	bus.step();
	assert(bus.commands == 3);
}

void testDeviceStateScheduling()
{
	Bus bus;
	const RS::NodeUid first = makeUid(1);
	RS::NodeUid second{};
	for (unsigned candidate = 2; candidate < 256; ++candidate) {
		second = makeUid(static_cast<uint8_t>(candidate));
		if (RS::Helpers::getAllocationBucket(first, 1) == RS::Helpers::getAllocationBucket(second, 1)) {
			break;
		}
	}
	assert(RS::Helpers::getAllocationBucket(first, 1) == RS::Helpers::getAllocationBucket(second, 1));
	// Транспорт доставляет два ответа одной группы без потери
	bus.addNode("same", first);
	bus.addNode("same", second);
	assert(bus.hub.allocateNodes(1));
	bus.run();
	assert(bus.observer.registered.size() == 2);
	const std::vector<std::pair<RS::MessageType, uint8_t>> expected{
		{RS::MessageType::AssignAddressReq, 1}, {RS::MessageType::AssignAddressReq, 2},
		{RS::MessageType::DeviceInfoReq, 1}, {RS::MessageType::DeviceInfoReq, 2}
	};
	assert(bus.registrationRequests == expected);
	assert(bus.observer.registered[0].first == first && bus.observer.registered[0].second == 1);
	assert(bus.observer.registered[1].first == second && bus.observer.registered[1].second == 2);

	// Обычная работа также обслуживает по одному устройству, без перезаписи pending
	AllocationTime::now = std::chrono::milliseconds{20000};
	bus.step();
	bus.run();
	assert(bus.hub.sendCmdToDevice(bus.nodes[0]->getUid(), 1, 2));
	assert(bus.hub.sendCmdToDevice(bus.nodes[1]->getUid(), 3, 4));
	bus.hub.process(AllocationTime::milliseconds());
	Parser parser;
	assert(parser.update(bus.masterSerial.data.data(), bus.masterSerial.data.size()) == bus.masterSerial.data.size());
	assert(parser.isReady());
	const auto *request = reinterpret_cast<const RS::Header *>(parser.data());
	assert(request->messageType == RS::MessageType::Command);
	const uint8_t firstAddress = request->receiverUID;
	const size_t length = bus.masterSerial.data.size();
	bus.hub.process(AllocationTime::milliseconds());
	assert(bus.masterSerial.data.size() == length);
	bus.exchange();
	AllocationTime::now += std::chrono::milliseconds{100};
	bus.hub.process(AllocationTime::milliseconds());
	parser.reset();
	assert(parser.update(bus.masterSerial.data.data(), bus.masterSerial.data.size()) == bus.masterSerial.data.size());
	assert(parser.isReady());
	request = reinterpret_cast<const RS::Header *>(parser.data());
	assert(request->messageType == RS::MessageType::Command && request->receiverUID != firstAddress);
	bus.exchange();
	assert(bus.commands == 2);
}

int main()
{
	testAllocationAndRestart();
	testLostConfirmations();
	testMasterRestartDuringAssignment();
	testCollisionAndCapacity();
	testPeriodicDiscovery();
	testAutomaticStartup();
	testAllocationPausesScheduling();
	testDeviceStateScheduling();
	return 0;
}
