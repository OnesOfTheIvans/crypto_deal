#ifndef BYBIT_INSTRUMENT_DTO_H
#define BYBIT_INSTRUMENT_DTO_H

#include "LotSizeFilterDto.hpp"
#include "PriceFilterDto.hpp"

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace bybit {
    struct InstrumentDto
    {
        std::optional<std::string> symbol;
        std::optional<std::string> status;
        std::optional<std::string> baseCoin;
        std::optional<std::string> quoteCoin;
        std::optional<PriceFilterDto> priceFilter;
        std::optional<LotSizeFilterDto> lotSizeFilter;
    };

    BOOST_DESCRIBE_STRUCT(InstrumentDto, (), (symbol, status, baseCoin, quoteCoin, priceFilter, lotSizeFilter))
}

#endif
