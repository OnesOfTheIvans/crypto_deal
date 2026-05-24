#ifndef BINANCE_OCO_DTO_H
#define BINANCE_OCO_DTO_H

#include "OrderDto.hpp"

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace binance {
    struct OcoDto
    {
        std::optional<std::int64_t> orderListId;
        std::optional<std::string> listClientOrderId;
        std::optional<std::int64_t> transactionTime;
        std::vector<OrderDto> orderReports;
    };

    BOOST_DESCRIBE_STRUCT(OcoDto, (), (orderListId, listClientOrderId, transactionTime, orderReports))
}

#endif
