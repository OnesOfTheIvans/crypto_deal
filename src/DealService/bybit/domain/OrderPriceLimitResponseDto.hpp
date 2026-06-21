#ifndef BYBIT_ORDER_PRICE_LIMIT_RESPONSE_DTO_H
#define BYBIT_ORDER_PRICE_LIMIT_RESPONSE_DTO_H

#include "OrderPriceLimitDto.hpp"

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace bybit {
    struct OrderPriceLimitResponseDto
    {
        std::int64_t retCode;
        std::optional<std::string> retMsg;
        std::optional<OrderPriceLimitDto> result;
    };

    BOOST_DESCRIBE_STRUCT(OrderPriceLimitResponseDto, (), (retCode, retMsg, result))
}

#endif
