#ifndef BINANCE_OUTBOUND_ACCOUNT_POSITION_EVENT_DTO_H
#define BINANCE_OUTBOUND_ACCOUNT_POSITION_EVENT_DTO_H

#include "StreamBalanceDto.hpp"

#include <boost/describe.hpp>

#include <optional>
#include <string>
#include <vector>

namespace binance {
    struct OutboundAccountPositionEventDto
    {
        std::optional<std::string> e;
        std::optional<std::vector<StreamBalanceDto>> B;
    };

    BOOST_DESCRIBE_STRUCT(OutboundAccountPositionEventDto, (), (e, B))
}

#endif
