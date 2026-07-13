#ifndef BYBIT_ORDER_RESPONSE_DTO_H
#define BYBIT_ORDER_RESPONSE_DTO_H

#include "OrderResultDto.hpp"

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace bybit {
    struct OrderResponseDto
    {
        std::int64_t retCode;
        std::optional<std::string> retMsg;
        std::optional<OrderResultDto> result;
    };

    BOOST_DESCRIBE_STRUCT(OrderResponseDto, (), (retCode, retMsg, result))
}

#endif
