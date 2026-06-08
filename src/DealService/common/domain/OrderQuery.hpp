#ifndef ORDER_QUERY_H
#define ORDER_QUERY_H

#include "OrderCategory.hpp"

#include <optional>
#include <string>

struct OrderQuery
{
    std::string symbol;                           // required
    std::optional<std::string> orderId;           // Binance uses numeric orderId; pass as string
    std::optional<std::string> clientOrderId;     // Binance: origClientOrderId; Bybit: orderLinkId
    OrderCategory category = OrderCategory::SPOT; // Bybit requires; Binance ignores
};

#endif
