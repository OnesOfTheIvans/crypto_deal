#ifndef SIMULATED_OPERATION_CHAIN_RUN_PLAN_H
#define SIMULATED_OPERATION_CHAIN_RUN_PLAN_H

#include "OperationAcceptedIdentifiers.hpp"
#include "OperationChainDefinition.hpp"
#include "OperationContextSnapshot.hpp"

#include <chrono>
#include <cstddef>
#include <vector>

struct SimulatedOperationChainRunPlan
{
    OperationChainDefinition definition;
    std::vector<OperationContextSnapshot> succeededStepContexts;
    std::vector<OperationAcceptedIdentifiers> acceptedIdentifiers;
    std::size_t awaitingStepIndex = 0;
    std::chrono::milliseconds startDelay{};
    std::chrono::milliseconds stepDelay{};
};

#endif
