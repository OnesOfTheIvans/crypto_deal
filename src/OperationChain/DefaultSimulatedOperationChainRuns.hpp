#ifndef DEFAULT_SIMULATED_OPERATION_CHAIN_RUNS_H
#define DEFAULT_SIMULATED_OPERATION_CHAIN_RUNS_H

#include "SimulatedOperationChainRunPlan.hpp"

#include <chrono>
#include <vector>

std::vector<SimulatedOperationChainRunPlan>
createDefaultSimulatedOperationChainRunPlans(std::chrono::milliseconds stepDelay = std::chrono::milliseconds{250},
                                             std::chrono::milliseconds startStagger = std::chrono::milliseconds{150});

#endif
