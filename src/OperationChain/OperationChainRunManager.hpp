#ifndef OPERATION_CHAIN_RUN_MANAGER_H
#define OPERATION_CHAIN_RUN_MANAGER_H

#include "OperationChain.hpp"
#include "OperationChainBuilder.hpp"
#include "OperationChainDefinition.hpp"
#include "OperationChainRunSnapshot.hpp"
#include "type_aliasing.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using OperationChainRunChangeHandler = std::function<void(OperationChainRunSnapshot)>;

class OperationChainRunManager
{
  private:
    struct RunRecord
    {
        OperationChainRunSnapshot snapshot;
        std::unique_ptr<OperationChain> chain;
        std::shared_ptr<OperationCancellationCoordinator> cancellationCoordinator;
        std::jthread worker;
        std::jthread cancellationWorker;

        RunRecord(OperationChainRunSnapshot snapshot,
                  std::unique_ptr<OperationChain> chain,
                  std::shared_ptr<OperationCancellationCoordinator> cancellationCoordinator);
    };

    const std::vector<OperationChainDefinition> definitions;
    std::vector<Exchanger> exchangers;
    OperationChainBuilder builder;
    std::map<std::string, std::size_t> definitionIndexes;
    std::map<OperationChainRunId, std::unique_ptr<RunRecord>> runs;
    mutable std::mutex stateMutex;
    OperationChainRunChangeHandler runChangeHandler;
    OperationChainRunId nextRunId;
    std::uint64_t nextUpdateSequence;
    bool stopping;

    const OperationChainDefinition &getDefinition(const std::string &definitionName) const;

    void executeRun(OperationChainRunId runId, OperationChain &chain, std::stop_token stopToken);

    void updateRun(OperationChainRunId runId, OperationChainSnapshot snapshot);

    void cancelRun(const std::shared_ptr<OperationCancellationCoordinator> &cancellationCoordinator,
                   std::stop_token stopToken);

    void notifyRunChanged(const OperationChainRunSnapshot &snapshot) const;

  public:
    OperationChainRunManager(std::vector<OperationChainDefinition> definitions,
                             Exchanger binanceDealService,
                             Exchanger bybitDealService);

    ~OperationChainRunManager();

    void setRunChangeHandler(OperationChainRunChangeHandler handler);

    OperationChainRunId startRun(const std::string &definitionName);

    void requestRunCancellation(OperationChainRunId runId);

    const std::vector<OperationChainDefinition> &getDefinitions() const;

    std::vector<OperationChainRunSnapshot> getRuns() const;

    std::optional<OperationChainRunSnapshot> getRun(OperationChainRunId runId) const;

    void requestStop();

    void stopAndWait();

    bool isStopping() const;
};

#endif
