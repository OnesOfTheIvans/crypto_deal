#ifndef BYBIT_STREAM_ORDER_DTO_H
#define BYBIT_STREAM_ORDER_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace bybit {
    struct StreamOrderDto
    {
        std::optional<std::string> orderStatus;
        std::optional<std::string> orderLinkId;
    };

    BOOST_DESCRIBE_STRUCT(StreamOrderDto, (), (orderStatus, orderLinkId))
}

#endif
