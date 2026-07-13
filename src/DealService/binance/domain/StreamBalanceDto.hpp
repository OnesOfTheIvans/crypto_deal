#ifndef BINANCE_STREAM_BALANCE_DTO_H
#define BINANCE_STREAM_BALANCE_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace binance {
    struct StreamBalanceDto
    {
        std::string a;
        std::optional<std::string> f;
        std::optional<std::string> l;
    };

    BOOST_DESCRIBE_STRUCT(StreamBalanceDto, (), (a, f, l))
}

#endif
