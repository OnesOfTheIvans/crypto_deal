#ifndef PLACE_ORDER_REQUEST_H
#define PLACE_ORDER_REQUEST_H

#include "OrderOperation.hpp"
#include "OrderType.hpp"
#include "common/type_aliasing.hpp"

#include <optional>
#include <string>

struct PlaceOrderRequest
{
    std::string symbol;                 // e.g. "WLDUSDT"
    std::optional<OrderOperation> side; // common input: BUY / SELL
    std::optional<OrderType> type;      // common input: MARKET / LIMIT
    Decimal quantity{};

    std::optional<Decimal> price;           // required for LIMIT
    std::optional<std::string> timeInForce; // e.g. "GTC" (required for LIMIT on Binance)
    std::optional<std::string> clientOrderId;

    std::string category = "spot"; // Bybit uses it; Binance ignores

    std::optional<std::string> triggerPrice;
    std::optional<std::string> orderFilter;
    std::optional<std::string> marketUnit;
};

#endif
