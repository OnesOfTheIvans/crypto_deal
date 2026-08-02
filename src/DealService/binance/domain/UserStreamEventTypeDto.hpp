#ifndef BINANCE_USER_STREAM_EVENT_TYPE_DTO_H
#define BINANCE_USER_STREAM_EVENT_TYPE_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace binance {
    struct UserStreamEventTypeDto
    {
        std::optional<std::string> e;
    };

    BOOST_DESCRIBE_STRUCT(UserStreamEventTypeDto, (), (e))
}

#endif
