#ifndef OPERATION_CONTEXT_H
#define OPERATION_CONTEXT_H

#include "ExchangerPull.hpp"
#include "ExchangerType.hpp"
#include "common/OrderOperation.hpp"
#include "common/type_aliasing.hpp"

#include <optional>
#include <string>

struct OperationContext
{
    ExchangerType exchangerType;
    const ExchangerPull exchangersPull;
    std::string inAsset;
    std::string previousInAsset;
    std::optional<OrderOperation> side;
    std::optional<std::string> orderId;
    Decimal quantity{};

    OperationContext(const std::vector<Exchanger> &exchangers) : exchangersPull(exchangers) {}
};

#endif
