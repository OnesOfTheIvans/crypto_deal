#ifndef BINANCE_USER_STREAM_EVENT_TYPE_MESSAGE_DTO_H
#define BINANCE_USER_STREAM_EVENT_TYPE_MESSAGE_DTO_H

#include "UserStreamEventTypeDto.hpp"

#include <boost/describe.hpp>

#include <optional>

namespace binance {
    struct UserStreamEventTypeMessageDto
    {
        std::optional<UserStreamEventTypeDto> event;
    };

    BOOST_DESCRIBE_STRUCT(UserStreamEventTypeMessageDto, (), (event))
}

#endif
