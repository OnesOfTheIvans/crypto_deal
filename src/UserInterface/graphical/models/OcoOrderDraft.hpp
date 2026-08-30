#ifndef OCO_ORDER_DRAFT_H
#define OCO_ORDER_DRAFT_H

#include "ExchangerType.hpp"
#include "common/domain/OrderOperation.hpp"
#include "common/domain/TradablePair.hpp"

#include <boost/decimal.hpp>

#include <optional>
#include <string>

using Decimal = boost::decimal::decimal128_t;

struct OcoOrderDraft
{
    ExchangerType exchangerType = ExchangerType::BINANCE;
    TradablePair pair;
    OrderOperation side = OrderOperation::BUY;
    Decimal quantity{};
    Decimal limitPrice{};
    Decimal stopPrice{};
    std::optional<Decimal> stopLimitPrice;
    std::optional<std::string> stopLimitTimeInForce;
    std::string quantityText;
    std::string limitPriceText;
    std::string stopPriceText;
    std::optional<std::string> stopLimitPriceText;
};

#endif
