#include "OperationChainDefinition.hpp"

#include "common/exception_handling.hpp"

#include <utility>

using namespace exception_handling;
using namespace std;

namespace {
    bool isKnownExchangerType(ExchangerType exchangerType)
    {
        return exchangerType == ExchangerType::BINANCE || exchangerType == ExchangerType::BYBIT;
    }
}

OperationChainDefinition::OperationChainDefinition(string name,
                                                   ExchangerType initialExchangerType,
                                                   string initialAsset,
                                                   Decimal initialQuantity,
                                                   vector<OperationDefinition> operations)
    : name(move(name)), initialExchangerType(initialExchangerType), initialAsset(move(initialAsset)),
      initialQuantity(initialQuantity), operations(move(operations))
{
    throwIf(this->name.empty(), "Operation-chain definition requires a name");
    throwIf(!isKnownExchangerType(this->initialExchangerType),
            "Operation-chain definition requires a known initial exchange");
    throwIf(this->initialAsset.empty(), "Operation-chain definition requires an initial asset");
    throwIf(this->initialQuantity <= 0, "Operation-chain definition requires a positive initial quantity");
    throwIf(this->operations.empty(), "Operation-chain definition requires at least one operation");
}

const string &OperationChainDefinition::getName() const
{
    return name;
}

ExchangerType OperationChainDefinition::getInitialExchangerType() const
{
    return initialExchangerType;
}

const string &OperationChainDefinition::getInitialAsset() const
{
    return initialAsset;
}

const Decimal &OperationChainDefinition::getInitialQuantity() const
{
    return initialQuantity;
}

const vector<OperationDefinition> &OperationChainDefinition::getOperations() const
{
    return operations;
}
