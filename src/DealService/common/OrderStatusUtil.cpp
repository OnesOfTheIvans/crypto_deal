#include "OrderStatusUtil.hpp"

#include <set>
#include <stdexcept>
#include <string>

using namespace std;

namespace {
    const set<string> BINANCE_CANCELLED_ORDER_STATUSES = {"CANCELED"};
    const set<string> BINANCE_OTHER_TERMINAL_ORDER_STATUSES = {"REJECTED", "EXPIRED", "EXPIRED_IN_MATCH"};
    const set<string> BYBIT_CANCELLED_ORDER_STATUSES = {"Cancelled", "PartiallyFilledCanceled"};
    const set<string> BYBIT_OTHER_TERMINAL_ORDER_STATUSES = {"Rejected", "Deactivated"};
}

OrderStatusState OrderStatusUtil::classifyOrderStatus(ExchangerType exchangerType, const string &status)
{
    if (status.empty())
    {
        return OrderStatusState::UNAVAILABLE;
    }

    switch (exchangerType)
    {
    case ExchangerType::BINANCE:
        if (status == "FILLED")
        {
            return OrderStatusState::FILLED;
        }
        if (BINANCE_CANCELLED_ORDER_STATUSES.contains(status))
        {
            return OrderStatusState::CANCELLED;
        }
        if (BINANCE_OTHER_TERMINAL_ORDER_STATUSES.contains(status))
        {
            return OrderStatusState::OTHER_TERMINAL;
        }
        return OrderStatusState::ACTIVE;
    case ExchangerType::BYBIT:
        if (status == "Filled")
        {
            return OrderStatusState::FILLED;
        }
        if (BYBIT_CANCELLED_ORDER_STATUSES.contains(status))
        {
            return OrderStatusState::CANCELLED;
        }
        if (BYBIT_OTHER_TERMINAL_ORDER_STATUSES.contains(status))
        {
            return OrderStatusState::OTHER_TERMINAL;
        }
        return OrderStatusState::ACTIVE;
    }

    throw runtime_error("Unsupported exchange while classifying an order status");
}

bool OrderStatusUtil::isOrderFilled(ExchangerType exchangerType, const string &status)
{
    return classifyOrderStatus(exchangerType, status) == OrderStatusState::FILLED;
}

bool OrderStatusUtil::isOrderCancelled(ExchangerType exchangerType, const string &status)
{
    return classifyOrderStatus(exchangerType, status) == OrderStatusState::CANCELLED;
}

bool OrderStatusUtil::isOrderTerminal(ExchangerType exchangerType, const string &status)
{
    const OrderStatusState state = classifyOrderStatus(exchangerType, status);
    return state == OrderStatusState::FILLED || state == OrderStatusState::CANCELLED ||
           state == OrderStatusState::OTHER_TERMINAL;
}
