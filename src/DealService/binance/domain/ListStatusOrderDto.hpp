#ifndef BINANCE_LIST_STATUS_ORDER_DTO_H
#define BINANCE_LIST_STATUS_ORDER_DTO_H

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace binance {
    struct ListStatusOrderDto
    {
        std::optional<std::string> s;
        std::optional<std::int64_t> i;
        std::optional<std::string> c;
    };

    BOOST_DESCRIBE_STRUCT(ListStatusOrderDto, (), (s, i, c))
}

#endif
