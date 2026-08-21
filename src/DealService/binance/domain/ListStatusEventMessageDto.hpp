#ifndef BINANCE_LIST_STATUS_EVENT_MESSAGE_DTO_H
#define BINANCE_LIST_STATUS_EVENT_MESSAGE_DTO_H

#include "ListStatusEventDto.hpp"

#include <boost/describe.hpp>

#include <optional>

namespace binance {
    struct ListStatusEventMessageDto
    {
        std::optional<ListStatusEventDto> event;
    };

    BOOST_DESCRIBE_STRUCT(ListStatusEventMessageDto, (), (event))
}

#endif
