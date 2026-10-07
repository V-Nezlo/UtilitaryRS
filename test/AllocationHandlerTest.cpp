#include <UtilitaryRS/RsHandler.hpp>
#include <UtilitaryRS/MultiNode.hpp>
#include <UtilitaryRS/Crc8.hpp>

#include <cassert>
#include <vector>

class AllocationSerial {
public:
	void write(const uint8_t *aData, size_t aLength)
	{
		data.insert(data.end(), aData, aData + aLength);
	}

	std::vector<uint8_t> data;
};

class AllocationMaster : public RS::RsHandler<AllocationSerial, Crc8, 100> {
	using Base = RS::RsHandler<AllocationSerial, Crc8, 100>;
public:
	using Base::Base;

	unsigned discoverAnswers{0};
	unsigned assignAnswers{0};
	uint8_t number{0};
	uint32_t roundNonce{0};
	RS::NodeUid uid{};
	uint32_t transactionId{0};
	uint8_t nodeId{0};

protected:
	void handleDiscoverAnswer(uint8_t aMessageNumber,
		uint32_t aRoundNonce, const RS::NodeUid &aUID) override
	{
		++discoverAnswers;
		number = aMessageNumber;
		roundNonce = aRoundNonce;
		uid = aUID;
	}

	void handleAssignAddressAnswer(uint8_t aMessageNumber,
		const RS::NodeUid &aUID, uint32_t aTransactionId, uint8_t aNodeId) override
	{
		++assignAnswers;
		number = aMessageNumber;
		uid = aUID;
		transactionId = aTransactionId;
		nodeId = aNodeId;
	}
};

using Node = RS::RsHandler<AllocationSerial, Crc8, 100>;

void exchange(AllocationMaster &aMaster, Node &aNode, AllocationSerial &aMasterSerial, AllocationSerial &aNodeSerial)
{
	aNode.update(aMasterSerial.data.data(), aMasterSerial.data.size());
	aMasterSerial.data.clear();
	aMaster.update(aNodeSerial.data.data(), aNodeSerial.data.size());
	aNodeSerial.data.clear();
	assert(aMasterSerial.data.empty()); // Нет дополнительных ACK на сообщения аллокации
}

template<class T>
void deliver(Node &aHandler, const T &aMessage)
{
	RS::RsParser<100, Crc8> parser;
	uint8_t buffer[100];
	const size_t length = parser.create(buffer, &aMessage, sizeof(aMessage));
	aHandler.update(buffer, length);
}

int main()
{
	static_assert(sizeof(RS::NodeUid) == 16);
	static_assert(sizeof(RS::DiscoverAnwMessage) == 24);
	static_assert(sizeof(RS::AssignAddressReqMessage) == 25);
	static_assert(sizeof(RS::AssignAddressAnwMessage) == 25);

	RS::DeviceVersion version{};
	RS::NodeUid uid{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
	AllocationSerial masterSerial;
	AllocationSerial nodeSerial;
	AllocationMaster master("master", version, RS::NodeUid{0x80}, masterSerial, 0);
	Node node("node", version, uid, nodeSerial);
	assert(node.getUid() == RS::kUnallocatedUID);
	assert(node.getNodeUid() == uid);
	assert(master.getNodeUid() == RS::NodeUid{0x80});

	// UID копируется, его время жизни не зависит от переданного массива
	RS::NodeUid original = uid;
	uid[0] = 0;
	assert(node.getNodeUid() == original);
	uid = original;

	// На полный проход групп нода отвечает ровно один раз без переопределения обработчиков
	for (uint8_t bucket = 0; bucket < RS::kAllocationBucketCount; ++bucket) {
		const uint8_t number = master.sendDiscoverRequest(0x12345678, bucket);
		assert(number != 0);
		exchange(master, node, masterSerial, nodeSerial);
		if (bucket == RS::Helpers::getAllocationBucket(uid, 0x12345678)) {
			assert(master.number == number);
		}
	}
	assert(master.discoverAnswers == 1 && master.uid == uid);
	assert(master.roundNonce == 0x12345678);

	// Обычные broadcast-запросы не вызывают ответ неаллоцированной ноды
	master.sendProbe(RS::kReservedUID);
	exchange(master, node, masterSerial, nodeSerial);
	assert(master.discoverAnswers == 1);

	RS::NodeUid otherUid = uid;
	otherUid[0] ^= 0x20;
	master.sendAssignAddressRequest(otherUid, 1, 32);
	exchange(master, node, masterSerial, nodeSerial);
	assert(node.getUid() == RS::kUnallocatedUID && master.assignAnswers == 0);

	const uint8_t assignNumber = master.sendAssignAddressRequest(uid, 0x87654321, 32);
	exchange(master, node, masterSerial, nodeSerial);
	assert(node.getUid() == 32 && master.assignAnswers == 1);
	assert(master.nodeId == 32 && master.number == assignNumber);
	assert(master.transactionId == 0x87654321 && master.uid == uid);

	// Адресованная нода молчит при обнаружении и подтверждает повтор назначения
	master.sendDiscoverRequest(1, RS::Helpers::getAllocationBucket(uid, 1));
	exchange(master, node, masterSerial, nodeSerial);
	assert(master.discoverAnswers == 1);
	const uint8_t retryNumber = master.sendAssignAddressRequest(uid, 0x87654321, 32);
	exchange(master, node, masterSerial, nodeSerial);
	assert(master.assignAnswers == 2 && master.number == retryNumber);

	// Другая транзакция или адрес не перенумеровывают уже аллоцированную ноду
	master.sendAssignAddressRequest(uid, 0x87654322, 32);
	exchange(master, node, masterSerial, nodeSerial);
	master.sendAssignAddressRequest(uid, 0x87654321, 31);
	exchange(master, node, masterSerial, nodeSerial);
	assert(master.assignAnswers == 2 && node.getUid() == 32);

	assert(master.sendDiscoverRequest(1, 32) == 0);
	assert(master.sendAssignAddressRequest(uid, 1, 0) == 0);
	assert(master.sendAssignAddressRequest(uid, 1, 33) == 0);
	assert(node.sendDiscoverRequest(1, 0) == 0);
	assert(masterSerial.data.empty() && nodeSerial.data.empty());

	RS::AssignAddressReqMessage wrongRequest{};
	wrongRequest.receiverUID = RS::kReservedUID;
	wrongRequest.transmitUID = 1;
	wrongRequest.messageType = RS::MessageType::AssignAddressReq;
	wrongRequest.payload.uid = uid;
	wrongRequest.payload.transactionId = 0x87654321;
	wrongRequest.payload.nodeId = 32;
	deliver(node, wrongRequest);
	wrongRequest.transmitUID = 0;
	wrongRequest.payload.nodeId = 33;
	deliver(node, wrongRequest);
	assert(nodeSerial.data.empty() && node.getUid() == 32);

	// Сохраненный адрес можно передать при создании ноды, повтор не меняет его
	Node restored("restored", version, uid, nodeSerial, 32);
	master.sendAssignAddressRequest(uid, 0x87654321, 32);
	exchange(master, restored, masterSerial, nodeSerial);
	assert(master.assignAnswers == 3 && restored.getUid() == 32);

	// Составное устройство фильтрует назначение по постоянному UID каждой ноды
	Node first("first", version, uid, nodeSerial);
	Node second("second", version, otherUid, nodeSerial);
	RS::MultiNode<100, Crc8, Node, Node> composite(std::tie(first, second));
	master.sendAssignAddressRequest(uid, 7, 1);
	composite.update(masterSerial.data.data(), masterSerial.data.size());
	masterSerial.data.clear();
	master.update(nodeSerial.data.data(), nodeSerial.data.size());
	nodeSerial.data.clear();
	assert(first.getUid() == 1 && second.getUid() == RS::kUnallocatedUID);
	assert(master.assignAnswers == 4 && master.uid == uid);

	// Изменение nonce разделяет UID, отличающиеся старшими битами байта
	bool separated = false;
	for (uint32_t nonce = 0; nonce < 32; ++nonce) {
		separated |= RS::Helpers::getAllocationBucket(uid, nonce) != RS::Helpers::getAllocationBucket(otherUid, nonce);
	}
	assert(separated);

	for (unsigned i = 0; i < 300; ++i) {
		assert(master.sendDiscoverRequest(i, 0) != 0);
		masterSerial.data.clear();
	}
	// После сброса мастера нода сохраняет UID и адрес, но принимает новую транзакцию
	master.sendDeviceInfoRequest(32, true);
	node.update(masterSerial.data.data(), masterSerial.data.size());
	masterSerial.data.clear();
	master.update(nodeSerial.data.data(), nodeSerial.data.size());
	nodeSerial.data.clear();
	masterSerial.data.clear(); // ACK на ответ с информацией
	assert(node.getUid() == 32 && node.getNodeUid() == uid);
	master.sendAssignAddressRequest(uid, 100, 32);
	exchange(master, node, masterSerial, nodeSerial);
	assert(master.assignAnswers == 5 && master.transactionId == 100);
	return 0;
}
