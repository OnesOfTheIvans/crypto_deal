#ifndef BYBIT_CANCEL_ALL_OPEN_ORDERS_REQUEST_DTO_H
#define BYBIT_CANCEL_ALL_OPEN_ORDERS_REQUEST_DTO_H

#include <boost/json.hpp>

#include <optional>
#include <string>
#include <utility>

namespace bybit {
    struct CancelAllOpenOrdersRequestDto
    {
        std::string category;
        std::optional<std::string> symbol;
    };

    inline void
    tag_invoke(boost::json::value_from_tag, boost::json::value &value, const CancelAllOpenOrdersRequestDto &request)
    {
        boost::json::object object;
        object["category"] = request.category;

        if (request.symbol.has_value() && !request.symbol.value().empty())
        {
            object["symbol"] = request.symbol.value();
        }

        value = std::move(object);
    }
}

#endif
