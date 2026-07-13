#ifndef BYBIT_CREATE_ORDER_REQUEST_DTO_H
#define BYBIT_CREATE_ORDER_REQUEST_DTO_H

#include <boost/json.hpp>

#include <optional>
#include <string>
#include <utility>

namespace bybit {
    struct CreateOrderRequestDto
    {
        std::string category;
        std::string symbol;
        std::string side;
        std::string orderType;
        std::string qty;
        std::optional<std::string> price;
        std::optional<std::string> timeInForce;
        std::optional<std::string> orderLinkId;
        std::optional<std::string> triggerPrice;
        std::optional<std::string> orderFilter;
        std::optional<std::string> marketUnit;
    };

    inline void tag_invoke(boost::json::value_from_tag, boost::json::value &value, const CreateOrderRequestDto &request)
    {
        boost::json::object object;
        object["category"] = request.category;
        object["symbol"] = request.symbol;
        object["side"] = request.side;
        object["orderType"] = request.orderType;
        object["qty"] = request.qty;

        if (request.price.has_value() && !request.price.value().empty())
        {
            object["price"] = request.price.value();
        }
        if (request.timeInForce.has_value() && !request.timeInForce.value().empty())
        {
            object["timeInForce"] = request.timeInForce.value();
        }
        if (request.orderLinkId.has_value() && !request.orderLinkId.value().empty())
        {
            object["orderLinkId"] = request.orderLinkId.value();
        }
        if (request.triggerPrice.has_value() && !request.triggerPrice.value().empty())
        {
            object["triggerPrice"] = request.triggerPrice.value();
        }
        if (request.orderFilter.has_value() && !request.orderFilter.value().empty())
        {
            object["orderFilter"] = request.orderFilter.value();
        }
        if (request.marketUnit.has_value() && !request.marketUnit.value().empty())
        {
            object["marketUnit"] = request.marketUnit.value();
        }

        value = std::move(object);
    }
}

#endif
