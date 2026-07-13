#ifndef BINANCE_EXCHANGE_INFO_DTO_H
#define BINANCE_EXCHANGE_INFO_DTO_H

#include "SymbolDto.hpp"

#include <boost/describe.hpp>

#include <vector>

namespace binance {
    struct ExchangeInfoDto
    {
        std::vector<SymbolDto> symbols;
    };

    BOOST_DESCRIBE_STRUCT(ExchangeInfoDto, (), (symbols))
}

#endif
