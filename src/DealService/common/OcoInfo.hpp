#ifndef OCO_INFO_H
#define OCO_INFO_H

#include <string>
#include <vector>
#include "OrderInfo.hpp"

struct OcoInfo
{
    std::string orderListId;               // Binance numeric -> store as string
    std::string listClientOrderId;
    long long transactTimeMs = 0;
    std::vector<OrderInfo> orders;         // 2 orders (legs)
};

#endif
