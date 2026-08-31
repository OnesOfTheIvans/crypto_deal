#include "OperationChainBuilder.hpp"

#include "OperationFactory.hpp"

#include <vector>

using namespace std;

OperationChain OperationChainBuilder::build(const OperationChainDefinition &definition,
                                            const vector<Exchanger> &exchangers) const
{
    const OperationFactory operationFactory;
    vector<operation> operations;
    operations.reserve(definition.getOperations().size());

    for (const auto &operationDefinition : definition.getOperations())
    {
        operations.push_back(operationFactory.create(operationDefinition.getType(), operationDefinition.getConfig()));
    }

    return OperationChain(operations,
                          exchangers,
                          definition.getInitialExchangerType(),
                          definition.getInitialAsset(),
                          definition.getInitialQuantity());
}
