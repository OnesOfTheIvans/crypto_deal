#ifndef BYBIT_SUBSCRIBE_RESPONSE_DTO_H
#define BYBIT_SUBSCRIBE_RESPONSE_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace bybit {
    struct SubscribeResponseDto
    {
        std::optional<std::string> op;
        std::optional<bool> success;
    };

    BOOST_DESCRIBE_STRUCT(SubscribeResponseDto, (), (op, success))
}

#endif
