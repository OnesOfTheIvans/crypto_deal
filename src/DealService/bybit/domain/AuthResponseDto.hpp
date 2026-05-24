#ifndef BYBIT_AUTH_RESPONSE_DTO_H
#define BYBIT_AUTH_RESPONSE_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace bybit {
    struct AuthResponseDto
    {
        std::optional<std::string> op;
        std::optional<bool> success;
    };

    BOOST_DESCRIBE_STRUCT(AuthResponseDto, (), (op, success))
}

#endif
