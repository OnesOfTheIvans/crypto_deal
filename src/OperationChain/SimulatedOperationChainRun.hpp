#ifndef SIMULATED_OPERATION_CHAIN_RUN_H
#define SIMULATED_OPERATION_CHAIN_RUN_H

#include "OperationChain.hpp"
#include "SimulatedOperationChainRunPlan.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <stop_token>

class SimulatedOperationChainRun
{
  private:
    const SimulatedOperationChainRunPlan plan;
    OperationChainClock clock;
    mutable std::mutex snapshotMutex;
    OperationChainSnapshot snapshot;
    mutable std::mutex controlMutex;
    std::condition_variable_any controlChanged;
    bool cancellationRequested = false;

    bool isCancellationRequested() const;

    bool waitForDelay(std::chrono::milliseconds delay, std::stop_token stopToken);

    bool waitForCancellation(std::stop_token stopToken);

    void startExecution();

    void startStep(std::size_t stepIndex);

    void finishStep(std::size_t stepIndex);

    void markStepAwaiting(std::size_t stepIndex);

    void cancelPendingExecution();

    void cancelExecutionBetweenSteps();

    void cancelStepAndExecution(std::size_t stepIndex);

    void updateSnapshotTime(OperationChainTimePoint timePoint);

    void notifyStateChanged(const OperationChainStateChangeHandler &stateChangeHandler) const;

  public:
    explicit SimulatedOperationChainRun(
        SimulatedOperationChainRunPlan plan,
        OperationChainClock clock = []() { return std::chrono::system_clock::now(); });

    OperationChainSnapshot getSnapshot() const;

    void requestCancellation();

    void execute(std::stop_token stopToken, const OperationChainStateChangeHandler &stateChangeHandler = {});
};

#endif
