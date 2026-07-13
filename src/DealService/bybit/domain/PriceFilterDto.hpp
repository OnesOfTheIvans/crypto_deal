#ifndef BYBIT_PRICE_FILTER_DTO_H
#define BYBIT_PRICE_FILTER_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace bybit {
    struct PriceFilterDto
    {
        std::optional<std::string> tickSize;
        std::optional<std::string> minPrice;
        std::optional<std::string> maxPrice;
    };

    BOOST_DESCRIBE_STRUCT(PriceFilterDto, (), (tickSize, minPrice, maxPrice))
}

#endif
