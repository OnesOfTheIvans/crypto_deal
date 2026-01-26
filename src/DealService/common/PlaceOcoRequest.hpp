#ifndef PLACE_OCO_REQUEST_H
#define PLACE_OCO_REQUEST_H

#include <string>
#include <optional>

struct PlaceOcoRequest
{
    std::string symbol;                    // "WLDUSDT"
    std::string side;                      // "BUY" or "SELL"
    double quantity = 0.0;

    double price = 0.0;                    // LIMIT leg price
    double stopPrice = 0.0;                // stop trigger price

    std::optional<double> stopLimitPrice;  // if set -> STOP_LOSS_LIMIT leg
    std::optional<std::string> stopLimitTimeInForce; // required if stopLimitPrice set, e.g. "GTC"

    std::optional<std::string> listClientOrderId;
    std::optional<std::string> limitClientOrderId;
    std::optional<std::string> stopClientOrderId;
};

#endif
