/*!
\file
\brief Класс, описывающий парсер протокола UtilitaryRS
\author V-Nezlo (vlladimirka@gmail.com)
\date 16.09.2025
\version 2.0
*/

#include "RsTypes.hpp"
#include "RsHelpers.hpp"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef LIB_RSPARSER_HPP_
#define LIB_RSPARSER_HPP_

namespace RS {

template<size_t BufferSize, typename CRC>
class RsParser {
	struct BufferedMessage {
		MessageType type;
		size_t chunkSize;
	};

public:
	enum class State { Idle, Header, ConstPayload, VolatilePayload, Crc, Done };
	static constexpr uint8_t kInitChecksum{0x00};

	RsParser() : position{0}, parserState{State::Idle}, buffer{}, message{}
	{ }

	///
	/// \brief Основная функция парсера
	/// \param aBuffer - указатель на данные, которые нужно отпарсить
	/// \param aLength - длина пришедших данных для парсинга
	/// \return возвращает количество отпарcенных байт
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
							message.type = static_cast<MessageType>(value);
						}

						if (position == sizeof(Header)) {
							if (message.type >= MessageType::TypeEnd) {
								reset();
								return i;
							}

							if (Helpers::getMessageSizeByType(message.type)) {
								parserState = State::ConstPayload;
							} else {
								parserState = State::VolatilePayload;
							}
							break;
						}
					} else {
						reset();
						return i;
					}
					break;

				case State::ConstPayload: {
					const size_t messageSize = Helpers::getMessageSizeByType(message.type);
					if (position < messageSize) {
						buffer[position] = value;

						++position;

						if (position == messageSize) {
							parserState = State::Crc;
						}
					} else {
						reset();
						return i;
					}
					} break;

				case State::VolatilePayload: {
					const size_t baseSize = Helpers::getVolatileMessageBaseSize(message.type);
					const size_t payloadMaxSize = Helpers::getVolatileMessageMaxPayloadSize(message.type);

					if (baseSize == 0 || payloadMaxSize == 0) {
						// Сообщение не поддерживается, сброс
						reset();
						return i;
					}

					if (position < baseSize) {
						buffer[position] = value;
						++position;

						if (position == baseSize) {
							message.chunkSize = value;

							if (message.chunkSize > payloadMaxSize) {
								reset();
								return i;
							}
						}
					} else if (position < baseSize + payloadMaxSize) {
						buffer[position] = value;
						++position;

						if (position == baseSize + message.chunkSize) {
							parserState = State::Crc;
							break;
						}
					}

					} break;

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

				default:
					reset();
					break;
			}

			if (position >= BufferSize) {
				reset();
				return i;
			}
		}
		return aLength;
	}

	/// \brief Получить receiver сообщения из самого сообщения (полное сообщение, включая преамбулу)
	/// \param aBuffer сообщение
	/// \param aLength длина
	/// \return UID
	static uint8_t getReceiverFromMsg(const uint8_t *aBuffer, size_t aLength)
	{
		if (aBuffer == nullptr || aLength < sizeof(Header)) {
			return RS::kReservedUID;
		}

		const Header* header = reinterpret_cast<const Header*>(aBuffer + 1);
		return header->receiverUID;
	}

	/// \brief Получить receiver сообщения из самого сообщения (только header+payload)
	/// \param aBuffer сообщение
	/// \param aLength длина
	/// \return UID
	static uint8_t getReceiverFromPayload(const uint8_t *aBuffer, size_t aLength)
	{
		if (aBuffer == nullptr || aLength < sizeof(Header)) {
			return RS::kReservedUID;
		}

		const Header* header = reinterpret_cast<const Header*>(aBuffer);
		return header->receiverUID;
	}

	/// \brief Создает сообщение протокола UtilitaryRS из сообщения
	/// \param aBuffer - указатель на сырой буффер, внутри которого будет создано сообщение
	/// \param aData - указатель на сообщение
	/// \param aLength - длина сообщения
	/// \return возвращает конечную длину сообщения UtilitaryRS
	size_t create(void *aBuffer, const void *aData, size_t aLength)
	{
		uint8_t *const pos = static_cast<uint8_t *>(aBuffer);

		pos[0] = kPreambl;
		memcpy(&pos[1], aData, aLength);
		pos[aLength + 1] = CRC::calculate(aData, aLength);

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
		message = {};
	}

	bool isReady() const
	{
		return parserState == State::Done;
	}

private:
	size_t position;
	State parserState;
	uint8_t buffer[BufferSize];
	BufferedMessage message;

	static constexpr char kPreambl{'R'};
};

} // namespace RS

#endif //LIB_RSPARSER_HPP_
