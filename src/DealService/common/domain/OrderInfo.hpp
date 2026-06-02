#ifndef ORDER_INFO_H
#define ORDER_INFO_H

#include <boost/decimal.hpp>

#include <string>

using Decimal = boost::decimal::decimal128_t;

struct OrderInfo
{
    // ------------------------
    // Common (Binance + Bybit)
    // ------------------------
    std::string symbol; // e.g. "WLDUSDT"

    std::string orderId; // Common: unique order id
                         // Binance: numeric "orderId" -> store as string via to_string(...)
                         // Bybit: already string "orderId"

    std::string clientOrderId; // Common: client-supplied id (optional)
                               // Binance: "clientOrderId" / request "newClientOrderId"
                               // Bybit: "orderLinkId"

    std::string side; // Common meaning: buy/sell
                      // Binance values: "BUY" / "SELL"
                      // Bybit values: "Buy" / "Sell"

    std::string type; // Common meaning: order type
                      // Binance: "MARKET","LIMIT",...
                      // Bybit: "Market","Limit",...

    std::string timeInForce; // Common meaning: GTC/IOC/FOK...
                             // Binance: present for LIMIT (and others), values "GTC","IOC","FOK"
                             // Bybit: similar, values "GTC","IOC","FOK" (case may differ)

    std::string status; // Common meaning: lifecycle state
                        // Binance: "NEW","PARTIALLY_FILLED","FILLED",... (ALL CAPS)
                        // Bybit: "New","PartiallyFilled","Filled",... (+ Untriggered, etc.)

    Decimal price{}; // Common meaning: order price
                     // Market orders: often 0 or empty
                     // For LIMIT: must be set

    Decimal origQty{}; // Common meaning: requested quantity
                       // Binance: "origQty"
                       // Bybit: "qty"

    Decimal executedQty{}; // Common meaning: filled quantity
                           // Binance: "executedQty"
                           // Bybit: "cumExecQty"

    Decimal cumQuoteQty{}; // Common meaning: filled quote amount (value)
                           // Binance: "cummulativeQuoteQty" (or "cumulativeQuoteQty" in some contexts)
                           // Bybit: "cumExecValue"

    long long createdTimeMs = 0; // Common meaning: order creation time in ms
                                 // Binance: often "transactTime" in place order response; in queries you may have
                                 // "time" Bybit: "createdTime"

    long long updatedTimeMs = 0; // Common meaning: last update time in ms
                                 // Binance: "updateTime" in order query; may be absent in placeOrder response
                                 // Bybit: "updatedTime"

    // -----------------------------------------
    // Bybit-unique / optional (keep if you need)
    // -----------------------------------------
    std::string category; // Bybit V5 required concept: "spot"/"linear"/...
                          // Binance Spot: not used (leave empty)

    Decimal leavesQty{}; // Bybit: remaining qty "leavesQty"
                         // Binance: can be derived as origQty - executedQty (not always exact with rounding)

    Decimal avgPrice{}; // Bybit: "avgPrice"
                        // Binance: can be derived: cumQuoteQty / executedQty (if executedQty>0), not a native field
};

#endif
