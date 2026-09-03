#ifndef OPERATION_CHAINS_PAGE_H
#define OPERATION_CHAINS_PAGE_H

#include "OperationChainDefinition.hpp"
#include "OperationChainRunSnapshot.hpp"
#include "graphical/models/OperationChainRunFilter.hpp"

#include <QWidget>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

class QLabel;
class QListWidget;
class QTableWidget;
class QToolButton;
class OperationChainRunModel;
class OperationChainRunInspector;

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
    OperationChainRunInspector *runInspector;
    std::optional<OperationChainRunId> selectedRunId;

    void createLayout();

    QWidget *createDefinitionsSection();

    QWidget *createRunsSection();

    void populateDefinitions();

    void updateRuns();

    void selectRunFromTable();

    void updateRunInspector(const std::vector<OperationChainRunSnapshot> &visibleRuns);

    void updateRunFilterToggle();

    void setRunFilterExpanded(bool expanded);

    OperationChainRunFilter getSelectedRunFilter() const;

    std::optional<OperationChainRunId> getSelectedTableRunId() const;

    void populateRunRow(std::size_t row, const OperationChainRunSnapshot &run);

    void updateActionError();

    bool confirmRunStart(const std::string &definitionName);

    bool confirmRunCancellation(OperationChainRunId runId);

  public:
    explicit OperationChainsPage(OperationChainRunModel &runModel, QWidget *parent = nullptr);
};

#endif
