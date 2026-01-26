#ifndef ORDER_QUERY_H
#define ORDER_QUERY_H

#include <string>
#include <optional>

struct OrderQuery
{
    std::string symbol;                       // required
    std::optional<std::string> orderId;       // Binance uses numeric orderId; pass as string
    std::optional<std::string> clientOrderId; // Binance: origClientOrderId; Bybit: orderLinkId
    std::string category = "spot";            // Bybit requires; Binance ignores
};

#endif
