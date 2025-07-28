/*!
\file
\brief Класс, описывающий парсер протокола UtilitaryRS
\author V-Nezlo (vlladimirka@gmail.com)
\date 20.10.2023
\version 1.0

*/

#include "RsTypes.hpp"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef LIB_RSPARSER_HPP_
#define LIB_RSPARSER_HPP_

namespace RS {

template<size_t BufferSize, typename CRC>
class RsParser {
	struct BufferedMessage {
		uint8_t type;
		size_t answerSize;
	};

public:
	enum class State { Idle, Header, Command, Request, Answer, Ack, Probe, Crc, Done };
	static constexpr uint8_t kInitChecksum{0x00};

	RsParser() : position{0}, parserState{State::Idle}, buffer{}, message{0, 0}
	{ }

	///
	/// \brief Основная функция парсера
	/// \param aBuffer - указатель на данные, которые нужно отпарсить
	/// \param aLength - длина пришедших данных для парсинга
	/// \return возвращает количество отпаршенных байт
	///
	size_t update(const uint8_t *aBuffer, size_t aLength)
	{
		if (isReady()) {
			reset();
		}

		for (size_t i = 0; i < aLength; ++i) {
			if (isReady()) {
				return i;
			}

			const uint8_t value = *(static_cast<const uint8_t *>(aBuffer) + i);

			switch (parserState) {
				case State::Idle:
					if (value == kPreambl) {
						parserState = State::Header;
					}
					break;
				case State::Header:
					if (position < sizeof(Header)) {
						buffer[position] = value;
						++position;

						if (position == offsetof(Header, messageType) + 1) {
							message.type = value;
						}

						if (position == sizeof(Header)) {
							const auto header = reinterpret_cast<Header *>(buffer);
							switch (header->messageType) {
								case MessageType::Command:
									parserState = State::Command;
									break;
								case MessageType::Request:
									parserState = State::Request;
									break;
								case MessageType::Answer:
									parserState = State::Answer;
									break;
								case MessageType::Ack:
									parserState = State::Ack;
									break;
								case MessageType::Probe:
									parserState = State::Probe;
									break;
							}
						}
					}
					break;
				case State::Command:
					static constexpr size_t kCommandSize = sizeof(CommandPayload) + sizeof(Header);
					if (position < kCommandSize) {
						buffer[position] = value;
						++position;

						if (position == kCommandSize) {
							parserState = State::Crc;
						}
					}
					break;
				case State::Request:
					static constexpr size_t kRequestSize = sizeof(RequestPayload) + sizeof(Header);
					if (position < kRequestSize) {
						buffer[position] = value;

						++position;

						if (position == kRequestSize) {
							parserState = State::Crc;
						}
					}
					break;
				case State::Answer:
					static constexpr size_t kAnswerSize = sizeof(AnswerPayload) + sizeof(Header);
					if (position < kAnswerSize) {
						buffer[position] = value;
						++position;

						if (message.type == static_cast<uint8_t>(MessageType::Answer)
							&& position == 7) {
							message.answerSize = value;
						}
					} else if (position <= BufferSize) {
						if (position < message.answerSize + kAnswerSize) {
							buffer[position] = value;
							++position;

							if (position == message.answerSize + kAnswerSize) {
								parserState = State::Crc;
								break;
							}
						}
					} else {
						reset();
						return i;
					}
					break;

				case State::Ack:
					static constexpr size_t kAckSize = sizeof(AckPayload) + sizeof(Header);
					if (position < kAckSize) {
						buffer[position] = value;
						++position;

						if (position == kAckSize) {
							parserState = State::Crc;
						}
					}
					break;

				case State::Probe:
					static constexpr size_t kProbeSize = sizeof(ProbePayload) + sizeof(Header);
					if (position < kProbeSize) {
						buffer[position] = value;
						++position;

						if (position == kProbeSize) {
							parserState = State::Crc;
						}
					}
					break;

				case State::Crc: {
					uint8_t crc = CRC::calculate(buffer, position);

					if (crc == value) {
						parserState = State::Done;
					} else {
						reset();
					}
				} break;

				case State::Done:
					break;
			}
		}
		return aLength;
	}

	///
	/// \brief Создает сообщение протокола UtilitaryRS из сообщения
	/// \param aBuffer - указатель на сырой буффер, внутри которого будет создано сообщение
	/// \param aData - указатель на сообщение
	/// \param aLength - длина сообщения
	/// \return возвращает конечную длину сообщения UtilitaryRS
	///
	size_t create(void *aBuffer, const void *aData, size_t aLength)
	{
		uint8_t *const position = static_cast<uint8_t *>(aBuffer);

		position[0] = kPreambl;
		memcpy(&position[1], aData, aLength);
		position[aLength + 1] = CRC::calculate(aData, aLength);

		return aLength + 2;
	}

	///
	/// \return Возвращает текущий статус парсера
	///
	State state() const
	{
		return parserState;
	}

	///
	/// \return Возвращает указатель на сырые данные парсера
	///
	const uint8_t *data() const
	{
		return buffer;
	}

	///
	/// \return Возвращает текущую позицию парсера
	///
	size_t length() const
	{
		return position;
	}

	///
	/// \brief Сброс парсера
	///
	void reset()
	{
		position = 0;
		parserState = State::Idle;
	}

private:
	size_t position;
	State parserState;
	uint8_t buffer[BufferSize];
	BufferedMessage message;

	static constexpr char kPreambl{'R'};

	bool isReady() const
	{
		return parserState == State::Done;
	}
};

} // namespace RS

#endif //LIB_RSPARSER_HPP_
