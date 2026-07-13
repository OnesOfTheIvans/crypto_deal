#ifndef BYBIT_CANCEL_ORDER_REQUEST_DTO_H
#define BYBIT_CANCEL_ORDER_REQUEST_DTO_H

#include <boost/json.hpp>

#include <optional>
#include <string>
#include <utility>

namespace bybit {
    struct CancelOrderRequestDto
    {
        std::string category;
        std::string symbol;
        std::optional<std::string> orderId;
        std::optional<std::string> orderLinkId;
    };

    inline void tag_invoke(boost::json::value_from_tag, boost::json::value &value, const CancelOrderRequestDto &request)
    {
        boost::json::object object;
        object["category"] = request.category;
        object["symbol"] = request.symbol;

        if (request.orderId.has_value() && !request.orderId.value().empty())
        {
            object["orderId"] = request.orderId.value();
        }
        if (request.orderLinkId.has_value() && !request.orderLinkId.value().empty())
        {
            object["orderLinkId"] = request.orderLinkId.value();
        }

        value = std::move(object);
    }
}

#endif
