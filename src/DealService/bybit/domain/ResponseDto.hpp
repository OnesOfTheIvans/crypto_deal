#ifndef BYBIT_RESPONSE_DTO_H
#define BYBIT_RESPONSE_DTO_H

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace bybit {
    struct ResponseDto
    {
        std::int64_t retCode;
        std::optional<std::string> retMsg;
    };

    BOOST_DESCRIBE_STRUCT(ResponseDto, (), (retCode, retMsg))
}

#endif
