#ifndef BYBIT_STREAM_ORDER_MESSAGE_DTO_H
#define BYBIT_STREAM_ORDER_MESSAGE_DTO_H

#include "StreamOrderDto.hpp"

#include <boost/describe.hpp>

#include <optional>
#include <string>
#include <vector>

namespace bybit {
    struct StreamOrderMessageDto
    {
        std::optional<std::string> topic;
        std::optional<std::vector<StreamOrderDto>> data;
    };

    BOOST_DESCRIBE_STRUCT(StreamOrderMessageDto, (), (topic, data))
}

#endif
