#ifndef ORDER_LIST_QUERY_H
#define ORDER_LIST_QUERY_H

#include <string>
#include <optional>

struct OrderListQuery
{
    std::string symbol;                           // required on Binance
    std::optional<std::string> orderListId;       // Binance numeric -> store as string
    std::optional<std::string> listClientOrderId; // Binance listClientOrderId
    std::string category = "spot";                // unused for Binance; kept for symmetry
};

#endif
