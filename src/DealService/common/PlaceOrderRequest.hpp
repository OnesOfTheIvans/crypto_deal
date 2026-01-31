#ifndef PLACE_ORDER_REQUEST_H
#define PLACE_ORDER_REQUEST_H

#include <optional>
#include <string>

struct PlaceOrderRequest
{
    std::string symbol; // e.g. "WLDUSDT"
    std::string side;   // normalized input: "BUY" / "SELL"
    std::string type;   // normalized input: "MARKET" / "LIMIT"
    double quantity = 0.0;

    std::optional<double> price;            // required for LIMIT
    std::optional<std::string> timeInForce; // e.g. "GTC" (required for LIMIT on Binance)
    std::optional<std::string> clientOrderId;

    std::string category = "spot"; // Bybit uses it; Binance ignores

    std::optional<std::string> triggerPrice;
    std::optional<std::string> orderFilter;
    std::optional<std::string> marketUnit;
};

#endif
