#ifndef BYBIT_SERVER_TIME_RESPONSE_DTO_H
#define BYBIT_SERVER_TIME_RESPONSE_DTO_H

#include "ServerTimeResultDto.hpp"

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace bybit {
    struct ServerTimeResponseDto
    {
        std::int64_t retCode;
        std::optional<std::string> retMsg;
        std::optional<ServerTimeResultDto> result;
    };

    BOOST_DESCRIBE_STRUCT(ServerTimeResponseDto, (), (retCode, retMsg, result))
}

#endif
