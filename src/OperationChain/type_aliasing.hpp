#ifndef OPERATION_CHAIN_TYPE_ALIASING_H
#define OPERATION_CHAIN_TYPE_ALIASING_H

#include "DealService.hpp"

#include <boost/container/flat_map.hpp>

#include <chrono>
#include <cstdint>
#include <memory>

using Exchanger = std::shared_ptr<DealService>;
using OperationChainRunId = std::uint64_t;
using OperationChainTimePoint = std::chrono::system_clock::time_point;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;

#endif
