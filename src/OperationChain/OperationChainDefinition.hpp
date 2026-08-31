#ifndef OPERATION_CHAIN_DEFINITION_H
#define OPERATION_CHAIN_DEFINITION_H

#include "ExchangerType.hpp"
#include "OperationDefinition.hpp"

#include <boost/decimal.hpp>

#include <string>
#include <vector>

using Decimal = boost::decimal::decimal128_t;

class OperationChainDefinition
{
  private:
    std::string name;
    ExchangerType initialExchangerType;
    std::string initialAsset;
    Decimal initialQuantity{};
    std::vector<OperationDefinition> operations;

  public:
    OperationChainDefinition(std::string name,
                             ExchangerType initialExchangerType,
                             std::string initialAsset,
                             Decimal initialQuantity,
                             std::vector<OperationDefinition> operations);

    const std::string &getName() const;

    ExchangerType getInitialExchangerType() const;

    const std::string &getInitialAsset() const;

    const Decimal &getInitialQuantity() const;

    const std::vector<OperationDefinition> &getOperations() const;
};

#endif
