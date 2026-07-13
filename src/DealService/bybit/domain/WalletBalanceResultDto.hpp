#ifndef BYBIT_WALLET_BALANCE_RESULT_DTO_H
#define BYBIT_WALLET_BALANCE_RESULT_DTO_H

#include "WalletAccountDto.hpp"

#include <boost/describe.hpp>

#include <optional>
#include <vector>

namespace bybit {
    struct WalletBalanceResultDto
    {
        std::optional<std::vector<WalletAccountDto>> list;
    };

    BOOST_DESCRIBE_STRUCT(WalletBalanceResultDto, (), (list))
}

#endif
