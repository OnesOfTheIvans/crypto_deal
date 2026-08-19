#ifndef BINANCE_SYMBOL_DTO_H
#define BINANCE_SYMBOL_DTO_H

#include "FilterDto.hpp"

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace binance {
    struct SymbolDto
    {
        std::string symbol;
        std::string status;
        std::string baseAsset;
        std::string quoteAsset;
        std::optional<std::int64_t> baseAssetPrecision;
        std::optional<std::int64_t> quotePrecision;
        std::optional<bool> isSpotTradingAllowed;
        std::vector<FilterDto> filters;
    };

    BOOST_DESCRIBE_STRUCT(
        SymbolDto,
        (),
        (symbol, status, baseAsset, quoteAsset, baseAssetPrecision, quotePrecision, isSpotTradingAllowed, filters))
}

#endif
