#ifndef BYBIT_REALTIME_ORDER_RESULT_DTO_H
#define BYBIT_REALTIME_ORDER_RESULT_DTO_H

#include "OrderDto.hpp"

#include <boost/describe.hpp>

#include <vector>

namespace bybit {
    struct RealtimeOrderResultDto
    {
        std::vector<OrderDto> list;
    };

    BOOST_DESCRIBE_STRUCT(RealtimeOrderResultDto, (), (list))
}

#endif
