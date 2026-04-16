#ifndef OPERATION_CONTEXT_H
#define OPERATION_CONTEXT_H

#include "ExchangerPull.hpp"
#include "ExchangerType.hpp"

#include <optional>
#include <string>

struct OperationContext
{
    ExchangerType exchangerType;
    const ExchangerPull exchangersPull;
    std::string inAsset;
    std::string previousInAsset;
    std::string side;
    std::optional<std::string> orderId;
    double quantity;

    OperationContext(const std::vector<Exchanger> &exchangers) : exchangersPull(exchangers) {}
};

#endif