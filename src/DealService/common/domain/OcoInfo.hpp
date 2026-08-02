#ifndef OCO_INFO_H
#define OCO_INFO_H

#include "OrderInfo.hpp"
#include <string>

struct OcoInfo
{
    std::string orderListId; // Binance numeric -> store as string
    std::string listClientOrderId;
    long long transactTimeMs = 0;
    OrderInfo takeProfitOrder;
    OrderInfo stopLossOrder;
};

#endif
