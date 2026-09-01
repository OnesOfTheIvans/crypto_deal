#include "OperationChainBuilder.hpp"

#include "OperationFactory.hpp"

#include <memory>
#include <utility>
#include <vector>

using namespace std;

unique_ptr<OperationChain> OperationChainBuilder::build(const OperationChainDefinition &definition,
                                                        const vector<Exchanger> &exchangers) const
{
    const OperationFactory operationFactory;
    vector<operation> operations;
    operations.reserve(definition.getOperations().size());

    for (const auto &operationDefinition : definition.getOperations())
    {
        operations.push_back(operationFactory.create(operationDefinition.getType(), operationDefinition.getConfig()));
    }

    return make_unique<OperationChain>(definition, move(operations), exchangers);
}
