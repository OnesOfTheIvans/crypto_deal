#ifndef ENUM_STRING_CONVERTER_H
#define ENUM_STRING_CONVERTER_H

#include "../binance/OrderOperation.hpp"
#include "../binance/OrderType.hpp"
#include "../bybit/OrderOperation.hpp"
#include "../bybit/OrderType.hpp"
#include "../bybit/OrderCategory.hpp"

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
inline const std::unordered_map<binance::OrderOperation, std::string> EnumStringConverter<binance::OrderOperation>::conversionalMap {
    { binance::OrderOperation::BUY,  "BUY" },
    { binance::OrderOperation::SELL, "SELL" }
};

template<>
inline const std::unordered_map<binance::OrderType, std::string> EnumStringConverter<binance::OrderType>::conversionalMap {
    { binance::OrderType::MARKET,  "MARKET" },
    { binance::OrderType::LIMIT,  "LIMIT" }
};

template<>
inline const std::unordered_map<bybit::OrderOperation, std::string> EnumStringConverter<bybit::OrderOperation>::conversionalMap {
    { bybit::OrderOperation::BUY,  "Buy" },
    { bybit::OrderOperation::SELL, "Sell" }
};

template<>
inline const std::unordered_map<bybit::OrderType, std::string> EnumStringConverter<bybit::OrderType>::conversionalMap {
    { bybit::OrderType::MARKET,  "Market" },
    { bybit::OrderType::LIMIT,  "Limit" }
};

template<>
inline const std::unordered_map<bybit::OrderCategory, std::string> EnumStringConverter<bybit::OrderCategory>::conversionalMap {
    { bybit::OrderCategory::SPOT,  "spot" }
};

#endif