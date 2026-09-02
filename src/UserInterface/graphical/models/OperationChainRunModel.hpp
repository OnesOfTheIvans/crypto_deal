#ifndef OPERATION_CHAIN_RUN_MODEL_H
#define OPERATION_CHAIN_RUN_MODEL_H

#include "OperationChainDefinition.hpp"
#include "OperationChainRunSnapshot.hpp"

#include <QObject>
#include <QString>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class OperationChainRunManager;

class OperationChainRunModel final : public QObject
{
    Q_OBJECT

  private:
    struct RunCallbackState;

    OperationChainRunManager &runManager;
    std::map<OperationChainRunId, OperationChainRunSnapshot> runs;
    std::shared_ptr<RunCallbackState> runCallbackState;
    QString actionError;

    void attachRunChangeHandler();

    void updateRun(OperationChainRunSnapshot snapshot);

    void updateActionError(QString error);

  public:
    explicit OperationChainRunModel(OperationChainRunManager &runManager, QObject *parent = nullptr);

    ~OperationChainRunModel() override;

    const std::vector<OperationChainDefinition> &getDefinitions() const;

    std::vector<OperationChainRunSnapshot> getRuns() const;

    std::optional<OperationChainRunSnapshot> getRun(OperationChainRunId runId) const;

    const QString &getActionError() const;

    bool startRun(const std::string &definitionName);

    bool requestRunCancellation(OperationChainRunId runId);

  signals:
    void runsChanged();

    void actionErrorChanged();
};

#endif
