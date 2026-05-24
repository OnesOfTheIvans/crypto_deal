#ifndef BINANCE_ENUM_STRING_CONVERTER_H
#define BINANCE_ENUM_STRING_CONVERTER_H

#include "common/domain/OrderOperation.hpp"
#include "common/domain/OrderType.hpp"

#include <string>
#include <type_traits>
#include <unordered_map>

template <typename T>
concept BinanceEnumType = std::is_enum_v<T>;

namespace binance {
    template <BinanceEnumType E> class EnumStringConverter
    {
      private:
        static const std::unordered_map<E, std::string> conversionalMap;

      public:
        static std::string toString(E key)
        {
            return conversionalMap.at(key);
        }
    };

    template <>
    inline const std::unordered_map<OrderOperation, std::string> EnumStringConverter<OrderOperation>::conversionalMap{
        {OrderOperation::BUY, "BUY"},
        {OrderOperation::SELL, "SELL"}};

    template <>
    inline const std::unordered_map<OrderType, std::string> EnumStringConverter<OrderType>::conversionalMap{
        {OrderType::MARKET, "MARKET"},
        {OrderType::LIMIT, "LIMIT"}};
}

#endif
