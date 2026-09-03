#include "SimulatedOperationChainRun.hpp"

#include "common/exception_handling.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <mutex>
#include <stop_token>
#include <utility>

using namespace exception_handling;
using namespace std;

SimulatedOperationChainRun::SimulatedOperationChainRun(SimulatedOperationChainRunPlan plan, OperationChainClock clock)
    : plan(move(plan)), clock(move(clock))
{
    const vector<OperationDefinition> &operations = this->plan.definition.getOperations();
    throwIf(!this->clock, "Simulated operation-chain run requires a clock");
    throwIf(this->plan.awaitingStepIndex >= operations.size(),
            "Simulated operation-chain Awaiting step index is out of range");
    throwIf(this->plan.succeededStepContexts.size() != this->plan.awaitingStepIndex,
            "Simulated operation-chain run requires one output context per Succeeded step");
    throwIf(this->plan.acceptedIdentifiers.size() != operations.size(),
            "Simulated operation-chain run requires identifier data for every step");
    throwIf(this->plan.startDelay < chrono::milliseconds{0},
            "Simulated operation-chain run requires a non-negative start delay");
    throwIf(this->plan.stepDelay < chrono::milliseconds{0},
            "Simulated operation-chain run requires a non-negative step delay");

    snapshot.definitionName = this->plan.definition.getName();
    snapshot.initialContext = {this->plan.definition.getInitialExchangerType(),
                               this->plan.definition.getInitialAsset(),
                               this->plan.definition.getInitialQuantity()};
    snapshot.currentContext = snapshot.initialContext;
    snapshot.createdAt = this->clock();
    snapshot.updatedAt = snapshot.createdAt;
    snapshot.steps.reserve(operations.size());
    for (size_t index = 0; index < operations.size(); ++index)
    {
        OperationStepSnapshot step;
        step.index = index;
        step.type = operations[index].getType();
        step.config = operations[index].getConfig();
        snapshot.steps.push_back(move(step));
    }
}

bool SimulatedOperationChainRun::isCancellationRequested() const
{
    lock_guard<mutex> lock(controlMutex);
    return cancellationRequested;
}

bool SimulatedOperationChainRun::waitForDelay(chrono::milliseconds delay, stop_token stopToken)
{
    unique_lock<mutex> lock(controlMutex);
    const bool cancelled = controlChanged.wait_for(lock, stopToken, delay, [this]() { return cancellationRequested; });
    return !cancelled && !stopToken.stop_requested();
}

bool SimulatedOperationChainRun::waitForCancellation(stop_token stopToken)
{
    unique_lock<mutex> lock(controlMutex);
    return controlChanged.wait(lock, stopToken, [this]() { return cancellationRequested; });
}

void SimulatedOperationChainRun::startExecution()
{
    lock_guard<mutex> lock(snapshotMutex);
    throwIf(snapshot.status != OperationChainStatus::PENDING, "Simulated operation chain can only be executed once");

    const OperationChainTimePoint timePoint = clock();
    snapshot.status = OperationChainStatus::RUNNING;
    snapshot.startedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void SimulatedOperationChainRun::startStep(size_t stepIndex)
{
    lock_guard<mutex> lock(snapshotMutex);
    throwIf(snapshot.status != OperationChainStatus::RUNNING, "Simulated operation chain is not running");
    OperationStepSnapshot &step = snapshot.steps.at(stepIndex);
    throwIf(step.status != OperationStepStatus::PENDING, "Simulated operation-chain step is not pending");

    const OperationChainTimePoint timePoint = clock();
    snapshot.currentStepIndex = stepIndex;
    step.status = OperationStepStatus::RUNNING;
    step.inputContext = snapshot.currentContext;
    step.startedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void SimulatedOperationChainRun::finishStep(size_t stepIndex)
{
    lock_guard<mutex> lock(snapshotMutex);
    OperationStepSnapshot &step = snapshot.steps.at(stepIndex);
    throwIf(step.status != OperationStepStatus::RUNNING, "Simulated operation-chain step is not running");

    const OperationChainTimePoint timePoint = clock();
    snapshot.currentContext = plan.succeededStepContexts.at(stepIndex);
    step.status = OperationStepStatus::SUCCEEDED;
    step.outputContext = snapshot.currentContext;
    step.acceptedIdentifiers = plan.acceptedIdentifiers.at(stepIndex);
    step.finishedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void SimulatedOperationChainRun::markStepAwaiting(size_t stepIndex)
{
    lock_guard<mutex> lock(snapshotMutex);
    OperationStepSnapshot &step = snapshot.steps.at(stepIndex);
    throwIf(step.status != OperationStepStatus::RUNNING, "Simulated operation-chain step is not running");

    step.status = OperationStepStatus::AWAITING;
    step.acceptedIdentifiers = plan.acceptedIdentifiers.at(stepIndex);
    updateSnapshotTime(clock());
}

void SimulatedOperationChainRun::cancelPendingExecution()
{
    lock_guard<mutex> lock(snapshotMutex);
    throwIf(snapshot.status != OperationChainStatus::PENDING, "Pending simulated operation chain is not cancellable");

    const OperationChainTimePoint timePoint = clock();
    snapshot.status = OperationChainStatus::CANCELLED;
    snapshot.currentStepIndex.reset();
    snapshot.finishedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void SimulatedOperationChainRun::cancelExecutionBetweenSteps()
{
    lock_guard<mutex> lock(snapshotMutex);
    throwIf(snapshot.status != OperationChainStatus::RUNNING, "Running simulated operation chain is not cancellable");

    const OperationChainTimePoint timePoint = clock();
    snapshot.status = OperationChainStatus::CANCELLED;
    snapshot.currentStepIndex.reset();
    snapshot.finishedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void SimulatedOperationChainRun::cancelStepAndExecution(size_t stepIndex)
{
    lock_guard<mutex> lock(snapshotMutex);
    OperationStepSnapshot &step = snapshot.steps.at(stepIndex);
    throwIf(step.status != OperationStepStatus::RUNNING && step.status != OperationStepStatus::AWAITING,
            "Simulated operation-chain step is not cancellable");

    const OperationChainTimePoint timePoint = clock();
    step.status = OperationStepStatus::CANCELLED;
    step.finishedAt = timePoint;
    snapshot.status = OperationChainStatus::CANCELLED;
    snapshot.currentStepIndex.reset();
    snapshot.finishedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void SimulatedOperationChainRun::updateSnapshotTime(OperationChainTimePoint timePoint)
{
    snapshot.updatedAt = timePoint;
    ++snapshot.revision;
}

void SimulatedOperationChainRun::notifyStateChanged(const OperationChainStateChangeHandler &stateChangeHandler) const
{
    if (!stateChangeHandler)
    {
        return;
    }

    try
    {
        stateChangeHandler(getSnapshot());
    }
    catch (const exception &exception)
    {
        cerr << "Simulated operation-chain state observer failed: " << exception.what() << '\n';
    }
    catch (...)
    {
        cerr << "Simulated operation-chain state observer failed with a non-standard exception\n";
    }
}

OperationChainSnapshot SimulatedOperationChainRun::getSnapshot() const
{
    lock_guard<mutex> lock(snapshotMutex);
    return snapshot;
}

void SimulatedOperationChainRun::requestCancellation()
{
    {
        lock_guard<mutex> lock(controlMutex);
        cancellationRequested = true;
    }
    controlChanged.notify_all();
}

void SimulatedOperationChainRun::execute(stop_token stopToken,
                                         const OperationChainStateChangeHandler &stateChangeHandler)
{
    if (!waitForDelay(plan.startDelay, stopToken))
    {
        if (isCancellationRequested())
        {
            cancelPendingExecution();
            notifyStateChanged(stateChangeHandler);
        }
        return;
    }

    startExecution();
    notifyStateChanged(stateChangeHandler);
    for (size_t stepIndex = 0; stepIndex <= plan.awaitingStepIndex; ++stepIndex)
    {
        if (stopToken.stop_requested())
        {
            return;
        }
        if (isCancellationRequested())
        {
            cancelExecutionBetweenSteps();
            notifyStateChanged(stateChangeHandler);
            return;
        }

        startStep(stepIndex);
        notifyStateChanged(stateChangeHandler);
        if (!waitForDelay(plan.stepDelay, stopToken))
        {
            if (isCancellationRequested())
            {
                cancelStepAndExecution(stepIndex);
                notifyStateChanged(stateChangeHandler);
            }
            return;
        }

        if (stepIndex < plan.awaitingStepIndex)
        {
            finishStep(stepIndex);
            notifyStateChanged(stateChangeHandler);
            continue;
        }

        markStepAwaiting(stepIndex);
        notifyStateChanged(stateChangeHandler);
        if (waitForCancellation(stopToken))
        {
            cancelStepAndExecution(stepIndex);
            notifyStateChanged(stateChangeHandler);
        }
        return;
    }
}
