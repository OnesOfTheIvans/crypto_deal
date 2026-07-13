#ifndef BINANCE_FILTER_DTO_H
#define BINANCE_FILTER_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace binance {
    struct FilterDto
    {
        std::optional<std::string> filterType;
        std::optional<std::string> minPrice;
        std::optional<std::string> maxPrice;
        std::optional<std::string> tickSize;
        std::optional<std::string> minQty;
        std::optional<std::string> maxQty;
        std::optional<std::string> stepSize;
        std::optional<std::string> minNotional;
        std::optional<std::string> maxNotional;
    };

    BOOST_DESCRIBE_STRUCT(
        FilterDto,
        (),
        (filterType, minPrice, maxPrice, tickSize, minQty, maxQty, stepSize, minNotional, maxNotional))
}

#endif
