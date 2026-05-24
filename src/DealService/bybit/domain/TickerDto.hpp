#ifndef BYBIT_TICKER_DTO_H
#define BYBIT_TICKER_DTO_H

#include <boost/describe.hpp>

#include <string>

namespace bybit {
    struct TickerDto
    {
        std::string lastPrice;
    };

    BOOST_DESCRIBE_STRUCT(TickerDto, (), (lastPrice))
}

#endif
