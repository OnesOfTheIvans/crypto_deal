#ifndef OPERATION_CONTEXT_H
#define OPERATION_CONTEXT_H

#include "ExchangerPull.hpp"
#include "ExchangerType.hpp"
#include "common/domain/OrderOperation.hpp"

#include <boost/decimal.hpp>

#include <optional>
#include <string>

using Decimal = boost::decimal::decimal128_t;

struct OperationContext
{
    ExchangerType exchangerType;
    const ExchangerPull exchangersPull;
    std::string inAsset;
    Decimal quantity{};

    OperationContext(const std::vector<Exchanger> &exchangers) : exchangersPull(exchangers) {}
};

#endif
