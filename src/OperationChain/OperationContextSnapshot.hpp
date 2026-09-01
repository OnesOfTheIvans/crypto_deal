#ifndef OPERATION_CONTEXT_SNAPSHOT_H
#define OPERATION_CONTEXT_SNAPSHOT_H

#include "ExchangerType.hpp"

#include <boost/decimal.hpp>

#include <string>

using Decimal = boost::decimal::decimal128_t;

struct OperationContextSnapshot
{
    ExchangerType exchangerType = ExchangerType::BINANCE;
    std::string asset;
    Decimal quantity{};

    bool operator==(const OperationContextSnapshot &) const = default;
};

#endif
