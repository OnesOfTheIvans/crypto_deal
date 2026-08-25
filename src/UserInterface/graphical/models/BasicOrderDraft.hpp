#ifndef BASIC_ORDER_DRAFT_H
#define BASIC_ORDER_DRAFT_H

#include "ExchangerType.hpp"
#include "OperationType.hpp"
#include "common/domain/OrderOperation.hpp"
#include "common/domain/OrderType.hpp"
#include "common/domain/TradablePair.hpp"

#include <boost/decimal.hpp>

#include <optional>
#include <string>

using Decimal = boost::decimal::decimal128_t;

struct BasicOrderDraft
{
    ExchangerType exchangerType = ExchangerType::BINANCE;
    OperationType operation = OperationType::BUY_CRYPTO;
    TradablePair pair;
    OrderOperation side = OrderOperation::BUY;
    OrderType type = OrderType::MARKET;
    Decimal quantity{};
    std::optional<Decimal> price;
    std::optional<std::string> timeInForce;
    std::string quantityText;
    std::optional<std::string> priceText;
};

#endif
