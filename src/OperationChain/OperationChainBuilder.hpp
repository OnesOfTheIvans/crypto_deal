#ifndef OPERATION_CHAIN_BUILDER_H
#define OPERATION_CHAIN_BUILDER_H

#include "OperationChain.hpp"
#include "OperationChainDefinition.hpp"

#include <vector>

class OperationChainBuilder
{
  public:
    OperationChain build(const OperationChainDefinition &definition, const std::vector<Exchanger> &exchangers) const;
};

#endif
