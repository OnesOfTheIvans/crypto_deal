#ifndef BINANCE_USER_STREAM_SUBSCRIBE_PARAMS_DTO_H
#define BINANCE_USER_STREAM_SUBSCRIBE_PARAMS_DTO_H

#include <boost/describe.hpp>

#include <string>

namespace binance {
    struct UserStreamSubscribeParamsDto
    {
        std::string apiKey;
        long long timestamp;
        std::string signature;
    };

    BOOST_DESCRIBE_STRUCT(UserStreamSubscribeParamsDto, (), (apiKey, timestamp, signature))
}

#endif
