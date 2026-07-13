#ifndef BYBIT_SERVER_TIME_RESULT_DTO_H
#define BYBIT_SERVER_TIME_RESULT_DTO_H

#include <boost/describe.hpp>

#include <string>

namespace bybit {
    struct ServerTimeResultDto
    {
        std::string timeSecond;
    };

    BOOST_DESCRIBE_STRUCT(ServerTimeResultDto, (), (timeSecond))
}

#endif
