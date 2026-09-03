#include "OperationChainRunModel.hpp"

#include "OperationChainRunManager.hpp"

#include <QMetaObject>

#include <exception>
#include <mutex>
#include <utility>

using namespace std;

struct OperationChainRunModel::RunCallbackState
{
    mutex stateMutex;
    OperationChainRunModel *model = nullptr;
};

OperationChainRunModel::OperationChainRunModel(OperationChainRunManager &runManager, QObject *parent)
    : QObject(parent), runManager(runManager), runCallbackState(make_shared<RunCallbackState>())
{
    runCallbackState->model = this;
    attachRunChangeHandler();
    for (OperationChainRunSnapshot snapshot : runManager.getRuns())
    {
        updateRun(move(snapshot));
    }
}

OperationChainRunModel::~OperationChainRunModel()
{
    {
        lock_guard<mutex> lock(runCallbackState->stateMutex);
        runCallbackState->model = nullptr;
    }
    runManager.setRunChangeHandler({});
}

const vector<OperationChainDefinition> &OperationChainRunModel::getDefinitions() const
{
    return runManager.getDefinitions();
}

vector<OperationChainRunSnapshot> OperationChainRunModel::getRuns(OperationChainRunFilter filter) const
{
    vector<OperationChainRunSnapshot> snapshots;
    snapshots.reserve(runs.size());
    for (const auto &run : runs)
    {
        snapshots.push_back(run.second);
    }
    return filterAndSortOperationChainRuns(move(snapshots), filter);
}

optional<OperationChainRunSnapshot> OperationChainRunModel::getRun(OperationChainRunId runId) const
{
    const auto run = runs.find(runId);
    if (run == runs.end())
    {
        return nullopt;
    }
    return run->second;
}

const QString &OperationChainRunModel::getActionError() const
{
    return actionError;
}

bool OperationChainRunModel::startRun(const string &definitionName)
{
    updateActionError({});
    try
    {
        const OperationChainRunId runId = runManager.startRun(definitionName);
        const optional<OperationChainRunSnapshot> snapshot = runManager.getRun(runId);
        if (snapshot.has_value())
        {
            updateRun(snapshot.value());
        }
        return true;
    }
    catch (const exception &exception)
    {
        updateActionError("Unable to start operation chain: " + QString::fromUtf8(exception.what()));
    }
    catch (...)
    {
        updateActionError("Unable to start operation chain: non-standard exception");
    }
    return false;
}

bool OperationChainRunModel::requestRunCancellation(OperationChainRunId runId)
{
    updateActionError({});
    try
    {
        runManager.requestRunCancellation(runId);
        const optional<OperationChainRunSnapshot> snapshot = runManager.getRun(runId);
        if (snapshot.has_value())
        {
            updateRun(snapshot.value());
        }
        return true;
    }
    catch (const exception &exception)
    {
        updateActionError("Unable to cancel operation-chain run: " + QString::fromUtf8(exception.what()));
    }
    catch (...)
    {
        updateActionError("Unable to cancel operation-chain run: non-standard exception");
    }
    return false;
}

void OperationChainRunModel::attachRunChangeHandler()
{
    const shared_ptr<RunCallbackState> callbackState = runCallbackState;
    runManager.setRunChangeHandler(
        [callbackState](OperationChainRunSnapshot snapshot)
        {
            lock_guard<mutex> lock(callbackState->stateMutex);
            OperationChainRunModel *model = callbackState->model;
            if (model != nullptr)
            {
                QMetaObject::invokeMethod(
                    model,
                    [model, snapshot = move(snapshot)]() mutable { model->updateRun(move(snapshot)); },
                    Qt::QueuedConnection);
            }
        });
}

void OperationChainRunModel::updateRun(OperationChainRunSnapshot snapshot)
{
    const auto current = runs.find(snapshot.runId);
    if (current != runs.end() && current->second.updateSequence >= snapshot.updateSequence)
    {
        return;
    }

    runs.insert_or_assign(snapshot.runId, move(snapshot));
    emit runsChanged();
}

void OperationChainRunModel::updateActionError(QString error)
{
    if (actionError == error)
    {
        return;
    }

    actionError = move(error);
    emit actionErrorChanged();
}
