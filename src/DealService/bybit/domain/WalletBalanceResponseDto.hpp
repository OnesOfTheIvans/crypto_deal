#ifndef BYBIT_WALLET_BALANCE_RESPONSE_DTO_H
#define BYBIT_WALLET_BALANCE_RESPONSE_DTO_H

#include "WalletBalanceResultDto.hpp"

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace bybit {
    struct WalletBalanceResponseDto
    {
        std::int64_t retCode;
        std::optional<std::string> retMsg;
        std::optional<WalletBalanceResultDto> result;
    };

    BOOST_DESCRIBE_STRUCT(WalletBalanceResponseDto, (), (retCode, retMsg, result))
}

#endif
