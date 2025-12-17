/*!
\file
\brief Класс-композит для нескольких устройств
\author V-Nezlo (vlladimirka@gmail.com)
\date 10.11.2025
\version 2.0
*/

#ifndef LIB_COMPOSITEDEVICE_HPP_
#define LIB_COMPOSITEDEVICE_HPP_

#include "RsParser.hpp"
#include "RsTypes.hpp"
#include <tuple>

namespace RS {

template<size_t ParserSize, typename CRC, typename... Ts>
class MultiNode {
	using Parser = RS::RsParser<ParserSize, CRC>;

	std::tuple<Ts &...> devices;
	Parser parser;

public:
	template<typename... ARGs>
	MultiNode(const std::tuple<ARGs...> &args) : devices{args}
	{
	}

	/// \brief Основная функция, прокидывающая получаемые байты в парсер и отправляющие в протокольный обработчик
	/// \param aData указатель на валидные данные
	/// \param aLength размер валидных данных
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
				routeMessage(parser.data(), parser.length());
				parser.reset();
			}
		}
	}

private:
	/// \brief Функция маршрутизации для устройств
	void routeMessage(const uint8_t *aMessage, size_t aLength)
	{
		const uint8_t targetUID = Parser::getReceiverFromPayload(aMessage, aLength);

		std::apply(
			[&](auto &...device)
			{
				(((device.getUid() == targetUID || targetUID == RS::kReservedUID) ? device.process(aMessage, aLength) : void()), ...);
			},
			devices);
	}
};

} // namespace RS

#endif // LIB_COMPOSITEDEVICE_HPP_
