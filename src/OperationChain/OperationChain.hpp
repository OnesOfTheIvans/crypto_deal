#ifndef OPERATION_CHAIN_H
#define OPERATION_CHAIN_H

#include "DealService.hpp"
#include "OperationContext.hpp"

#include <boost/container/flat_map.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

using operation = std::function<void(OperationContext &)>;

class OperationChain
{
  private:
    OperationContext context;
    std::vector<operation> operations;

  public:
    OperationChain(const std::vector<operation> &operations,
                   const std::vector<Exchanger> &exchangers,
                   ExchangerType initExchangerType,
                   const std::string &initInAsset,
                   const Decimal &initQuantity);

    void execute();
};

#endif
