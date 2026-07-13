#ifndef BYBIT_TICKER_RESULT_DTO_H
#define BYBIT_TICKER_RESULT_DTO_H

#include "TickerDto.hpp"

#include <boost/describe.hpp>

#include <vector>

namespace bybit {
    struct TickerResultDto
    {
        std::vector<TickerDto> list;
    };

    BOOST_DESCRIBE_STRUCT(TickerResultDto, (), (list))
}

#endif
