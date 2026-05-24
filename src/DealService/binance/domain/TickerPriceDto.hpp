#ifndef BINANCE_TICKER_PRICE_DTO_H
#define BINANCE_TICKER_PRICE_DTO_H

#include <boost/describe.hpp>

#include <string>

namespace binance {
    struct TickerPriceDto
    {
        std::string price;
    };

    BOOST_DESCRIBE_STRUCT(TickerPriceDto, (), (price))
}

#endif
