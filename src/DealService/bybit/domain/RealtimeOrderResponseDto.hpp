#ifndef BYBIT_REALTIME_ORDER_RESPONSE_DTO_H
#define BYBIT_REALTIME_ORDER_RESPONSE_DTO_H

#include "RealtimeOrderResultDto.hpp"

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace bybit {
    struct RealtimeOrderResponseDto
    {
        std::int64_t retCode;
        std::optional<std::string> retMsg;
        std::optional<RealtimeOrderResultDto> result;
    };

    BOOST_DESCRIBE_STRUCT(RealtimeOrderResponseDto, (), (retCode, retMsg, result))
}

#endif
