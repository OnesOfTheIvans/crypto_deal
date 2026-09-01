#ifndef OPERATION_H
#define OPERATION_H

#include "OperationAcceptedIdentifiers.hpp"
#include "OperationContext.hpp"

#include <functional>

using OperationProgressHandler = std::function<void(OperationAcceptedIdentifiers)>;
using operation = std::function<void(OperationContext &, const OperationProgressHandler &)>;

#endif
