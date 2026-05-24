#ifndef BYBIT_TICKER_RESPONSE_DTO_H
#define BYBIT_TICKER_RESPONSE_DTO_H

#include "TickerResultDto.hpp"

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace bybit {
    struct TickerResponseDto
    {
        std::int64_t retCode;
        std::optional<std::string> retMsg;
        std::optional<TickerResultDto> result;
    };

    BOOST_DESCRIBE_STRUCT(TickerResponseDto, (), (retCode, retMsg, result))
}

#endif
