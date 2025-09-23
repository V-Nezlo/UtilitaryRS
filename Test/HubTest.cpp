
#include "Crc64.hpp"
#include "Crc8.hpp"
#include "Mocks/MockTime.hpp"
#include "RsTypes.hpp"
#include <DeviceHub.hpp>
#include <assert.h>
#include <chrono>
#include <iostream>
#include <vector>

// NOLINTBEGIN
class MockSerial {
public:
	void write(const uint8_t *data, size_t len)
	{
		buf.insert(buf.end(), data, data + len);
	}

	// Получить и очистить накопленные байты (симуляция "канала" — master -> device и обратно)
	std::vector<uint8_t> readAll()
	{
		std::vector<uint8_t> out = std::move(buf);
		buf.clear();
		return out;
	}

	// Получить без очистки (не используем здесь)
	const std::vector<uint8_t> &data() const
	{
		return buf;
	}

	void clear()
	{
		buf.clear();
	}

private:
	std::vector<uint8_t> buf;
};

template<typename Interface, typename Crc, size_t ParserSize>
class DeviceNode : public RS::RsHandler<Interface, Crc, ParserSize> {
public:
	DeviceNode(const char *name, RS::DeviceVersion &ver, uint8_t uid, Interface &iface) :
		RS::RsHandler<Interface, Crc, ParserSize>(name, ver, uid, iface)
	{ }

	// Обрабатывать BlobRequest: если request == 2 и ожидается 4 байта, отправляем 0xAABBCCDD
	RS::Result processBlobRequest(
		uint8_t aTransmitUID, uint8_t aMessageNumber, uint8_t aRequest, uint8_t aRequestedDataSize) override
	{
		(void)aTransmitUID;
		if (aRequest == 2 && aRequestedDataSize == 4) {
			uint32_t payload = 0xAABBCCDDu;
			// sendAnswer( receiver, msgNumber, request, requestedSize, data, size )
			const bool ok = this->sendAnswer(aTransmitUID, aMessageNumber, aRequest, aRequestedDataSize,
				reinterpret_cast<void *>(&payload), static_cast<uint8_t>(sizeof(payload)));
			return ok ? RS::Result::Ok : RS::Result::Error;
		}
		return RS::Result::Unsupported;
	}
	// Мы хотим: при получении команды 0x06,0x07 -> вернуть Ok (и пометить флаг)
	RS::Result handleCommand(uint8_t aCommand, uint8_t aArgument) override
	{
		if (aCommand == 0x06 && aArgument == 0x07) {
			commandReceived = true;
			return RS::Result::Ok;
		} else {
			return RS::Result::InvalidArg;
		}
	}

	RS::Result handleFileWriteRequest(uint8_t /*uid*/, uint8_t aFile, uint32_t aFileSize) override
	{
		if (aFile == 0) {
			recvBuf.clear();
			recvBuf.reserve(aFileSize);
			expectedSize = aFileSize;
			return RS::Result::Ok;
		} else {
			return RS::Result::Error;
		}
	}

	RS::Result handleWriteChunk(uint8_t /*uid*/, uint8_t /*filenum*/, const void *data, size_t len) override
	{
		const uint8_t *p = reinterpret_cast<const uint8_t *>(data);
		recvBuf.insert(recvBuf.end(), p, p + len);
		return RS::Result::Ok;
	}

	RS::Result handleWriteChunkFinalize(uint8_t /*uid*/, uint8_t /*msgNum*/, uint16_t /*chunks*/, uint64_t crc) override
	{
		uint64_t calc = Crc64::calculate(recvBuf.data(), recvBuf.size());
		if (calc == crc && recvBuf.size() == expectedSize) {
			// Сравним полученный файл с переданным

			// Переданный файл
			uint8_t buffer[128];
			for (auto i = 0; i < 128; ++i) { buffer[i] = i; }

			if (!memcmp(recvBuf.data(), buffer, sizeof(buffer))) {
				fileOk = true;
				return RS::Result::Ok;
			} else {
				return RS::Result::ChecksumFailed;
			}
		}
		return RS::Result::Error;
	}

	bool wasCommandReceived() const
	{
		return commandReceived;
	}

	bool isFileOk() const
	{
		return fileOk;
	}

private:
	bool commandReceived{false};
	bool fileOk{false};

	std::vector<uint8_t> recvBuf;
	size_t expectedSize;
};

class DeviceHubObserverMock : public RS::DeviceHubObserver {
public:
	void onAckNotReceivedEv(const std::string &aName, RS::MessageType aMessage) override
	{
		(void)aMessage;
		lastNotAckName = aName;
	}
	void onAckReceivedEv(const std::string &aName, RS::MessageType aMessage, RS::Result aCode) override
	{
		(void)aMessage;
		lastAckName = aName;
		lastAckCode = aCode;
	}
	void onCommandResultEv(const std::string &aName, RS::Result aReturn) override
	{
		lastCommandName = aName;
		lastCommandResult = aReturn;
	}
	void onRequestErrorEv(const std::string &aName, RS::Result aReturn) override
	{
		lastRequestErrorName = aName;
		lastRequestError = aReturn;
	}
	RS::Result blobAnswerEvReceived(const std::string &aName, uint8_t Request, const void *aData, size_t aSize) override
	{
		(void)Request;
		(void)aData;
		(void)aSize;

		if (aSize == sizeof(uint32_t)) {
			uint32_t answer;
			memcpy(&answer, aData, aSize);

			if (answer == 0xAABBCCDDu) {
				anwerCorrected = true;
			}
		}

		lastBlobName = aName;
		return RS::Result::Ok;
	}
	void deviceInfoReceivedEv(const std::string &aName, RS::DeviceVersion aVersion) override
	{
		lastDeviceRegistered = aName;
		deviceVersion = aVersion;
	}
	RS::Result fileWriteResultEv(const std::string &aName, RS::Result aReturn) override
	{
		lastFileName = aName;
		lastFileResult = aReturn;
		return aReturn;
	}

	// тестовые поля
	std::string lastNotAckName;
	std::string lastAckName;
	RS::Result lastAckCode{RS::Result::Error};

	std::string lastDeviceRegistered;
	RS::DeviceVersion deviceVersion{};

	std::string lastCommandName;
	RS::Result lastCommandResult{RS::Result::Error};

	std::string lastRequestErrorName;
	RS::Result lastRequestError{RS::Result::Error};

	std::string lastBlobName;

	std::string lastFileName;
	RS::Result lastFileResult{RS::Result::Error};

	bool anwerCorrected;
};

int main()
{
	using Hub = RS::DeviceHub<2, MockSerial, MockTime, Crc8, Crc64, 256>;
	using Device = DeviceNode<MockSerial, Crc8, 256>;

	// Версии
	RS::DeviceVersion hubVer{};
	hubVer.hwRevision = 1;
	hubVer.swMajor = 0;
	hubVer.swMinor = 1;
	hubVer.swRevision = 0x1234;
	hubVer.hash = 0x1111;

	RS::DeviceVersion devVer{};
	devVer.hwRevision = 2;
	devVer.swMajor = 1;
	devVer.swMinor = 5;
	devVer.swRevision = 0x80;
	devVer.hash = 0xAABBCCDD;

	MockSerial masterSerial; // интерфейс для хаба (он пишет сюда)
	MockSerial deviceSerial; // интерфейс для девайса (он пишет сюда)

	std::string deviceName = "dev1";

	// Создаем hub и device
	Hub hub(hubVer, masterSerial);
	Device device(deviceName.c_str(), devVer, 1, deviceSerial);

	DeviceHubObserverMock obs;
	hub.registerObserver(&obs);
	// Начнем опрос устройств
	hub.probeAll();
	// Передадим данные из мастера в устройство
	auto m2d = masterSerial.readAll();
	assert(!m2d.empty()); // мы должны получить probe
	device.update(m2d.data(), m2d.size());
	// Девайс должен отправить ack
	auto d2m = deviceSerial.readAll();
	assert(!d2m.empty());
	hub.update(d2m.data(), d2m.size());

	// После получения ACK hub должен создать запись (в handleAck он делает hub[uid] = DeviceWrapper{})
	// и на следующем process отправит DeviceInfoReq. Вызовем process с текущим временем.
	hub.process(MockTime::milliseconds());

	// hub написал DeviceInfoReq в masterSerial
	m2d = masterSerial.readAll();
	assert(!m2d.empty());
	device.update(m2d.data(), m2d.size());

	// device ответил DeviceInfoAnw (в процессе processDeviceInfoRequest)
	d2m = deviceSerial.readAll();
	assert(!d2m.empty());
	hub.update(d2m.data(), d2m.size());

	// После обработки DeviceInfoAnw observer должен получить уведомление и заполнить lastDeviceRegistered
	MockTime::delay(std::chrono::milliseconds{1000});
	hub.process(MockTime::milliseconds());

	// Проверки
	if (obs.lastDeviceRegistered != deviceName) {
		std::cerr << "Device was not registered: expected 'dev1', got '" << obs.lastDeviceRegistered << "'\n";
		return 1;
	}

	std::cout << "Device registered: " << obs.lastDeviceRegistered << "\n";

	// Тестируем интерфейсы Hub
	// === 1) Отправка команды (hub -> device) и ACK ===
	bool r = hub.sendCmdToDevice(deviceName, 0x06, 0x07);
	assert(r && "sendCmdToDevice returned false");

	// Дадим время, чтобы process отправил команду
	MockTime::delay(std::chrono::milliseconds{50});
	hub.process(MockTime::milliseconds());

	// master -> device (команда)
	m2d = masterSerial.readAll();
	assert(!m2d.empty());
	device.update(m2d.data(), m2d.size());

	// device обработал команду и отправил ACK
	d2m = deviceSerial.readAll();
	assert(!d2m.empty());
	hub.update(d2m.data(), d2m.size());

	// обработано — observer должен получить onCommandResultEv
	if (obs.lastCommandName != deviceName || obs.lastCommandResult != RS::Result::Ok) {
		std::cerr << "Command path failed: lastCommandName='" << obs.lastCommandName
				  << "', result=" << static_cast<int>(obs.lastCommandResult) << "\n";
		return 2;
	}
	std::cout << "Command OK\n";

	// Дополнительно убедимся, что self device получил команду (в DeviceNode мы помечаем флаг)
	if (!device.wasCommandReceived()) {
		std::cerr << "DeviceNode didn't register receiving command\n";
		return 3;
	}

	// === 2) Blob request (hub -> device -> blob answer -> hub) ===
	// Hub запросит 4 байта по request id = 2
	bool sendBlob = hub.sendBlobRequestToDevice(deviceName, 2 /* req */, 4 /* size */);
	assert(sendBlob && "sendBlobRequestToDevice returned false");

	// Позволим hub'у отправить запрос
	MockTime::delay(std::chrono::milliseconds{50});
	hub.process(MockTime::milliseconds());

	// master -> device (BlobRequest)
	m2d = masterSerial.readAll();
	assert(!m2d.empty());
	device.update(m2d.data(), m2d.size());

	// device должен ответить BlobAnswer (sendAnswer внутри processBlobRequest)
	d2m = deviceSerial.readAll();
	assert(!d2m.empty());
	hub.update(d2m.data(), d2m.size());

	// После обработки BlobAnswer observer должен видеть имя устройства
	if (obs.lastBlobName != deviceName) {
		std::cerr << "Blob handling failed: expected blob from '" << deviceName << "', got '" << obs.lastBlobName
				  << "'\n";
		return 4;
	}

	if (obs.anwerCorrected) {
		std::cout << "Blob OK\n";
	} else {
		std::cerr << "Blob handling failed\n";
	}

	// Теперь попробуем передать файл
	// Просто файл с заранее известным содержимым
	uint8_t buffer[128];
	for (auto i = 0; i < 128; ++i) { buffer[i] = i; }
	// Инициируем отправку файла 0
	hub.sendFile(deviceName, 0, buffer, sizeof(buffer), 16);
	MockTime::delay(std::chrono::milliseconds{1000});
	hub.process(MockTime::milliseconds());
	// Прилетает реквест, должен вернуть OK
	MockTime::delay(std::chrono::milliseconds{50});
	m2d = masterSerial.readAll();
	assert(!m2d.empty());
	device.update(m2d.data(), m2d.size());

	// Начинаем запись файла
	for (int i = 0; i < 8; ++i) {
		d2m = deviceSerial.readAll();
		assert(!d2m.empty());
		hub.update(d2m.data(), d2m.size());

		MockTime::delay(std::chrono::milliseconds{100});
		hub.process(MockTime::milliseconds());
		MockTime::delay(std::chrono::milliseconds{100});

		m2d = masterSerial.readAll();
		assert(!m2d.empty());
		device.update(m2d.data(), m2d.size());
		MockTime::delay(std::chrono::milliseconds{60});
	}

	d2m = deviceSerial.readAll();
	assert(!d2m.empty());
	hub.update(d2m.data(), d2m.size());
	// Тут мы отметим факт отправки всех чанков и перейдем в finalize
	MockTime::delay(std::chrono::milliseconds{100});
	hub.process(MockTime::milliseconds());
	MockTime::delay(std::chrono::milliseconds{1010});
	// А тут произойдет отправка Finalize пакета
	hub.process(MockTime::milliseconds());
	// Передадим пакет девайсу
	MockTime::delay(std::chrono::milliseconds{100});
	m2d = masterSerial.readAll();
	assert(!m2d.empty());
	device.update(m2d.data(), m2d.size());
	// Девайс должен посчитать CRC и вернуть статус получения отправленного файла
	MockTime::delay(std::chrono::milliseconds{100});
	d2m = deviceSerial.readAll();
	assert(!d2m.empty());
	hub.update(d2m.data(), d2m.size());

	assert(obs.anwerCorrected && device.isFileOk());
	std::cout << "File Transfer OK\n";
	std::cout << "ALL TESTS PASSED\n";

	return 0;
}
