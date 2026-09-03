#ifndef OPERATION_CHAIN_BUILDER_H
#define OPERATION_CHAIN_BUILDER_H

#include "OperationCancellationCoordinator.hpp"
#include "OperationChain.hpp"
#include "OperationChainDefinition.hpp"

#include <memory>
#include <vector>

class OperationChainBuilder
{
  public:
    std::unique_ptr<OperationChain>
    build(const OperationChainDefinition &definition,
          const std::vector<Exchanger> &exchangers,
          std::shared_ptr<OperationCancellationCoordinator> cancellationCoordinator = {}) const;
};

#endif
