#ifndef OPERATION_CHAIN_SNAPSHOT_H
#define OPERATION_CHAIN_SNAPSHOT_H

#include "OperationChainStatus.hpp"
#include "OperationContextSnapshot.hpp"
#include "OperationStepSnapshot.hpp"
#include "type_aliasing.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct OperationChainSnapshot
{
    std::string definitionName;
    OperationChainStatus status = OperationChainStatus::PENDING;
    OperationContextSnapshot initialContext;
    OperationContextSnapshot currentContext;
    std::optional<std::size_t> currentStepIndex;
    OperationChainTimePoint createdAt;
    std::optional<OperationChainTimePoint> startedAt;
    OperationChainTimePoint updatedAt;
    std::optional<OperationChainTimePoint> finishedAt;
    std::string error;
    std::uint64_t revision = 0;
    std::vector<OperationStepSnapshot> steps;
};

#endif
