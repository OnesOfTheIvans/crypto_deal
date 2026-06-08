#ifndef BYBIT_ENUM_STRING_CONVERTER_H
#define BYBIT_ENUM_STRING_CONVERTER_H

#include "common/domain/OrderCategory.hpp"
#include "common/domain/OrderOperation.hpp"
#include "common/domain/OrderType.hpp"

#include <string>
#include <type_traits>
#include <unordered_map>

template <typename T>
concept BybitEnumType = std::is_enum_v<T>;

namespace bybit {
    template <BybitEnumType E> class EnumStringConverter
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
        {OrderOperation::BUY, "Buy"},
        {OrderOperation::SELL, "Sell"}};

    template <>
    inline const std::unordered_map<OrderType, std::string> EnumStringConverter<OrderType>::conversionalMap{
        {OrderType::MARKET, "Market"},
        {OrderType::LIMIT, "Limit"}};

    template <>
    inline const std::unordered_map<OrderCategory, std::string> EnumStringConverter<OrderCategory>::conversionalMap{
        {OrderCategory::SPOT, "spot"}};
}

#endif
