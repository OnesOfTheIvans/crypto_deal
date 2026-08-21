#include "OperationChain.hpp"

OperationChain::OperationChain(const std::vector<operation> &operations,
                               const std::vector<Exchanger> &exchangers,
                               ExchangerType initExchangerType,
                               const std::string &initInAsset,
                               const Decimal &initQuantity)
    : operations(operations), context(exchangers)
{
    context.exchangerType = initExchangerType;
    context.inAsset = initInAsset;
    context.quantity = initQuantity;
}

void OperationChain::execute()
{
    for (const auto &operation : operations)
    {
        operation(context);
    }
}