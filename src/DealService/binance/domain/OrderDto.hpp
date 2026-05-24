#ifndef BINANCE_ORDER_DTO_H
#define BINANCE_ORDER_DTO_H

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace binance {
    struct OrderDto
    {
        std::optional<std::string> symbol;
        std::optional<std::int64_t> orderId;
        std::optional<std::string> clientOrderId;
        std::optional<std::string> side;
        std::optional<std::string> type;
        std::optional<std::string> timeInForce;
        std::optional<std::string> status;
        std::optional<std::string> price;
        std::optional<std::string> origQty;
        std::optional<std::string> executedQty;
        std::optional<std::string> cummulativeQuoteQty;
        std::optional<std::string> cumulativeQuoteQty;
        std::optional<std::int64_t> transactTime;
    };

    BOOST_DESCRIBE_STRUCT(OrderDto,
                          (),
                          (symbol,
                           orderId,
                           clientOrderId,
                           side,
                           type,
                           timeInForce,
                           status,
                           price,
                           origQty,
                           executedQty,
                           cummulativeQuoteQty,
                           cumulativeQuoteQty,
                           transactTime))
}

#endif
