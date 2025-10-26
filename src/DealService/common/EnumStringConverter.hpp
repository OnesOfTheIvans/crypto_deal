#ifndef ENUM_STRING_CONVERTER_H
#define ENUM_STRING_CONVERTER_H

#include "../binance/OrderOperation.hpp"
#include "../binance/OrderType.hpp"

#include <type_traits>
#include <string>
#include <unordered_map>

template <typename T>
concept EnumType = std::is_enum_v<T>;

template <EnumType E>
class EnumStringConverter {
    static const std::unordered_map<E, std::string> conversionalMap;
public:
    static std::string toString(E key) {
        return conversionalMap.at(key);
    }
};

template<>
const std::unordered_map<binance::OrderOperation, std::string> EnumStringConverter<binance::OrderOperation>::conversionalMap {
    { binance::OrderOperation::BUY,  "BUY" },
    { binance::OrderOperation::SELL, "SELL" }
};

template<>
const std::unordered_map<binance::OrderType, std::string> EnumStringConverter<binance::OrderType>::conversionalMap {
    { binance::OrderType::MARKET,  "MARKET" }
};

#endif