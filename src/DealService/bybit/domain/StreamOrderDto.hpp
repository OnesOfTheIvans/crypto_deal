#ifndef BYBIT_STREAM_ORDER_DTO_H
#define BYBIT_STREAM_ORDER_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace bybit {
    struct StreamOrderDto
    {
        std::optional<std::string> category;
        std::optional<std::string> symbol;
        std::optional<std::string> orderId;
        std::optional<std::string> orderLinkId;
        std::optional<std::string> side;
        std::optional<std::string> orderType;
        std::optional<std::string> timeInForce;
        std::optional<std::string> orderStatus;
        std::optional<std::string> price;
        std::optional<std::string> qty;
        std::optional<std::string> cumExecQty;
        std::optional<std::string> cumExecValue;
        std::optional<std::string> leavesQty;
        std::optional<std::string> avgPrice;
        std::optional<std::string> createdTime;
        std::optional<std::string> updatedTime;
        std::optional<std::string> rejectReason;
        std::optional<std::string> cancelType;
    };

    BOOST_DESCRIBE_STRUCT(StreamOrderDto,
                          (),
                          (category,
                           symbol,
                           orderId,
                           orderLinkId,
                           side,
                           orderType,
                           timeInForce,
                           orderStatus,
                           price,
                           qty,
                           cumExecQty,
                           cumExecValue,
                           leavesQty,
                           avgPrice,
                           createdTime,
                           updatedTime,
                           rejectReason,
                           cancelType))
}

#endif
