#ifndef OPERATION_STEP_SNAPSHOT_H
#define OPERATION_STEP_SNAPSHOT_H

#include "OperationAcceptedIdentifiers.hpp"
#include "OperationContextSnapshot.hpp"
#include "OperationStepStatus.hpp"
#include "OperationType.hpp"
#include "config.hpp"
#include "type_aliasing.hpp"

#include <cstddef>
#include <optional>
#include <string>

struct OperationStepSnapshot
{
    std::size_t index = 0;
    OperationType type = OperationType::BUY_CRYPTO;
    Config config;
    OperationStepStatus status = OperationStepStatus::PENDING;
    std::optional<OperationContextSnapshot> inputContext;
    std::optional<OperationContextSnapshot> outputContext;
    OperationAcceptedIdentifiers acceptedIdentifiers;
    std::optional<OperationChainTimePoint> startedAt;
    std::optional<OperationChainTimePoint> finishedAt;
    std::string error;
};

#endif
