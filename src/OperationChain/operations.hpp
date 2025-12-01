#ifndef OPERATIONS_H
#define OPERATIONS_H

#include "OperationContext.hpp"

#include <boost/container/flat_map.hpp>

#include <functional>

using operation = std::function<OperationContext(OperationContext)>;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;

flat_map<std::string, operation> allOperations{{"first",
                                                [](OperationContext context)
                                                {
                                                    // code
                                                    return OperationContext();
                                                }},
                                               {"second",
                                                [](OperationContext context)
                                                {
                                                    // code
                                                    return OperationContext();
                                                }}};

#endif