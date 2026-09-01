#ifndef OPERATION_CHAIN_H
#define OPERATION_CHAIN_H

#include "Operation.hpp"
#include "OperationChainDefinition.hpp"
#include "OperationChainSnapshot.hpp"
#include "type_aliasing.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

using OperationChainClock = std::function<OperationChainTimePoint()>;
using OperationChainStateChangeHandler = std::function<void(OperationChainSnapshot)>;

class OperationChain
{
  private:
    OperationContext context;
    std::vector<operation> operations;
    OperationChainClock clock;
    mutable std::mutex snapshotMutex;
    OperationChainSnapshot snapshot;

    static OperationContextSnapshot createContextSnapshot(const OperationContext &context);

    void startExecution();

    void startStep(std::size_t stepIndex);

    void markStepAwaiting(std::size_t stepIndex, OperationAcceptedIdentifiers identifiers);

    void finishStep(std::size_t stepIndex);

    void finishExecution();

    void failExecution(std::size_t stepIndex, const std::string &error);

    void updateSnapshotTime(OperationChainTimePoint timePoint);

    void notifyStateChanged(const OperationChainStateChangeHandler &stateChangeHandler) const;

  public:
    OperationChain(
        const OperationChainDefinition &definition,
        std::vector<operation> operations,
        const std::vector<Exchanger> &exchangers,
        OperationChainClock clock = []() { return std::chrono::system_clock::now(); });

    OperationChainSnapshot getSnapshot() const;

    void execute(const OperationChainStateChangeHandler &stateChangeHandler = {});
};

#endif
