#ifndef ORDER_LIST_QUERY_H
#define ORDER_LIST_QUERY_H

#include "OrderCategory.hpp"

#include <optional>
#include <string>

struct OrderListQuery
{
    std::string symbol;                           // required on Binance
    std::optional<std::string> orderListId;       // Binance numeric -> store as string
    std::optional<std::string> listClientOrderId; // Binance listClientOrderId
    OrderCategory category = OrderCategory::SPOT; // unused for Binance; kept for symmetry
};

#endif
