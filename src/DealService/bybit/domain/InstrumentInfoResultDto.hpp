#ifndef BYBIT_INSTRUMENT_INFO_RESULT_DTO_H
#define BYBIT_INSTRUMENT_INFO_RESULT_DTO_H

#include "InstrumentDto.hpp"

#include <boost/describe.hpp>

#include <vector>

namespace bybit {
    struct InstrumentInfoResultDto
    {
        std::vector<InstrumentDto> list;
    };

    BOOST_DESCRIBE_STRUCT(InstrumentInfoResultDto, (), (list))
}

#endif
