#ifndef OPERATION_CHAINS_PAGE_H
#define OPERATION_CHAINS_PAGE_H

#include "OperationChainDefinition.hpp"
#include "OperationChainRunSnapshot.hpp"
#include "graphical/models/OperationChainRunFilter.hpp"

#include <QWidget>

#include <cstddef>
#include <string>

class QLabel;
class QListWidget;
class QTableWidget;
class QToolButton;
class OperationChainRunModel;

class OperationChainsPage final : public QWidget
{
  private:
    OperationChainRunModel &runModel;
    QLabel *actionError;
    QLabel *definitionsEmptyState;
    QLabel *runsEmptyState;
    QToolButton *runFilterToggle;
    QListWidget *runFilterList;
    QTableWidget *definitionsTable;
    QTableWidget *runsTable;

    void createLayout();

    QWidget *createDefinitionsSection();

    QWidget *createRunsSection();

    void populateDefinitions();

    void updateRuns();

    void updateRunFilterToggle();

    void setRunFilterExpanded(bool expanded);

    OperationChainRunFilter getSelectedRunFilter() const;

    void populateRunRow(std::size_t row, const OperationChainRunSnapshot &run);

    void updateActionError();

    bool confirmRunStart(const std::string &definitionName);

    bool confirmRunCancellation(OperationChainRunId runId);

  public:
    explicit OperationChainsPage(OperationChainRunModel &runModel, QWidget *parent = nullptr);
};

#endif
