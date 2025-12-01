#ifndef OPERATION_CONTEXT_H
#define OPERATION_CONTEXT_H

#include "ExchangerPull.hpp"
#include "ExchangerType.hpp"

struct OperationContext
{
    ExchangerType target;
    ExchangerType destination;
    ExchangerPull exchangersPull;
    double quantity;
};

#endif