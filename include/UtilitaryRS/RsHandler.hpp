/*!
\file
\brief Класс, описывающий поведение протокола RsHandler
\author V-Nezlo (vlladimirka@gmail.com)
\date 23.09.2025
\version 2.0

Данный класс обеспечивает всё взаимодействия внутри протокола
*/

#ifndef LIB_RSHANDLER_HPP
#define LIB_RSHANDLER_HPP

#include "RsParser.hpp"
#include "RsTypes.hpp"

namespace RS {

template<class Interface, typename Crc, size_t ParserSize>
class RsHandler {
	using Parser = RsParser<ParserSize, Crc>;

public:

	/// \brief Конструктор класса RsHandler
	/// \param aName имя устройства
	/// \param aVersion версия устройства
	/// \param aNodeUID номер данной ноды (от 0 до 255)
	/// \param aInterface ссылка на экземпляр типа Interface, заданного шаблоном
	RsHandler(const char *aName, DeviceVersion &aVersion, uint8_t aNodeUID, Interface &aInterface) :
		name{aName},
		version{aVersion},
		health{Health::WarnUp},
		flags{0},
		nodeUID{aNodeUID},
		parser{},
		interface{aInterface},
		messageBuffer{}
	{ }

	/// \brief Основная функция, прокидывающая получаемые байты в парсер и отправляющие в протокольный обработчик
	/// \param aData - указатель на валидные данные
	/// \param aLength - размер валидных данных
	void update(const uint8_t *aData, size_t aLength)
	{
		size_t left = aLength;

		while (left) {
			size_t parsed = parser.update(static_cast<const uint8_t *>(aData) + (aLength - left), left);

			if (parsed == 0) {
				parser.reset();
				break;
			}

			left -= parsed;
			if (parser.state() == Parser::State::Done) {
				process(parser.data(), parser.length());
				parser.reset();
			}
		}
	}

	/// \brief Функция для отправки команды от текущей ноды
	/// \param aReceiverUID - UID получателя команды
	/// \param aCommand - номер команды
	/// \param aArgument - аргумент для команды (может быть незадействован)
	/// \return номер сообщения
	uint8_t sendCommand(uint8_t aReceiverUID, uint8_t aCommand, uint8_t aArgument)
	{
		ComMessage message;
		message.messageType = MessageType::Command;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.number = ++messageNumber;
		message.payload.command = aCommand;
		message.payload.value = aArgument;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
		return message.number;
	}

	/// \brief Функция для отправки запроса на получение данных
	/// \param aReceiverUID UID получателя запроса
	/// \param aRequest номер запроса
	/// \param aDataSize размер данных, которые данная нода будет ожидать в ответе
	/// \return номер сообщения
	uint8_t sendBlobRequest(uint8_t aReceiverUID, uint8_t aRequest, uint8_t aDataSize)
	{
		BlobReqMessage message;
		message.messageType = MessageType::BlobRequest;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.number = ++messageNumber;
		message.payload.request = aRequest;
		message.payload.answerDataSize = aDataSize;

		const size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
		return message.number;
	}

	/// \brief Функция отправки запроса информации о устройстве
	/// \param aReceiverUID UID получателя запроса
	/// \return номер сообщения
	uint8_t sendDeviceInfoRequest(uint8_t aReceiverUID)
	{
		DeviceInfoReqMessage message;
		message.messageType = MessageType::DeviceInfoReq;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.number = ++messageNumber;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
		return message.number;
	}

	/// \brief Отправка команды на перезагрузку устройства
	/// \param aReceiverUID UID получателя
	/// \param aMagic магическое число для управления
	/// \return номер сообщения
	uint8_t sendRebootCmd(uint8_t aReceiverUID, uint64_t aMagic)
	{
		RebootMessage message;
		message.messageType = MessageType::Reboot;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.payload.magic = aMagic;
		message.number = ++messageNumber;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
		return message.number;
	}

	/// \brief Запрос на отправку файла
	/// \param aReceiverUID UID получателя
	/// \param aFileNum номер файла
	/// \param aFileSize размер отправляемого файла
	/// \return номер сообщения
	uint8_t fileWriteRequest(uint8_t aReceiverUID, uint8_t aFileNum, uint32_t aFileSize)
	{
		FileWriteRequestMessage message;
		message.messageType = MessageType::FileWriteRequest;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.number = ++messageNumber;
		message.payload.fileNumber = aFileNum;
		message.payload.fileSize = aFileSize;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
		return message.number;
	}

	/// \brief Функция отправки чанка на устройство
	/// \param aReceiverUID UID получателя
	/// \param aFileNum номер файла
	/// \param aChunk данные
	/// \param aChunkSize размер чанка
	/// \return номер сообщения
	uint8_t fileWriteChunk(uint8_t aReceiverUID, uint8_t aFileNum, const void *aChunk, uint8_t aChunkSize)
	{
		FileWriteChunkMessage message;
		message.messageType = MessageType::FileWriteChunk;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.number = ++messageNumber;
		message.payload.fileNum = aFileNum;
		message.payload.chunkSize = aChunkSize;

		uint8_t *payloadStart = messageBuffer + 1;
		// Add answer header
		memcpy(payloadStart, &message, sizeof(message));
		// Add answer payload through memcpy
		memcpy(payloadStart + sizeof(message), aChunk, aChunkSize);

		const size_t fullSize = sizeof(message) + aChunkSize;
		const size_t len = parser.create(messageBuffer, payloadStart, fullSize);

		interface.write(messageBuffer, len);
		return message.number;
	}

	/// \brief Отправить завершающую последовательность файла
	/// \param aReceiverUID UID получателя
 	/// \param aFileNum номер файла
	/// \param aChunkNumber общее число чанков
	/// \param aCrc CRC64 от всех чанков
	/// \return номер сообщения
	uint8_t fileWriteFinalize(uint8_t aReceiverUID, uint8_t aFileNum, uint16_t aChunkNumber, uint64_t aCrc)
	{
		FileWriteFinalizeMessage message;
		message.messageType = MessageType::FileWriteFinalize;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.number = ++messageNumber;
		message.payload.fileNum = aFileNum;
		message.payload.chunksNumber = aChunkNumber;
		message.payload.crc = aCrc;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
		return message.number;
	}

	/// \brief Функция отправки Probe сообщения, целевая нода должна ответить, иначе она not present
	/// \param aReceiverUID UID получателя ответа
	/// \return номер сообщения
	uint8_t sendProbe(uint8_t aReceiverUID)
	{
		ProbeMessage message;
		message.messageType = MessageType::Probe;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.number = ++messageNumber;
		message.payload.reserved = 0xFF;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
		return message.number;
	}

	/// \brief Запрос Health устройства
	/// \param aReceiverUID
	/// \return номер сообщения
	uint8_t sendHealthRequest(uint8_t aReceiverUID)
	{
		HealthReqMessage message;
		message.messageType = MessageType::HealthReq;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.number = ++messageNumber;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
		return message.number;
	}

	/// \brief Обработать полученное health
	/// \param aTransmitUID номер отправителя
	/// \param aMessageNumber номер сообщения
	/// \param aHealth здоровье
	/// \param aFlags флаги
	virtual void handleDeviceHealth(uint8_t aTransmitUID, uint8_t aMessageNumber, Health aHealth, uint16_t aFlags)
	{
		return;
	}

	/// \brief Функция обработки запроса, реализуется на стороне клиента, внутри функции собираются и отправляются
	/// данные запрашиваемому Если нужно ответить на запрос то необходимо вызывать функцию sendAnswer внутри этой
	/// функции и вернуть true, иначе просто вернуть false, будет отправлен Ack с ошибкой запроса
	/// \param aTransmitUID - UID получателя ответа на запрос
	/// \param aMessageNumber номер сообщения, ответ должен содержать такой же номер
	/// \param aRequest - номер запроса
	/// \param aRequestedDataSize - размер отправляемых данных
	/// \return статус выполнения команды
	///
	/// Пример реализации:
	/// \code
	/// if (aRequest == 2) {
	///     uint32_t data = 0xAABBCCDD;
	///     return BaseType::sendAnswer(aTransmitUID, aRequest, aRequestedDataSize, reinterpret_cast<void *>(&data),
	///     sizeof(data));
	/// }
	/// return false;
	/// \endcode
	virtual Result processBlobRequest(uint8_t aTransmitUID, uint8_t aMessageNumber, uint8_t aRequest, uint8_t aRequestedDataSize)
	{
		return Result::Unsupported;
	}

	/// \brief Функция обработки команд. возвращает статус, который затем автоматически отправляется как
	/// Code в Ack сообщении отправителю команды
	/// \param aCommand - номер команды
	/// \param aArgument - аргумент команды
	/// \return статус выполнения команды
	///
	/// Пример реализации:
	/// \code
	/// if (aCommand == 1) {
	///     doDomething();
	///     return ACK_SUCCESS; // Клиентский энумератор для кодов возврата
	/// } else {
	///     return 0;
	/// }
	/// \endcode
	virtual Result handleCommand(uint8_t aCommand, uint8_t aArgument)
	{
		return Result::Unsupported;
	}

	/// \brief Обработать запрос на запись файла
	/// \param aTranceiverUID UID отправителя
	/// \param aFile номер файла
	/// \param aFileSize размер файла
	/// \return статус выполнения команды
	virtual Result handleFileWriteRequest(uint8_t aTranceiverUID, uint8_t aFile, uint32_t aFileSize)
	{
		return Result::Unsupported;
	}

	/// \brief Обработать полученный чанк
	/// \param aTransmitUID UID отправителя
	/// \param aFileNum Номер файла
	/// \param aChunkData данные чанка
	/// \param aChunkLen длина чанка
	/// \return статус выполнения команды
	virtual Result handleWriteChunk(uint8_t aTransmitUID, uint8_t aFileNum, const void *aChunkData, size_t aChunkLen)
	{
		return Result::Unsupported;
	}

	/// \brief Обработка полученного чанка с данными
	/// \param aTransmitUID UID отправителя
	/// \param aFileNum номер файла
	/// \param aChunkCount общее число отправленных чанков
	/// \param aFileCRC CRC64 от всех чанков
	/// \return статус выполнения команды
	virtual Result handleWriteChunkFinalize(
		uint8_t aTransmitUID, uint8_t aFileNum, uint16_t aChunkCount, uint64_t aFileCRC)
	{
		return Result::Unsupported;
	}

	/// \brief Обработка ответа на запрос версии
	/// \param aTranceiverUID UID отправителя
	/// \param aMessageNumber номер сообщения
	/// \param aVersion версия ПО
	/// \param aName имя устройства
	/// \param nameLen длина имени устройства
	virtual void handleDeviceInfoAnswer(uint8_t aTranceiverUID, uint8_t aMessageNumber, DeviceVersion aVersion, const void *aName, size_t nameLen) {}

	/// \brief Функция обработки ответа, можно использовать как индикатор того что адресат вообще жив
	/// \param aTranceiverUID - отправитель Ack
	/// \param aMessageNumber номер сообщения
	/// \param aReturnCode - код возврата, который пришел с Ack
	///
	/// Пример реализации:
	/// \code
	/// if (aReturnCode == 0) {
	///     showError();
	/// } else if (aReturnCode == 1) {
	///     showSuccess();
	/// }
	/// \endcode
	virtual void handleAck(uint8_t aTranceiverUID, uint8_t aMessageNumber, Result aReturnCode) {}

	/// \brief Функция обработки команды ребута
	/// \param aMagic - магические числа для типа ребута
	/// \return результат выполнения команды
	virtual Result handleReboot(uint64_t aMagic)
	{
		return Result::Unsupported;
	}

	/// \brief Функция обработки ответа
	/// \param aTranceiverUID - UID отправителя
	/// \param aMessageNumber номер сообщения
	/// \param aRequest - номер запроса
	/// \param aData - указатель на данные ответа
	/// \param aLength - размер данных ответа
	/// \return статус обработки ответа
	///
	/// Пример реализации:
	/// \code
	/// if (aRequest == 1) {
	///     float temperature = reinterpret_cast<float *>(aData);
	///     showTemperature(float);
	///     return 1; // Ack вернется с успехом
	/// } else {
	///     return 0; // Ack вернется с ошибкой
	/// }
	/// \endcode
	virtual Result handleBlobAnswer(uint8_t aTranceiverUID, uint8_t aMessageNumber, uint8_t aRequest, const uint8_t *aData, uint8_t aLength)
	{
		return Result::Unsupported;
	}

protected:
	/// \brief Функция, которая отправляет ответ, собранный в функции processRequest. Вызывать через базовый класс
	/// \param aTranceiverUID - UID отправителя ответа
	/// \param aMessageNumber номер сообщения
	/// \param aRequest - номер запроса
	/// \param aRequestedDataSize - количество байт данных, которые были запрошены
	/// \param aData - указатель на данные для отправки
	/// \param aSize - размер данных для отправки
	/// \return в случае ошибки возвращает false, иначе - true
	bool sendAnswer(uint8_t aReceiverUID, uint8_t aMessageNumber, uint8_t aRequest, uint8_t aRequestedDataSize, void *aData, uint8_t aSize)
	{
		if (aSize != aRequestedDataSize) {
			return false;
		}

		if (aSize + 2 + sizeof(BlobAnwMessage) > ParserSize) {
			return 0;
		}

		BlobAnwMessage header;
		header.transmitUID = nodeUID;
		header.receiverUID = aReceiverUID;
		header.messageType = MessageType::BlobAnswer;
		header.number = aMessageNumber;
		header.payload.dataSize = aSize;
		header.payload.request = aRequest;

		uint8_t *payloadStart = messageBuffer + 1;
		// Add answer header
		memcpy(payloadStart, &header, sizeof(header));
		// Add answer payload through memcpy
		memcpy(payloadStart + sizeof(header), aData, aSize);

		const size_t fullSize = sizeof(header) + aSize;
		const size_t len = parser.create(messageBuffer, payloadStart, fullSize);

		interface.write(messageBuffer, len);
		return true;
	}

private:
	const char *name;
	const DeviceVersion version;

	Health health;
	uint16_t flags;

	uint8_t nodeUID;
	Parser parser;
	Interface &interface;
	uint8_t messageBuffer[ParserSize];
	uint8_t messageNumber;

	/// \brief Функция отправки ответа
	/// \param aTransmitterUID - получатель ответа (отправитель команд\запросов)
	/// \param aMessageNumber номер сообщения, такой же как у сообщения на который формируется ack
	/// \param aReturnCode - код возврата
	void sendAck(uint8_t aTransmitterUID, uint8_t aMessageNumber, Result aReturnCode)
	{
		AckMessage message;
		message.messageType = MessageType::Ack;
		message.receiverUID = aTransmitterUID;
		message.transmitUID = nodeUID;
		message.number = aMessageNumber;
		message.payload.code = aReturnCode;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
	}

	/// \brief Запрос Health
	/// \param aReceiverUID номер получателя
	/// \param aMessageNumber номер сообщения
	void sendHealth(uint8_t aReceiverUID, uint8_t aMessageNumber)
	{
		HealthAnwMessage message;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.number = aMessageNumber;
		message.messageType = MessageType::HealthAnw;

		message.payload.health = health;
		message.payload.flags = flags;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
	}

	/// \brief Обработка запроса получения версии
	/// \param aReceiverUID UID получателя
	/// \param aMessageNumber
	void processDeviceInfoRequest(uint8_t aReceiverUID, uint8_t aMessageNumber)
	{
		DeviceInfoAnwMessage message;
		message.messageType = MessageType::DeviceInfoAnw;
		message.transmitUID = nodeUID;
		message.receiverUID = aReceiverUID;
		message.number = aMessageNumber;
		message.payload.version = version;
		message.payload.nameLen = strlen(name);

		uint8_t *payloadStart = messageBuffer + 1;
		// Add answer header
		memcpy(payloadStart, &message, sizeof(message));
		// Add answer payload through memcpy
		memcpy(payloadStart + sizeof(message), name, message.payload.nameLen);

		const size_t fullSize = sizeof(message) + message.payload.nameLen;
		const size_t len = parser.create(messageBuffer, payloadStart, fullSize);

		interface.write(messageBuffer, len);
	}

	/// \brief Основная функция для взаимодействия внутри протокола, обработка идет только если сообщение предназначено
	/// именно этой ноде
	/// \param aMessage - указатель на распаршенное сообщение
	/// \param aLength - длина сообщения (пока не
	/// зайдествована)
	void process(const uint8_t *aMessage, size_t aLength)
	{
		if (aLength < sizeof(Header)) {
			return;
		}

		const auto *header = reinterpret_cast<const Header *>(aMessage);

		if (header->receiverUID == nodeUID) {
			bool ackNeeded = true;
			Result ackCode{Result::Unsupported};

			switch (header->messageType) {
				case MessageType::Ack: {
					const auto ackMsg = reinterpret_cast<const AckMessage *>(aMessage);
						handleAck(header->transmitUID, header->number, static_cast<Result>(ackMsg->payload.code));
					ackNeeded = false; // ACK на ACK не нужен
				} break;

				case MessageType::BlobAnswer: {
					const auto answerMsg = reinterpret_cast<const BlobAnwMessage *>(aMessage);
					ackCode = handleBlobAnswer(header->transmitUID, header->number, answerMsg->payload.request,
						&aMessage[sizeof(BlobAnwMessage)], answerMsg->payload.dataSize);
				} break;

				case MessageType::Command: {
					const auto cmdMsg = reinterpret_cast<const ComMessage *>(aMessage);
					ackCode = handleCommand(cmdMsg->payload.command, cmdMsg->payload.value);
				} break;

				case MessageType::BlobRequest: {
					const auto reqMsg = reinterpret_cast<const BlobReqMessage *>(aMessage);
					ackCode = processBlobRequest(reqMsg->transmitUID, reqMsg->number, reqMsg->payload.request, reqMsg->payload.answerDataSize);
					if (ackCode == Result::Ok) {
						ackNeeded = false; // Если ответ отправлен - дальше ACK не отправляем
					}
				} break;

				case MessageType::Probe: {
					ackCode = Result::Ok;
				} break;

				case MessageType::Reboot: {
					const auto rebootMsg = reinterpret_cast<const RebootMessage *>(aMessage);
					ackCode = handleReboot(rebootMsg->payload.magic);
				} break;

				case MessageType::DeviceInfoReq: {
					processDeviceInfoRequest(header->transmitUID, header->number);
					ackNeeded = false; // После ответа с ПО не нужен ACK
				} break;

				case MessageType::DeviceInfoAnw: {
					const auto deviceInfo = reinterpret_cast<const DeviceInfoAnwMessage *>(aMessage);
					const size_t nameLen = deviceInfo->payload.nameLen;
					handleDeviceInfoAnswer(header->transmitUID, header->number, deviceInfo->payload.version,
					&aMessage[sizeof(DeviceInfoAnwMessage)], deviceInfo->payload.nameLen);
					ackCode = Result::Ok;
				} break;

				case MessageType::FileWriteRequest: {
					const auto fileWReq = reinterpret_cast<const FileWriteRequestMessage *>(aMessage);
					ackCode = handleFileWriteRequest(header->transmitUID, fileWReq->payload.fileNumber, fileWReq->payload.fileSize);
				} break;

				case MessageType::FileWriteChunk: {
					const auto chunk = reinterpret_cast<const FileWriteChunkMessage *>(aMessage);
					ackCode = handleWriteChunk(header->transmitUID, chunk->payload.fileNum, &aMessage[sizeof(FileWriteChunkMessage)], chunk->payload.chunkSize);
				} break;

				case MessageType::FileWriteFinalize: {
					const auto chunkFinal = reinterpret_cast<const FileWriteFinalizeMessage *>(aMessage);
					ackCode = handleWriteChunkFinalize(header->transmitUID, chunkFinal->payload.fileNum, chunkFinal->payload.chunksNumber, chunkFinal->payload.crc);
				} break;

				case MessageType::HealthReq: {
					sendHealth(header->transmitUID, header->number);
					ackNeeded = false;
				} break;

				case MessageType::HealthAnw: {
					const auto healthMes = reinterpret_cast<const HealthAnwMessage *>(aMessage);
					handleDeviceHealth(header->transmitUID, header->number, healthMes->payload.health, healthMes->payload.flags);
					ackCode = Result::Ok;
				} break;

				default:
					break;
			}

			if (ackNeeded) {
				sendAck(header->transmitUID, header->number, ackCode);
			}
		}
	}
};

} // namespace RS
#endif // LIB_RSHANDLER_HPP
