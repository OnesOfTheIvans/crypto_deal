#ifndef BYBIT_ORDER_PRICE_LIMIT_DTO_H
#define BYBIT_ORDER_PRICE_LIMIT_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace bybit {
    struct OrderPriceLimitDto
    {
        std::optional<std::string> symbol;
        std::optional<std::string> buyLmt;
        std::optional<std::string> sellLmt;
        std::optional<std::string> ts;
    };

    BOOST_DESCRIBE_STRUCT(OrderPriceLimitDto, (), (symbol, buyLmt, sellLmt, ts))
}

#endif
