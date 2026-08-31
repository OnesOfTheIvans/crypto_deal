#ifndef OPERATION_DEFINITION_H
#define OPERATION_DEFINITION_H

#include "OperationType.hpp"
#include "config.hpp"

class OperationDefinition
{
  private:
    OperationType type;
    Config config;

  public:
    OperationDefinition(OperationType type, Config config);

    OperationType getType() const;

    const Config &getConfig() const;
};

#endif
