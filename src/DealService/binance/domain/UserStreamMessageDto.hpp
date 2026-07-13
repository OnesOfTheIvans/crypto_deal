#ifndef BINANCE_USER_STREAM_MESSAGE_DTO_H
#define BINANCE_USER_STREAM_MESSAGE_DTO_H

#include "OutboundAccountPositionEventDto.hpp"

#include <boost/describe.hpp>

#include <optional>

namespace binance {
    struct UserStreamMessageDto
    {
        std::optional<OutboundAccountPositionEventDto> event;
    };

    BOOST_DESCRIBE_STRUCT(UserStreamMessageDto, (), (event))
}

#endif
