#ifndef ENUM_STRING_CONVERTER_H
#define ENUM_STRING_CONVERTER_H

#include "OrderOperation.hpp"
#include "OrderType.hpp"

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
const std::unordered_map<OrderOperation, std::string> EnumStringConverter<OrderOperation>::conversionalMap {
    { OrderOperation::BUY,  "BUY" },
    { OrderOperation::SELL, "SELL" }
};

template<>
const std::unordered_map<OrderType, std::string> EnumStringConverter<OrderType>::conversionalMap {
    { OrderType::MARKET,  "MARKET" }
};

#endif