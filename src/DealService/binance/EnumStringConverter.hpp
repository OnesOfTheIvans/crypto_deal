#ifndef BINANCE_ENUM_STRING_CONVERTER_H
#define BINANCE_ENUM_STRING_CONVERTER_H

#include "common/domain/OrderOperation.hpp"
#include "common/domain/OrderType.hpp"
#include "domain/FilterType.hpp"

#include <optional>
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
        static const std::unordered_map<std::string, E> reverseConversionalMap;

      public:
        static std::string toString(E key)
        {
            return conversionalMap.at(key);
        }

        static std::optional<E> parseString(const std::string &value)
        {
            const auto it = reverseConversionalMap.find(value);
            return it != reverseConversionalMap.end() ? std::optional<E>{it->second} : std::nullopt;
        }
    };

    template <>
    inline const std::unordered_map<FilterType, std::string> EnumStringConverter<FilterType>::conversionalMap{
        {FilterType::PRICE_FILTER, "PRICE_FILTER"},
        {FilterType::LOT_SIZE, "LOT_SIZE"},
        {FilterType::MIN_NOTIONAL, "MIN_NOTIONAL"},
        {FilterType::NOTIONAL, "NOTIONAL"}};

    template <>
    inline const std::unordered_map<std::string, FilterType> EnumStringConverter<FilterType>::reverseConversionalMap{
        {"PRICE_FILTER", FilterType::PRICE_FILTER},
        {"LOT_SIZE", FilterType::LOT_SIZE},
        {"MIN_NOTIONAL", FilterType::MIN_NOTIONAL},
        {"NOTIONAL", FilterType::NOTIONAL}};

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
