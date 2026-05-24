#ifndef BINANCE_ERROR_DTO_H
#define BINANCE_ERROR_DTO_H

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace binance {
    struct ErrorDto
    {
        std::optional<std::int64_t> code;
        std::optional<std::string> msg;
    };

    BOOST_DESCRIBE_STRUCT(ErrorDto, (), (code, msg))
}

#endif
