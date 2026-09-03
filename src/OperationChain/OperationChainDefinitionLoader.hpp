#ifndef OPERATION_CHAIN_DEFINITION_LOADER_H
#define OPERATION_CHAIN_DEFINITION_LOADER_H

#include "OperationChainDefinition.hpp"

#include <filesystem>
#include <vector>

class OperationChainDefinitionLoader
{
  public:
    static std::vector<OperationChainDefinition> load(const std::filesystem::path &filePath);
};

#endif
