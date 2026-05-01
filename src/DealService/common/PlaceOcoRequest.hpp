#ifndef PLACE_OCO_REQUEST_H
#define PLACE_OCO_REQUEST_H

#include "type_aliasing.hpp"

#include <optional>
#include <string>

struct PlaceOcoRequest
{
    std::string symbol; // "WLDUSDT"
    std::string side;   // "BUY" or "SELL"
    Decimal quantity{};

    Decimal price{};     // LIMIT leg price
    Decimal stopPrice{}; // stop trigger price

    std::optional<Decimal> stopLimitPrice;           // if set -> STOP_LOSS_LIMIT leg
    std::optional<std::string> stopLimitTimeInForce; // required if stopLimitPrice set, e.g. "GTC"

    std::optional<std::string> listClientOrderId;
    std::optional<std::string> limitClientOrderId;
    std::optional<std::string> stopClientOrderId;
};

#endif