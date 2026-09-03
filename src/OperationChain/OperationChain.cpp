#include "OperationChain.hpp"

#include "OperationCancellationRequested.hpp"
#include "common/OrderWaitInterrupted.hpp"
#include "common/exception_handling.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

using namespace exception_handling;
using namespace std;

OperationChain::OperationChain(const OperationChainDefinition &definition,
                               vector<operation> operations,
                               const vector<Exchanger> &exchangers,
                               OperationChainClock clock,
                               shared_ptr<OperationCancellationCoordinator> cancellationCoordinator)
    : context(exchangers), operations(move(operations)), clock(move(clock)),
      cancellationCoordinator(cancellationCoordinator == nullptr ? make_shared<OperationCancellationCoordinator>()
                                                                 : move(cancellationCoordinator))
{
    throwIf(this->operations.size() != definition.getOperations().size(),
            "Operation-chain definition and executable operation counts do not match");
    throwIf(!this->clock, "Operation chain requires a clock");
    context.cancellationCoordinator = this->cancellationCoordinator.get();

    context.exchangerType = definition.getInitialExchangerType();
    context.inAsset = definition.getInitialAsset();
    context.quantity = definition.getInitialQuantity();

    snapshot.definitionName = definition.getName();
    snapshot.initialContext = createContextSnapshot(context);
    snapshot.currentContext = snapshot.initialContext;
    snapshot.createdAt = this->clock();
    snapshot.updatedAt = snapshot.createdAt;
    snapshot.steps.reserve(definition.getOperations().size());
    for (size_t index = 0; index < definition.getOperations().size(); ++index)
    {
        const OperationDefinition &operationDefinition = definition.getOperations()[index];
        OperationStepSnapshot step;
        step.index = index;
        step.type = operationDefinition.getType();
        step.config = operationDefinition.getConfig();
        snapshot.steps.push_back(move(step));
    }
}

OperationContextSnapshot OperationChain::createContextSnapshot(const OperationContext &context)
{
    return {context.exchangerType, context.inAsset, context.quantity};
}

void OperationChain::startExecution()
{
    lock_guard<mutex> lock(snapshotMutex);
    throwIf(snapshot.status != OperationChainStatus::PENDING, "Operation chain can only be executed once");

    const OperationChainTimePoint timePoint = clock();
    snapshot.status = OperationChainStatus::RUNNING;
    snapshot.startedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void OperationChain::startStep(size_t stepIndex)
{
    lock_guard<mutex> lock(snapshotMutex);
    throwIf(snapshot.status != OperationChainStatus::RUNNING, "Operation chain is not running");
    throwIf(stepIndex >= snapshot.steps.size(), "Operation-chain step index is out of range");

    OperationStepSnapshot &step = snapshot.steps[stepIndex];
    throwIf(step.status != OperationStepStatus::PENDING, "Operation-chain step is not pending");

    const OperationChainTimePoint timePoint = clock();
    snapshot.currentStepIndex = stepIndex;
    step.status = OperationStepStatus::RUNNING;
    step.inputContext = createContextSnapshot(context);
    step.startedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void OperationChain::markStepAwaiting(size_t stepIndex, OperationAcceptedIdentifiers identifiers)
{
    lock_guard<mutex> lock(snapshotMutex);
    throwIf(snapshot.status != OperationChainStatus::RUNNING, "Operation chain is not running");
    throwIf(!snapshot.currentStepIndex.has_value() || snapshot.currentStepIndex.value() != stepIndex,
            "Operation-chain progress does not match the current step");

    OperationStepSnapshot &step = snapshot.steps[stepIndex];
    throwIf(step.status != OperationStepStatus::RUNNING, "Operation-chain step is not running");

    step.status = OperationStepStatus::AWAITING;
    step.acceptedIdentifiers = move(identifiers);
    updateSnapshotTime(clock());
}

void OperationChain::finishStep(size_t stepIndex)
{
    lock_guard<mutex> lock(snapshotMutex);
    throwIf(snapshot.status != OperationChainStatus::RUNNING, "Operation chain is not running");
    throwIf(!snapshot.currentStepIndex.has_value() || snapshot.currentStepIndex.value() != stepIndex,
            "Operation-chain completion does not match the current step");

    OperationStepSnapshot &step = snapshot.steps[stepIndex];
    throwIf(step.status != OperationStepStatus::RUNNING && step.status != OperationStepStatus::AWAITING,
            "Operation-chain step is not active");

    const OperationChainTimePoint timePoint = clock();
    snapshot.currentContext = createContextSnapshot(context);
    step.status = OperationStepStatus::SUCCEEDED;
    step.outputContext = snapshot.currentContext;
    step.finishedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void OperationChain::finishExecution()
{
    lock_guard<mutex> lock(snapshotMutex);
    throwIf(snapshot.status != OperationChainStatus::RUNNING, "Operation chain is not running");

    const OperationChainTimePoint timePoint = clock();
    snapshot.status = OperationChainStatus::COMPLETED;
    snapshot.currentStepIndex.reset();
    snapshot.finishedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void OperationChain::cancelPendingExecution()
{
    lock_guard<mutex> lock(snapshotMutex);
    throwIf(snapshot.status != OperationChainStatus::PENDING, "Pending operation chain is not cancellable");

    const OperationChainTimePoint timePoint = clock();
    snapshot.status = OperationChainStatus::CANCELLED;
    snapshot.currentStepIndex.reset();
    snapshot.finishedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void OperationChain::cancelExecutionBetweenSteps()
{
    lock_guard<mutex> lock(snapshotMutex);
    throwIf(snapshot.status != OperationChainStatus::RUNNING, "Running operation chain is not cancellable");

    const OperationChainTimePoint timePoint = clock();
    snapshot.status = OperationChainStatus::CANCELLED;
    snapshot.currentStepIndex.reset();
    snapshot.finishedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void OperationChain::cancelStepAndExecution(size_t stepIndex)
{
    lock_guard<mutex> lock(snapshotMutex);
    throwIf(snapshot.status != OperationChainStatus::RUNNING, "Running operation chain is not cancellable");
    OperationStepSnapshot &step = snapshot.steps.at(stepIndex);
    throwIf(step.status != OperationStepStatus::RUNNING && step.status != OperationStepStatus::AWAITING,
            "Operation-chain step is not cancellable");

    const OperationChainTimePoint timePoint = clock();
    step.status = OperationStepStatus::CANCELLED;
    step.finishedAt = timePoint;
    snapshot.status = OperationChainStatus::CANCELLED;
    snapshot.currentContext = createContextSnapshot(context);
    snapshot.currentStepIndex.reset();
    snapshot.finishedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void OperationChain::failExecution(size_t stepIndex, const string &error)
{
    lock_guard<mutex> lock(snapshotMutex);
    OperationStepSnapshot &step = snapshot.steps.at(stepIndex);
    const OperationChainTimePoint timePoint = clock();
    step.status = OperationStepStatus::FAILED;
    step.finishedAt = timePoint;
    step.error = error;
    snapshot.status = OperationChainStatus::FAILED;
    snapshot.currentContext = createContextSnapshot(context);
    snapshot.currentStepIndex.reset();
    snapshot.error = error;
    snapshot.finishedAt = timePoint;
    updateSnapshotTime(timePoint);
}

void OperationChain::updateSnapshotTime(OperationChainTimePoint timePoint)
{
    snapshot.updatedAt = timePoint;
    ++snapshot.revision;
}

void OperationChain::notifyStateChanged(const OperationChainStateChangeHandler &stateChangeHandler) const
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
        cerr << "Operation-chain state observer failed: " << exception.what() << '\n';
    }
    catch (...)
    {
        cerr << "Operation-chain state observer failed with a non-standard exception\n";
    }
}

OperationChainSnapshot OperationChain::getSnapshot() const
{
    lock_guard<mutex> lock(snapshotMutex);
    return snapshot;
}

void OperationChain::execute(const OperationChainStateChangeHandler &stateChangeHandler)
{
    if (cancellationCoordinator->isCancellationRequested())
    {
        cancelPendingExecution();
        cancellationCoordinator->finishChain();
        notifyStateChanged(stateChangeHandler);
        return;
    }

    startExecution();
    notifyStateChanged(stateChangeHandler);

    for (size_t stepIndex = 0; stepIndex < operations.size(); ++stepIndex)
    {
        if (cancellationCoordinator->isCancellationRequested())
        {
            cancelExecutionBetweenSteps();
            cancellationCoordinator->finishChain();
            notifyStateChanged(stateChangeHandler);
            return;
        }

        startStep(stepIndex);
        notifyStateChanged(stateChangeHandler);
        cancellationCoordinator->startOperation(snapshot.steps[stepIndex].type != OperationType::SEND_TO);
        try
        {
            operations[stepIndex](context,
                                  [this, stepIndex, &stateChangeHandler](OperationAcceptedIdentifiers identifiers)
                                  {
                                      markStepAwaiting(stepIndex, move(identifiers));
                                      notifyStateChanged(stateChangeHandler);
                                  });
        }
        catch (const OperationCancellationRequested &)
        {
            cancellationCoordinator->finishOperation();
            cancelStepAndExecution(stepIndex);
            cancellationCoordinator->finishChain();
            notifyStateChanged(stateChangeHandler);
            return;
        }
        catch (const OrderWaitInterrupted &exception)
        {
            cancellationCoordinator->finishOperation();
            if (cancellationCoordinator->isCancellationConfirmed())
            {
                cancelStepAndExecution(stepIndex);
                cancellationCoordinator->finishChain();
                notifyStateChanged(stateChangeHandler);
                return;
            }

            const string error = cancellationCoordinator->hasCancellationFailed()
                                     ? cancellationCoordinator->getCancellationFailure()
                                     : string(exception.what());
            failExecution(stepIndex, error);
            cancellationCoordinator->finishChain();
            notifyStateChanged(stateChangeHandler);
            throw runtime_error(error);
        }
        catch (const exception &exception)
        {
            cancellationCoordinator->finishOperation();
            cancellationCoordinator->waitForCancellationResolution();
            if (cancellationCoordinator->isCancellationConfirmed())
            {
                cancelStepAndExecution(stepIndex);
                cancellationCoordinator->finishChain();
                notifyStateChanged(stateChangeHandler);
                return;
            }

            const string error = cancellationCoordinator->hasCancellationFailed()
                                     ? cancellationCoordinator->getCancellationFailure()
                                     : string(exception.what());
            failExecution(stepIndex, error);
            cancellationCoordinator->finishChain();
            notifyStateChanged(stateChangeHandler);
            if (cancellationCoordinator->hasCancellationFailed())
            {
                throw runtime_error(error);
            }
            throw;
        }
        catch (...)
        {
            cancellationCoordinator->finishOperation();
            failExecution(stepIndex, "Operation failed with a non-standard exception");
            cancellationCoordinator->finishChain();
            notifyStateChanged(stateChangeHandler);
            throw;
        }

        cancellationCoordinator->finishOperation();
        finishStep(stepIndex);
        notifyStateChanged(stateChangeHandler);

        if (cancellationCoordinator->isCancellationRequested() && stepIndex + 1 < operations.size())
        {
            cancelExecutionBetweenSteps();
            cancellationCoordinator->finishChain();
            notifyStateChanged(stateChangeHandler);
            return;
        }
    }

    finishExecution();
    cancellationCoordinator->finishChain();
    notifyStateChanged(stateChangeHandler);
}
