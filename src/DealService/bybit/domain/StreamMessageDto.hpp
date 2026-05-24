#ifndef BYBIT_STREAM_MESSAGE_DTO_H
#define BYBIT_STREAM_MESSAGE_DTO_H

#include "WalletAccountDto.hpp"

#include <boost/describe.hpp>

#include <optional>
#include <string>
#include <vector>

namespace bybit {
    struct StreamMessageDto
    {
        std::optional<std::string> topic;
        std::optional<std::vector<WalletAccountDto>> data;
    };

    BOOST_DESCRIBE_STRUCT(StreamMessageDto, (), (topic, data))
}

#endif
