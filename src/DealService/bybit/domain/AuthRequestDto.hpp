#ifndef BYBIT_AUTH_REQUEST_DTO_H
#define BYBIT_AUTH_REQUEST_DTO_H

#include <boost/json.hpp>

#include <string>
#include <utility>

namespace bybit {
    struct AuthRequestDto
    {
        std::string op;
        std::string apiKey;
        long long expires;
        std::string signature;
    };

    inline void tag_invoke(boost::json::value_from_tag, boost::json::value &value, const AuthRequestDto &request)
    {
        boost::json::object object;
        object["op"] = request.op;
        object["args"] = boost::json::array{request.apiKey, request.expires, request.signature};

        value = std::move(object);
    }
}

#endif
