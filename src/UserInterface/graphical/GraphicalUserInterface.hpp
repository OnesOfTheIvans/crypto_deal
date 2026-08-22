#ifndef GRAPHICAL_USER_INTERFACE_H
#define GRAPHICAL_USER_INTERFACE_H

#include "UserInterface.hpp"

#include <memory>

class DealService;

class GraphicalUserInterface final : public UserInterface
{
  private:
    std::shared_ptr<DealService> binanceDealService;
    std::shared_ptr<DealService> bybitDealService;

  public:
    GraphicalUserInterface(std::shared_ptr<DealService> binanceDealService,
                           std::shared_ptr<DealService> bybitDealService);

    int run() override;
};

#endif
