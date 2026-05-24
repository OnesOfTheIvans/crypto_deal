#ifndef BINANCE_ACCOUNT_DTO_H
#define BINANCE_ACCOUNT_DTO_H

#include "AccountBalanceDto.hpp"

#include <boost/describe.hpp>

#include <vector>

namespace binance {
    struct AccountDto
    {
        std::vector<AccountBalanceDto> balances;
    };

    BOOST_DESCRIBE_STRUCT(AccountDto, (), (balances))
}

#endif
