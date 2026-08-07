#ifndef OPERATION_FACTORY_H
#define OPERATION_FACTORY_H

#include "OperationContext.hpp"
#include "OperationType.hpp"
#include "config.hpp"

#include <boost/container/flat_map.hpp>

#include <functional>

using operation = std::function<void(OperationContext &)>;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;

class OperationFactory
{
  private:
    flat_map<OperationType, std::function<operation(const Config &)>> factories;

  public:
    OperationFactory();

    operation create(const OperationType &type, const Config &config) const;
};

#endif
