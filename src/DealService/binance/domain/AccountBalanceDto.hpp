#ifndef BINANCE_ACCOUNT_BALANCE_DTO_H
#define BINANCE_ACCOUNT_BALANCE_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace binance {
    struct AccountBalanceDto
    {
        std::string asset;
        std::optional<std::string> free;
        std::optional<std::string> locked;
    };

    BOOST_DESCRIBE_STRUCT(AccountBalanceDto, (), (asset, free, locked))
}

#endif
