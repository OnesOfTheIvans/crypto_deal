#ifndef BYBIT_COIN_BALANCE_DTO_H
#define BYBIT_COIN_BALANCE_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace bybit {
    struct CoinBalanceDto
    {
        std::string coin;
        std::optional<std::string> walletBalance;
        std::optional<std::string> locked;
        std::optional<std::string> availableToWithdraw;
        std::optional<std::string> availableToTrade;
    };

    BOOST_DESCRIBE_STRUCT(CoinBalanceDto, (), (coin, walletBalance, locked, availableToWithdraw, availableToTrade))
}

#endif
