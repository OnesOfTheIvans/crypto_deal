#ifndef BINANCE_USER_STREAM_SUBSCRIBE_REQUEST_DTO_H
#define BINANCE_USER_STREAM_SUBSCRIBE_REQUEST_DTO_H

#include "UserStreamSubscribeParamsDto.hpp"

#include <boost/describe.hpp>

#include <string>

namespace binance {
    struct UserStreamSubscribeRequestDto
    {
        std::string id;
        std::string method;
        UserStreamSubscribeParamsDto params;
    };

    BOOST_DESCRIBE_STRUCT(UserStreamSubscribeRequestDto, (), (id, method, params))
}

#endif
