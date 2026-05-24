#ifndef BYBIT_ORDER_RESULT_DTO_H
#define BYBIT_ORDER_RESULT_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace bybit {
    struct OrderResultDto
    {
        std::optional<std::string> orderId;
        std::optional<std::string> orderLinkId;
    };

    BOOST_DESCRIBE_STRUCT(OrderResultDto, (), (orderId, orderLinkId))
}

#endif
