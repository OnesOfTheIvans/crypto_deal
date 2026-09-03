#ifndef OPERATION_CHAIN_RUN_INSPECTOR_H
#define OPERATION_CHAIN_RUN_INSPECTOR_H

#include "OperationChainRunSnapshot.hpp"

#include <QString>
#include <QWidget>

#include <cstddef>
#include <optional>
#include <vector>

class QHBoxLayout;
class QLabel;
class QScrollArea;
class QTableWidget;
class QToolButton;

class OperationChainRunInspector final : public QWidget
{
    Q_OBJECT

  private:
    std::optional<OperationChainRunSnapshot> runSnapshot;
    std::optional<std::size_t> selectedStepIndex;
    QLabel *emptyState;
    QWidget *content;
    QLabel *runTitle;
    QLabel *selectionNotice;
    QScrollArea *progressScroll;
    QWidget *progressContent;
    QHBoxLayout *progressLayout;
    QLabel *detailsTitle;
    QTableWidget *detailsTable;
    std::vector<QToolButton *> stepButtons;

    void createLayout();

    void clearProgression();

    void rebuildProgression();

    void updateStepSelection();

    void updateDetails();

    void selectStep(std::size_t stepIndex);

    std::size_t getDefaultStepIndex() const;

    const OperationStepSnapshot *getStep(std::size_t stepIndex) const;

  public:
    explicit OperationChainRunInspector(QWidget *parent = nullptr);

    void setRun(const std::optional<OperationChainRunSnapshot> &snapshot, const QString &notice = {});

    std::optional<OperationChainRunId> getSelectedRunId() const;

    std::optional<std::size_t> getSelectedStepIndex() const;
};

#endif
