#ifndef OPERATION_CHAIN_TYPE_ALIASING_H
#define OPERATION_CHAIN_TYPE_ALIASING_H

#include "DealService.hpp"
#include "OperationContext.hpp"

#include <boost/container/flat_map.hpp>

#include <functional>
#include <memory>

using Exchanger = std::shared_ptr<DealService>;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;
using operation = std::function<OperationContext(OperationContext)>;

#endif