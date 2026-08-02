#ifndef BINANCE_LIST_STATUS_EVENT_DTO_H
#define BINANCE_LIST_STATUS_EVENT_DTO_H

#include "ListStatusOrderDto.hpp"

#include <boost/describe.hpp>

#include <optional>
#include <string>
#include <vector>

namespace binance {
    struct ListStatusEventDto
    {
        std::optional<std::string> L;
        std::optional<std::string> r;
        std::optional<std::vector<ListStatusOrderDto>> O;
    };

    BOOST_DESCRIBE_STRUCT(ListStatusEventDto, (), (L, r, O))
}

#endif
