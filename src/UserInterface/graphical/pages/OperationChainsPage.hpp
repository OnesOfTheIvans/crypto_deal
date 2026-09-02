#ifndef OPERATION_CHAINS_PAGE_H
#define OPERATION_CHAINS_PAGE_H

#include "OperationChainDefinition.hpp"
#include "OperationChainRunSnapshot.hpp"

#include <QWidget>

#include <cstddef>
#include <string>

class QLabel;
class QTableWidget;
class OperationChainRunModel;

class OperationChainsPage final : public QWidget
{
  private:
    OperationChainRunModel &runModel;
    QLabel *actionError;
    QLabel *definitionsEmptyState;
    QLabel *runsEmptyState;
    QTableWidget *definitionsTable;
    QTableWidget *runsTable;

    void createLayout();

    QWidget *createDefinitionsSection();

    QWidget *createRunsSection();

    void populateDefinitions();

    void updateRuns();

    void populateRunRow(std::size_t row, const OperationChainRunSnapshot &run);

    void updateActionError();

    bool confirmRunStart(const std::string &definitionName);

    bool confirmRunCancellation(OperationChainRunId runId);

  public:
    explicit OperationChainsPage(OperationChainRunModel &runModel, QWidget *parent = nullptr);
};

#endif
