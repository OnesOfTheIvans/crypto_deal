#ifndef BYBIT_LOT_SIZE_FILTER_DTO_H
#define BYBIT_LOT_SIZE_FILTER_DTO_H

#include <boost/describe.hpp>

#include <optional>
#include <string>

namespace bybit {
    struct LotSizeFilterDto
    {
        std::optional<std::string> qtyStep;
        std::optional<std::string> basePrecision;
        std::optional<std::string> minOrderQty;
        std::optional<std::string> maxOrderQty;
        std::optional<std::string> minOrderAmt;
        std::optional<std::string> maxOrderAmt;
    };

    BOOST_DESCRIBE_STRUCT(LotSizeFilterDto,
                          (),
                          (qtyStep, basePrecision, minOrderQty, maxOrderQty, minOrderAmt, maxOrderAmt))
}

#endif
