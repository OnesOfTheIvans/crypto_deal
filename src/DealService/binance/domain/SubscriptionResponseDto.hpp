#ifndef BINANCE_SUBSCRIPTION_RESPONSE_DTO_H
#define BINANCE_SUBSCRIPTION_RESPONSE_DTO_H

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>

namespace binance {
    struct SubscriptionResponseDto
    {
        std::optional<std::int64_t> status;
    };

    BOOST_DESCRIBE_STRUCT(SubscriptionResponseDto, (), (status))
}

#endif
