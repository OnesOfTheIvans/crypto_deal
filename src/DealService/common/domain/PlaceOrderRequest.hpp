#ifndef PLACE_ORDER_REQUEST_H
#define PLACE_ORDER_REQUEST_H

#include "OrderCategory.hpp"
#include "OrderOperation.hpp"
#include "OrderType.hpp"

#include <boost/decimal.hpp>

#include <optional>
#include <string>

using Decimal = boost::decimal::decimal128_t;

struct PlaceOrderRequest
{
    std::string symbol;  // e.g. "WLDUSDT"
    OrderOperation side; // common input: BUY / SELL
    OrderType type;      // common input: MARKET / LIMIT
    Decimal quantity{};

    std::optional<Decimal> price;           // required for LIMIT
    std::optional<std::string> timeInForce; // e.g. "GTC" (required for LIMIT on Binance)
    std::optional<std::string> clientOrderId;

    OrderCategory category = OrderCategory::SPOT; // Bybit uses it; Binance ignores

    std::optional<std::string> triggerPrice;
    std::optional<std::string> orderFilter;
    std::optional<std::string> marketUnit;
};

#endif
