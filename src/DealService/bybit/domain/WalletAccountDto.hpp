#ifndef BYBIT_WALLET_ACCOUNT_DTO_H
#define BYBIT_WALLET_ACCOUNT_DTO_H

#include "CoinBalanceDto.hpp"

#include <boost/describe.hpp>

#include <optional>
#include <vector>

namespace bybit {
    struct WalletAccountDto
    {
        std::optional<std::vector<CoinBalanceDto>> coin;
    };

    BOOST_DESCRIBE_STRUCT(WalletAccountDto, (), (coin))
}

#endif
