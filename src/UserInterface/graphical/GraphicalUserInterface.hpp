#ifndef GRAPHICAL_USER_INTERFACE_H
#define GRAPHICAL_USER_INTERFACE_H

#include "OperationChainDefinition.hpp"
#include "UserInterface.hpp"

#include <memory>
#include <vector>

class DealService;

class GraphicalUserInterface final : public UserInterface
{
  private:
    std::shared_ptr<DealService> binanceDealService;
    std::shared_ptr<DealService> bybitDealService;
    const std::vector<OperationChainDefinition> operationChainDefinitions;

  public:
    GraphicalUserInterface(std::shared_ptr<DealService> binanceDealService,
                           std::shared_ptr<DealService> bybitDealService,
                           std::vector<OperationChainDefinition> operationChainDefinitions);

    int run() override;
};

#endif
