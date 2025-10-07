#ifndef ORDER_TYPE_H
#define ORDER_TYPE_H

#include <string>
#include <unordered_map>

enum class OrderType {
    MARKET
};

namespace orderType {
    static const std::unordered_map<OrderType, std::string> typeToString {
        { OrderType::MARKET,  "MARKET" }
    };
}

#endif