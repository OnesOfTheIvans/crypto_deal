#ifndef ORDER_STATUS_UTIL_H
#define ORDER_STATUS_UTIL_H

#include "ExchangerType.hpp"

#include <string>

enum class OrderStatusState
{
    UNAVAILABLE,
    ACTIVE,
    FILLED,
    CANCELLED,
    OTHER_TERMINAL
};

class OrderStatusUtil final
{
  public:
    static OrderStatusState classifyOrderStatus(ExchangerType exchangerType, const std::string &status);

    static bool isOrderFilled(ExchangerType exchangerType, const std::string &status);

    static bool isOrderCancelled(ExchangerType exchangerType, const std::string &status);

    static bool isOrderTerminal(ExchangerType exchangerType, const std::string &status);
};

#endif
