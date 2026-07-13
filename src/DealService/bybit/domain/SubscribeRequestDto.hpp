#ifndef BYBIT_SUBSCRIBE_REQUEST_DTO_H
#define BYBIT_SUBSCRIBE_REQUEST_DTO_H

#include <boost/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace bybit {
    struct SubscribeRequestDto
    {
        std::string op;
        std::vector<std::string> args;
    };

    inline void tag_invoke(boost::json::value_from_tag, boost::json::value &value, const SubscribeRequestDto &request)
    {
        boost::json::object object;
        object["op"] = request.op;
        object["args"] = boost::json::value_from(request.args);

        value = std::move(object);
    }
}

#endif
