/*!
\file
\brief Файл, описывающий сообщения внутри протокола UtilitaryRS
\author V-Nezlo (vlladimirka@gmail.com)
\date 20.10.2023
\version 1.0
*/

#ifndef LIB_RSTYPES_HPP_
#define LIB_RSTYPES_HPP_

#include <stdint.h>

namespace RS {

///
/// \brief Энумератор типов сообщений - сообщения имеют следующие типы: Команда, Запрос данных, Ответ на запрос данных, подверждение приема сообщения
///
enum class MessageType : uint8_t { Probe = 0, Ack, Command, Request, Answer };

///
/// \brief Заголовок сообщения, содержит UID отправителя, UID получателя и тип сообщения
///
struct Header {
	uint8_t receiverUID;
	uint8_t transmitUID;
	MessageType messageType;
	uint8_t reserved;
} __attribute__((packed));

template<class T>
struct Packet : public Header {
	T payload;
} __attribute__((packed));

///
/// \brief Полезная нагрузка командного сообщения, содержит номер команды и аргумент
///
struct CommandPayload {
	uint8_t command;
	uint8_t value;
	uint8_t reserved;
} __attribute__((packed));

///
/// \brief Полезная нагрузка сообщения запроса данных, содержит номер запроса и количество запрашиваемых байт
///
struct RequestPayload {
	uint8_t request;
	uint8_t answerDataSize;
} __attribute__((packed));

///
/// \brief Полезная нагрузка сообщения ответа на запрос, содержит номер запроса, размер данных запроса
/// Сразу после последнего поля dataSize следует сам запрос размером dataSize
///
struct AnswerPayload {
	uint8_t request;
	uint8_t reserved;
	uint8_t dataSize;
} __attribute__((packed));

///
/// \brief Полезная нагрузка Ack сообщения, содержит код возврата
///
struct AckPayload {
	uint8_t code;
} __attribute__((packed));

///
/// \brief Полезная нагрузка сообщения Probe, не содержит ничего (пока)
///
struct ProbePayload {
	uint8_t reserved;
} __attribute__((packed));

} // namespace RS

#endif // LIB_RSTYPES_HPP_
