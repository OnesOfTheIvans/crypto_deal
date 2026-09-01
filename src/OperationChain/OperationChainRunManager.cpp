#include "OperationChainRunManager.hpp"

#include "common/exception_handling.hpp"

#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>

using namespace exception_handling;
using namespace std;

namespace {
    struct RunStartGate
    {
        mutex gateMutex;
        condition_variable_any condition;
        bool isOpen = false;
    };

    bool waitForRunStart(RunStartGate &gate, stop_token stopToken)
    {
        unique_lock<mutex> lock(gate.gateMutex);
        return gate.condition.wait(lock, stopToken, [&gate]() { return gate.isOpen; });
    }

    void openRunStartGate(RunStartGate &gate)
    {
        {
            lock_guard<mutex> lock(gate.gateMutex);
            gate.isOpen = true;
        }
        gate.condition.notify_all();
    }
}

OperationChainRunManager::RunRecord::RunRecord(OperationChainRunSnapshot snapshot, unique_ptr<OperationChain> chain)
    : snapshot(move(snapshot)), chain(move(chain))
{}

OperationChainRunManager::OperationChainRunManager(vector<OperationChainDefinition> definitions,
                                                   Exchanger binanceDealService,
                                                   Exchanger bybitDealService)
    : definitions(move(definitions)), exchangers{move(binanceDealService), move(bybitDealService)}, nextRunId(1),
      nextUpdateSequence(1), stopping(false)
{
    throwIf(exchangers[0] == nullptr, "Operation-chain run manager requires a Binance deal service");
    throwIf(exchangers[1] == nullptr, "Operation-chain run manager requires a Bybit deal service");
    throwIf(exchangers[0]->getExchangerType() != ExchangerType::BINANCE,
            "Operation-chain run manager received an invalid Binance deal service");
    throwIf(exchangers[1]->getExchangerType() != ExchangerType::BYBIT,
            "Operation-chain run manager received an invalid Bybit deal service");

    for (size_t index = 0; index < this->definitions.size(); ++index)
    {
        const bool inserted = definitionIndexes.emplace(this->definitions[index].getName(), index).second;
        throwIf(!inserted,
                "Operation-chain run manager received duplicate definition name: " +
                    this->definitions[index].getName());
    }
}

OperationChainRunManager::~OperationChainRunManager()
{
    stopAndWait();
}

const OperationChainDefinition &OperationChainRunManager::getDefinition(const string &definitionName) const
{
    const auto definition = definitionIndexes.find(definitionName);
    throwIf(definition == definitionIndexes.end(), "Unknown operation-chain definition: " + definitionName);
    return definitions[definition->second];
}

void OperationChainRunManager::setRunChangeHandler(OperationChainRunChangeHandler handler)
{
    lock_guard<mutex> lock(stateMutex);
    throwIf(stopping && handler, "Operation-chain run manager is stopping");
    runChangeHandler = move(handler);
}

OperationChainRunId OperationChainRunManager::startRun(const string &definitionName)
{
    OperationChainRunSnapshot initialSnapshot;
    auto startGate = make_shared<RunStartGate>();

    {
        lock_guard<mutex> lock(stateMutex);
        throwIf(stopping, "Operation-chain run manager is stopping");
        const OperationChainDefinition &definition = getDefinition(definitionName);
        throwIf(nextRunId == 0, "Operation-chain run ID space is exhausted");
        throwIf(nextUpdateSequence == 0, "Operation-chain update sequence space is exhausted");

        const OperationChainRunId runId = nextRunId++;
        unique_ptr<OperationChain> chain = builder.build(definition, exchangers);
        initialSnapshot.runId = runId;
        initialSnapshot.chainSnapshot = chain->getSnapshot();
        initialSnapshot.updateSequence = nextUpdateSequence++;

        auto record = make_unique<RunRecord>(initialSnapshot, move(chain));
        OperationChain *chainPointer = record->chain.get();
        const auto [run, inserted] = runs.emplace(runId, move(record));
        throwIf(!inserted, "Operation-chain run ID collision");

        try
        {
            run->second->worker = jthread(
                [this, runId, chainPointer, startGate](stop_token stopToken)
                {
                    if (waitForRunStart(*startGate, stopToken))
                    {
                        executeRun(runId, *chainPointer, stopToken);
                    }
                });
        }
        catch (...)
        {
            runs.erase(runId);
            throw;
        }
    }

    notifyRunChanged(initialSnapshot);
    openRunStartGate(*startGate);
    return initialSnapshot.runId;
}

void OperationChainRunManager::executeRun(OperationChainRunId runId, OperationChain &chain, stop_token stopToken)
{
    if (stopToken.stop_requested())
    {
        return;
    }

    try
    {
        chain.execute([this, runId](OperationChainSnapshot snapshot) { updateRun(runId, move(snapshot)); });
    }
    catch (const exception &exception)
    {
        const OperationChainSnapshot snapshot = chain.getSnapshot();
        if (snapshot.status != OperationChainStatus::FAILED)
        {
            cerr << "Operation-chain run " << runId << " failed without a terminal snapshot: " << exception.what()
                 << '\n';
        }
        updateRun(runId, snapshot);
    }
    catch (...)
    {
        const OperationChainSnapshot snapshot = chain.getSnapshot();
        if (snapshot.status != OperationChainStatus::FAILED)
        {
            cerr << "Operation-chain run " << runId << " failed with an unrecorded non-standard exception\n";
        }
        updateRun(runId, snapshot);
    }
}

void OperationChainRunManager::updateRun(OperationChainRunId runId, OperationChainSnapshot snapshot)
{
    OperationChainRunSnapshot changedSnapshot;
    {
        lock_guard<mutex> lock(stateMutex);
        const auto run = runs.find(runId);
        if (run == runs.end())
        {
            cerr << "Operation-chain run update referenced unknown run " << runId << '\n';
            return;
        }
        if (snapshot.revision <= run->second->snapshot.chainSnapshot.revision)
        {
            return;
        }

        throwIf(nextUpdateSequence == 0, "Operation-chain update sequence space is exhausted");
        run->second->snapshot.chainSnapshot = move(snapshot);
        run->second->snapshot.updateSequence = nextUpdateSequence++;
        changedSnapshot = run->second->snapshot;
    }

    notifyRunChanged(changedSnapshot);
}

void OperationChainRunManager::notifyRunChanged(const OperationChainRunSnapshot &snapshot) const
{
    OperationChainRunChangeHandler handler;
    {
        lock_guard<mutex> lock(stateMutex);
        handler = runChangeHandler;
    }
    if (!handler)
    {
        return;
    }

    try
    {
        handler(snapshot);
    }
    catch (const exception &exception)
    {
        cerr << "Operation-chain run observer failed: " << exception.what() << '\n';
    }
    catch (...)
    {
        cerr << "Operation-chain run observer failed with a non-standard exception\n";
    }
}

const vector<OperationChainDefinition> &OperationChainRunManager::getDefinitions() const
{
    return definitions;
}

vector<OperationChainRunSnapshot> OperationChainRunManager::getRuns() const
{
    lock_guard<mutex> lock(stateMutex);
    vector<OperationChainRunSnapshot> snapshots;
    snapshots.reserve(runs.size());
    for (const auto &run : runs)
    {
        snapshots.push_back(run.second->snapshot);
    }
    return snapshots;
}

optional<OperationChainRunSnapshot> OperationChainRunManager::getRun(OperationChainRunId runId) const
{
    lock_guard<mutex> lock(stateMutex);
    const auto run = runs.find(runId);
    if (run == runs.end())
    {
        return nullopt;
    }
    return run->second->snapshot;
}

void OperationChainRunManager::requestStop()
{
    lock_guard<mutex> lock(stateMutex);
    if (stopping)
    {
        return;
    }

    stopping = true;
    runChangeHandler = {};
    for (auto &run : runs)
    {
        run.second->worker.request_stop();
    }
}

void OperationChainRunManager::stopAndWait()
{
    requestStop();

    vector<jthread *> workers;
    {
        lock_guard<mutex> lock(stateMutex);
        workers.reserve(runs.size());
        for (auto &run : runs)
        {
            workers.push_back(&run.second->worker);
        }
    }

    for (jthread *worker : workers)
    {
        if (worker->joinable())
        {
            worker->join();
        }
    }
}

bool OperationChainRunManager::isStopping() const
{
    lock_guard<mutex> lock(stateMutex);
    return stopping;
}
