#ifndef EXCHANGER_PULL_H
#define EXCHANGER_PULL_H

#include "DealService.hpp"
#include "ExchangerType.hpp"

#include <boost/container/flat_map.hpp>

#include <memory>

using Exchanger = std::shared_ptr<DealService>;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;

class ExchangerPull
{
  private:
    flat_map<ExchangerType, Exchanger> exchangers;

  public:
    void addExchanger(Exchanger &exchanger);

    Exchanger &getExchanger(ExchangerType type);
};

#endif