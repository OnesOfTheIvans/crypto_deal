#ifndef BINANCE_SERVER_TIME_DTO_H
#define BINANCE_SERVER_TIME_DTO_H

#include <boost/describe.hpp>

#include <cstdint>

namespace binance {
    struct ServerTimeDto
    {
        std::int64_t serverTime;
    };

    BOOST_DESCRIBE_STRUCT(ServerTimeDto, (), (serverTime))
}

#endif
