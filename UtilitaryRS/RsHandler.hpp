/*!
\file
\brief Класс, описывающий поведение протокола RsHandler
\author V-Nezlo (vlladimirka@gmail.com)
\date 20.10.2023
\version 1.0

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
	using ReqMessage = Packet<RequestPayload>;
	using ComMessage = Packet<CommandPayload>;
	using AnwMessage = Packet<AnswerPayload>;
	using AckMessage = Packet<AckPayload>;
	using ProbeMessage = Packet<ProbePayload>;

public:
	///
	/// \brief Конструктор класса RsHandler
	/// \param aInterface - ссылка на экземпляр типа Interface, заданного шаблоном
	/// \param aNodeUID - номер данной ноды (от 0 до 255)
	///
	RsHandler(Interface &aInterface, uint8_t aNodeUID) :
		nodeUID{aNodeUID}, parser{}, interface{aInterface}, messageBuffer{}
	{ }

	///
	/// \brief Основная функция, прокидывающая получаемые байты в парсер и отправляющие в протокольный обработчик
	/// \param aData - указатель на валидные данные
	/// \param aLength - размер валидных данных
	///
	void update(uint8_t *aData, size_t aLength)
	{
		size_t left = aLength;

		while (left) {
			left -= parser.update(static_cast<uint8_t *>(aData) + (aLength - left), left);

			if (parser.state() == Parser::State::Done) {
				process(parser.data(), parser.length());
			}
		}
	}

	///
	/// \brief Функция для отправки команды от текущей ноды
	/// \param aReceiverUID - UID получателя команды
	/// \param aCommand - номер команды
	/// \param aArgument - аргумент для команды (может быть незадействован)
	///
	void sendCommand(uint8_t aReceiverUID, uint8_t aCommand, uint8_t aArgument)
	{
		ComMessage message;
		message.messageType = MessageType::Command;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.payload.command = aCommand;
		message.payload.value = aArgument;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
	}

	///
	/// \brief Функция для отправки запроса на получение данных
	/// \param aReceiverUID - UID получателя запроса
	/// \param aRequest - номер запроса
	/// \param aDataSize - размер данных, которые данная нода будет ожидать в ответе
	///
	void sendRequest(uint8_t aReceiverUID, uint8_t aRequest, uint8_t aDataSize)
	{
		ReqMessage message;
		message.messageType = MessageType::Request;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.payload.request = aRequest;
		message.payload.answerDataSize = aDataSize;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
	}

	///
	/// \brief Функция обработки запроса, реализуется на стороне клиента, внутри функции собираются и отправляются
	/// данные запрашиваемому Если нужно ответить на запрос то необходимо вызывать функцию sendAnswer внутри этой
	/// функции и вернуть true, иначе просто вернуть false, будет отправлен Ack с ошибкой запроса
	/// \param aTransmitUID - UID получателя ответа на запрос
	/// \param aRequest - номер запроса
	/// \param aRequestedDataSize - размер отправляемых данных
	/// \return флаг, показывающий, будет ли обработан запрос
	///
	/// Пример реализации:
	/// \code
	/// if (aRequest == 2) {
	///     uint32_t data = 0xAABBCCDD;
	///     return BaseType::sendAnswer(aTransmitUID, aRequest, aRequestedDataSize, reinterpret_cast<void *>(&data), sizeof(data));
	/// }
	/// return false;
	/// \endcode
	///
	virtual bool processRequest(uint8_t aTransmitUID, uint8_t aRequest, uint8_t aRequestedDataSize) = 0;

	///
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
	///
	virtual uint8_t handleCommand(uint8_t aCommand, uint8_t aArgument) = 0;

	///
	/// \brief Функция обработки ответа, можно использовать как индикатор того что адресат вообще жив
	/// \param aTranceiverUID - отправитель Ack
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
	///
	virtual void handleAck(uint8_t aTranceiverUID, uint8_t aReturnCode) = 0;

	///
	/// \brief Функция обработки ответа
	/// \param aTranceiverUID - UID отправителя
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
	///
	virtual uint8_t handleAnswer(uint8_t aTranceiverUID, uint8_t aRequest, const uint8_t *aData, uint8_t aLength) = 0;

	///
	/// \brief Функция, которая отправляет ответ, собранный в функции processRequest. Вызывать через базовый класс
	/// \param aReceiverUID - UID получателя ответа
	/// \param aRequest - номер запроса
	/// \param aRequestedDataSize - количество байт данных, которые были запрошены
	/// \param aData - указатель на данные для отправки
	/// \param aSize - размер данных для отправки
	/// \return в случае ошибки возвращает false, иначе - true
	///
	bool sendAnswer(uint8_t aReceiverUID, uint8_t aRequest, uint8_t aRequestedDataSize, void *aData, uint8_t aSize)
	{
		if (aSize != aRequestedDataSize) {
			return false;
		}

		if (aSize + 2 + sizeof(AnwMessage) > ParserSize) {
			return 0;
		}

		AnwMessage header;
		header.transmitUID = nodeUID;
		header.receiverUID = aReceiverUID;
		header.messageType = MessageType::Answer;
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

	///
	/// \brief Функция отправки Probe сообщения, целевая нода должна ответить, иначе она not present
	/// \param aReceiverUID UID получателя ответа
	///
	void sendProbe(uint8_t aReceiverUID)
	{
		ProbeMessage message;
		message.messageType = MessageType::Probe;
		message.receiverUID = aReceiverUID;
		message.transmitUID = nodeUID;
		message.payload.reserved = 0xFF;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
	}

private:
	uint8_t nodeUID;
	Parser parser;
	Interface &interface;
	uint8_t messageBuffer[ParserSize];

	///
	/// \brief Функция отправки ответа
	/// \param aTransmitterUID - получатель ответа (отправитель команд\запросов)
	/// \param aReturnCode - код возврата
	///
	void sendAck(uint8_t aTransmitterUID, uint8_t aReturnCode)
	{
		AckMessage message;
		message.messageType = MessageType::Ack;
		message.receiverUID = aTransmitterUID;
		message.transmitUID = nodeUID;
		message.payload.code = aReturnCode;

		size_t length = parser.create(messageBuffer, &message, sizeof(message));
		interface.write(messageBuffer, length);
	}

	///
	/// \brief Основная функция для взаимодействия внутри протокола, обработка идет только если сообщение предназначено
	/// именно этой ноде
	/// \param aMessage - указатель на распаршенное сообщение
	/// \param aLength - длина сообщения (пока не
	/// зайдествована)
	///
	void process(const uint8_t *aMessage, size_t aLength)
	{
		if (aLength < sizeof(Header)) {
			return;
		}

		const auto *header = reinterpret_cast<const Header *>(aMessage);

		if (header->receiverUID == nodeUID) {
			switch (header->messageType) {
				case MessageType::Ack: {
					const auto ackMsg = reinterpret_cast<const AckMessage *>(aMessage);
						handleAck(header->transmitUID, ackMsg->payload.code);
				} break;
				case MessageType::Answer: {
					const auto answerMsg = reinterpret_cast<const AnwMessage *>(aMessage);
					uint8_t returnCode = handleAnswer(header->transmitUID,
						answerMsg->payload.request, &aMessage[sizeof(AnwMessage)], answerMsg->payload.dataSize);
					sendAck(answerMsg->transmitUID, returnCode);
				} break;
				case MessageType::Command: {
					const auto cmdMsg = reinterpret_cast<const ComMessage *>(aMessage);
					uint8_t returnCode = handleCommand(cmdMsg->payload.command, cmdMsg->payload.value);
					sendAck(cmdMsg->transmitUID, returnCode);
				} break;
				case MessageType::Request: {
					const auto reqMsg = reinterpret_cast<const ReqMessage *>(aMessage);
					const auto isRequestProcessed = processRequest(reqMsg->transmitUID, reqMsg->payload.request, reqMsg->payload.answerDataSize);
					if (!isRequestProcessed) {
						sendAck(reqMsg->transmitUID, 0);
					}
				} break;
				case MessageType::Probe: {
					sendAck(header->transmitUID, 1);
				} break;

				default:
				break;
			}
		}
	}
};

} // namespace RS
#endif // LIB_RSHANDLER_HPP
