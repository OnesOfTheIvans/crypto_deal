#ifndef BYBIT_INSTRUMENT_INFO_RESPONSE_DTO_H
#define BYBIT_INSTRUMENT_INFO_RESPONSE_DTO_H

#include "InstrumentInfoResultDto.hpp"

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace bybit {
    struct InstrumentInfoResponseDto
    {
        std::int64_t retCode;
        std::optional<std::string> retMsg;
        std::optional<InstrumentInfoResultDto> result;
    };

    BOOST_DESCRIBE_STRUCT(InstrumentInfoResponseDto, (), (retCode, retMsg, result))
}

#endif
