#include "OperationChainsPage.hpp"

#include "common/DecimalConverter.hpp"
#include "graphical/GuiLayoutConstants.hpp"
#include "graphical/models/OperationChainRunModel.hpp"

#include <QAbstractItemView>
#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

using namespace GuiLayoutConstants;
using namespace std;

namespace {
    constexpr int CHAIN_SECTION_MARGIN = 16;
    constexpr int CHAIN_SECTION_SPACING = 10;
    constexpr int CHAIN_DEFINITIONS_MINIMUM_HEIGHT = 100;
    constexpr int CHAIN_DEFINITIONS_MAXIMUM_HEIGHT = 170;
    constexpr int CHAIN_RUNS_MINIMUM_HEIGHT = 120;
    constexpr int CHAIN_ACTION_MINIMUM_HEIGHT = 32;
    constexpr int CHAIN_TABLE_ROW_HEIGHT = 44;

    enum class DefinitionColumn
    {
        NAME,
        EXCHANGE,
        ASSET,
        QUANTITY,
        STEPS,
        ACTION,
        COUNT
    };

    enum class RunColumn
    {
        RUN,
        STATUS,
        INITIAL_CONTEXT,
        CURRENT_CONTEXT,
        STARTED,
        UPDATED,
        ERROR,
        ACTION,
        COUNT
    };

    QString getExchangeName(ExchangerType exchangerType)
    {
        return exchangerType == ExchangerType::BINANCE ? "Binance" : "Bybit";
    }

    QString formatDecimal(Decimal value)
    {
        return QString::fromStdString(DecimalConverter::formatDecimal(value));
    }

    QString formatContext(const OperationContextSnapshot &context)
    {
        return getExchangeName(context.exchangerType) + "\n" + QString::fromStdString(context.asset) + " · " +
               formatDecimal(context.quantity);
    }

    QString formatRunStatus(const OperationChainRunSnapshot &run)
    {
        QString status;
        switch (run.chainSnapshot.status)
        {
        case OperationChainStatus::PENDING:
            status = "Pending";
            break;
        case OperationChainStatus::RUNNING:
            status = "Running";
            break;
        case OperationChainStatus::COMPLETED:
            status = "Completed";
            break;
        case OperationChainStatus::FAILED:
            status = "Failed";
            break;
        case OperationChainStatus::CANCELLED:
            status = "Cancelled";
            break;
        }

        if (run.cancellationRequested && (run.chainSnapshot.status == OperationChainStatus::PENDING ||
                                          run.chainSnapshot.status == OperationChainStatus::RUNNING))
        {
            status += "\nCancellation requested";
        }
        return status;
    }

    QString formatTime(OperationChainTimePoint timePoint)
    {
        const auto milliseconds = chrono::duration_cast<chrono::milliseconds>(timePoint.time_since_epoch()).count();
        return QDateTime::fromMSecsSinceEpoch(milliseconds).toString("yyyy-MM-dd HH:mm:ss");
    }

    QString formatOptionalTime(const optional<OperationChainTimePoint> &timePoint)
    {
        return timePoint.has_value() ? formatTime(timePoint.value()) : "Not started";
    }

    bool canCancelRun(const OperationChainRunSnapshot &run)
    {
        return run.chainSnapshot.status == OperationChainStatus::PENDING ||
               run.chainSnapshot.status == OperationChainStatus::RUNNING;
    }

    QTableWidgetItem *createTableItem(const QString &text, Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter)
    {
        auto *item = new QTableWidgetItem(text);
        item->setTextAlignment(alignment);
        item->setToolTip(text);
        return item;
    }

    void configureTable(QTableWidget &table)
    {
        table.setAlternatingRowColors(true);
        table.setEditTriggers(QAbstractItemView::NoEditTriggers);
        table.setSelectionBehavior(QAbstractItemView::SelectRows);
        table.setSelectionMode(QAbstractItemView::SingleSelection);
        table.setShowGrid(false);
        table.setWordWrap(false);
        table.verticalHeader()->hide();
        table.verticalHeader()->setDefaultSectionSize(CHAIN_TABLE_ROW_HEIGHT);
    }
}

OperationChainsPage::OperationChainsPage(OperationChainRunModel &runModel, QWidget *parent)
    : QWidget(parent), runModel(runModel), actionError(nullptr), definitionsEmptyState(nullptr),
      runsEmptyState(nullptr), definitionsTable(nullptr), runsTable(nullptr)
{
    setObjectName("operationChainsPage");
    setProperty("primaryPage", true);

    createLayout();
    connect(&runModel, &OperationChainRunModel::runsChanged, this, &OperationChainsPage::updateRuns);
    connect(&runModel, &OperationChainRunModel::actionErrorChanged, this, &OperationChainsPage::updateActionError);
    populateDefinitions();
    updateRuns();
    updateActionError();
}

void OperationChainsPage::createLayout()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(PAGE_HORIZONTAL_MARGIN,
                               PAGE_VERTICAL_MARGIN,
                               PAGE_HORIZONTAL_MARGIN,
                               PAGE_VERTICAL_MARGIN);
    layout->setSpacing(PAGE_LAYOUT_SPACING);

    auto *title = new QLabel("Operation Chains", this);
    title->setProperty("pageTitle", true);

    auto *description = new QLabel("Start reusable trading workflows and monitor every session run.", this);
    description->setProperty("pageDescription", true);

    actionError = new QLabel(this);
    actionError->setObjectName("operationChainActionError");
    actionError->setProperty("operationChainActionError", true);
    actionError->setWordWrap(true);

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(actionError);
    layout->addSpacing(PAGE_PLACEHOLDER_SPACING);
    layout->addWidget(createDefinitionsSection());
    layout->addWidget(createRunsSection(), 1);
}

QWidget *OperationChainsPage::createDefinitionsSection()
{
    auto *section = new QWidget(this);
    section->setObjectName("operationChainDefinitionsSection");
    section->setProperty("operationChainSection", true);

    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(CHAIN_SECTION_MARGIN, CHAIN_SECTION_MARGIN, CHAIN_SECTION_MARGIN, CHAIN_SECTION_MARGIN);
    layout->setSpacing(CHAIN_SECTION_SPACING);

    auto *title = new QLabel("Available chains", section);
    title->setProperty("operationChainSectionTitle", true);

    auto *description = new QLabel("Definitions loaded from operation_chains.json.", section);
    description->setProperty("operationChainSectionDescription", true);

    definitionsEmptyState = new QLabel("No operation-chain definitions are available.", section);
    definitionsEmptyState->setObjectName("operationChainDefinitionsEmptyState");
    definitionsEmptyState->setProperty("operationChainEmptyState", true);
    definitionsEmptyState->setAlignment(Qt::AlignCenter);

    definitionsTable = new QTableWidget(section);
    definitionsTable->setObjectName("operationChainDefinitionsTable");
    definitionsTable->setProperty("operationChainTable", true);
    definitionsTable->setColumnCount(static_cast<int>(DefinitionColumn::COUNT));
    definitionsTable->setHorizontalHeaderLabels({"Name", "Initial exchange", "Asset", "Quantity", "Steps", "Action"});
    definitionsTable->setMinimumHeight(CHAIN_DEFINITIONS_MINIMUM_HEIGHT);
    definitionsTable->setMaximumHeight(CHAIN_DEFINITIONS_MAXIMUM_HEIGHT);
    configureTable(*definitionsTable);
    definitionsTable->setSelectionMode(QAbstractItemView::NoSelection);
    definitionsTable->horizontalHeader()->setSectionResizeMode(static_cast<int>(DefinitionColumn::NAME),
                                                               QHeaderView::Stretch);
    for (const DefinitionColumn column : {DefinitionColumn::EXCHANGE,
                                          DefinitionColumn::ASSET,
                                          DefinitionColumn::QUANTITY,
                                          DefinitionColumn::STEPS,
                                          DefinitionColumn::ACTION})
    {
        definitionsTable->horizontalHeader()->setSectionResizeMode(static_cast<int>(column),
                                                                   QHeaderView::ResizeToContents);
    }

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(definitionsEmptyState);
    layout->addWidget(definitionsTable);
    return section;
}

QWidget *OperationChainsPage::createRunsSection()
{
    auto *section = new QWidget(this);
    section->setObjectName("operationChainRunsSection");
    section->setProperty("operationChainSection", true);

    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(CHAIN_SECTION_MARGIN, CHAIN_SECTION_MARGIN, CHAIN_SECTION_MARGIN, CHAIN_SECTION_MARGIN);
    layout->setSpacing(CHAIN_SECTION_SPACING);

    auto *title = new QLabel("Session runs", section);
    title->setProperty("operationChainSectionTitle", true);

    auto *description = new QLabel("Runs are retained for this application session.", section);
    description->setProperty("operationChainSectionDescription", true);

    runsEmptyState = new QLabel("No operation-chain runs have been started this session.", section);
    runsEmptyState->setObjectName("operationChainRunsEmptyState");
    runsEmptyState->setProperty("operationChainEmptyState", true);
    runsEmptyState->setAlignment(Qt::AlignCenter);

    runsTable = new QTableWidget(section);
    runsTable->setObjectName("operationChainRunsTable");
    runsTable->setProperty("operationChainTable", true);
    runsTable->setColumnCount(static_cast<int>(RunColumn::COUNT));
    runsTable->setHorizontalHeaderLabels(
        {"Run", "Status", "Initial", "Current", "Started", "Updated", "Latest error", "Action"});
    runsTable->setMinimumHeight(CHAIN_RUNS_MINIMUM_HEIGHT);
    configureTable(*runsTable);
    runsTable->horizontalHeader()->setSectionResizeMode(static_cast<int>(RunColumn::RUN), QHeaderView::Stretch);
    runsTable->horizontalHeader()->setSectionResizeMode(static_cast<int>(RunColumn::STATUS),
                                                        QHeaderView::ResizeToContents);
    runsTable->horizontalHeader()->setSectionResizeMode(static_cast<int>(RunColumn::INITIAL_CONTEXT),
                                                        QHeaderView::ResizeToContents);
    runsTable->horizontalHeader()->setSectionResizeMode(static_cast<int>(RunColumn::CURRENT_CONTEXT),
                                                        QHeaderView::ResizeToContents);
    runsTable->horizontalHeader()->setSectionResizeMode(static_cast<int>(RunColumn::STARTED),
                                                        QHeaderView::ResizeToContents);
    runsTable->horizontalHeader()->setSectionResizeMode(static_cast<int>(RunColumn::UPDATED),
                                                        QHeaderView::ResizeToContents);
    runsTable->horizontalHeader()->setSectionResizeMode(static_cast<int>(RunColumn::ERROR), QHeaderView::Stretch);
    runsTable->horizontalHeader()->setSectionResizeMode(static_cast<int>(RunColumn::ACTION),
                                                        QHeaderView::ResizeToContents);

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(runsEmptyState, 1);
    layout->addWidget(runsTable, 1);
    return section;
}

void OperationChainsPage::populateDefinitions()
{
    const vector<OperationChainDefinition> &definitions = runModel.getDefinitions();
    definitionsTable->setRowCount(static_cast<int>(definitions.size()));
    definitionsTable->setVisible(!definitions.empty());
    definitionsEmptyState->setVisible(definitions.empty());

    for (size_t row = 0; row < definitions.size(); ++row)
    {
        const OperationChainDefinition &definition = definitions[row];
        definitionsTable->setItem(static_cast<int>(row),
                                  static_cast<int>(DefinitionColumn::NAME),
                                  createTableItem(QString::fromStdString(definition.getName())));
        definitionsTable->setItem(static_cast<int>(row),
                                  static_cast<int>(DefinitionColumn::EXCHANGE),
                                  createTableItem(getExchangeName(definition.getInitialExchangerType())));
        definitionsTable->setItem(static_cast<int>(row),
                                  static_cast<int>(DefinitionColumn::ASSET),
                                  createTableItem(QString::fromStdString(definition.getInitialAsset())));
        definitionsTable->setItem(static_cast<int>(row),
                                  static_cast<int>(DefinitionColumn::QUANTITY),
                                  createTableItem(formatDecimal(definition.getInitialQuantity()), Qt::AlignRight));
        definitionsTable->setItem(
            static_cast<int>(row),
            static_cast<int>(DefinitionColumn::STEPS),
            createTableItem(QString::number(static_cast<qulonglong>(definition.getOperations().size())),
                            Qt::AlignRight));

        auto *startButton = new QPushButton("Start", definitionsTable);
        startButton->setObjectName("startOperationChainButton");
        startButton->setProperty("primaryChainAction", true);
        startButton->setProperty("definitionName", QString::fromStdString(definition.getName()));
        startButton->setMinimumHeight(CHAIN_ACTION_MINIMUM_HEIGHT);
        startButton->setCursor(Qt::PointingHandCursor);
        connect(startButton,
                &QPushButton::clicked,
                this,
                [this, definitionName = definition.getName()]()
                {
                    if (confirmRunStart(definitionName))
                    {
                        runModel.startRun(definitionName);
                    }
                });
        definitionsTable->setCellWidget(static_cast<int>(row), static_cast<int>(DefinitionColumn::ACTION), startButton);
    }
}

void OperationChainsPage::updateRuns()
{
    const vector<OperationChainRunSnapshot> runs = runModel.getRuns();
    runsTable->setRowCount(0);
    runsTable->setRowCount(static_cast<int>(runs.size()));
    runsTable->setVisible(!runs.empty());
    runsEmptyState->setVisible(runs.empty());

    for (size_t row = 0; row < runs.size(); ++row)
    {
        populateRunRow(row, runs[row]);
    }
}

void OperationChainsPage::populateRunRow(size_t row, const OperationChainRunSnapshot &run)
{
    const int tableRow = static_cast<int>(row);
    const OperationChainSnapshot &snapshot = run.chainSnapshot;
    runsTable->setItem(
        tableRow,
        static_cast<int>(RunColumn::RUN),
        createTableItem(QString::fromStdString(snapshot.definitionName) + "\nRun #" + QString::number(run.runId)));
    runsTable->setItem(tableRow, static_cast<int>(RunColumn::STATUS), createTableItem(formatRunStatus(run)));
    runsTable->setItem(tableRow,
                       static_cast<int>(RunColumn::INITIAL_CONTEXT),
                       createTableItem(formatContext(snapshot.initialContext)));
    runsTable->setItem(tableRow,
                       static_cast<int>(RunColumn::CURRENT_CONTEXT),
                       createTableItem(formatContext(snapshot.currentContext)));
    runsTable->setItem(tableRow,
                       static_cast<int>(RunColumn::STARTED),
                       createTableItem(formatOptionalTime(snapshot.startedAt)));
    runsTable->setItem(tableRow, static_cast<int>(RunColumn::UPDATED), createTableItem(formatTime(snapshot.updatedAt)));
    runsTable->setItem(tableRow,
                       static_cast<int>(RunColumn::ERROR),
                       createTableItem(snapshot.error.empty() ? "—" : QString::fromStdString(snapshot.error)));

    if (!canCancelRun(run))
    {
        runsTable->setItem(tableRow, static_cast<int>(RunColumn::ACTION), createTableItem("—", Qt::AlignCenter));
        return;
    }

    auto *cancelButton = new QPushButton(run.cancellationRequested ? "Stopping..." : "Cancel / Stop", runsTable);
    cancelButton->setObjectName("cancelOperationChainRunButton");
    cancelButton->setProperty("dangerChainAction", true);
    cancelButton->setProperty("runId", QVariant::fromValue<qulonglong>(run.runId));
    cancelButton->setMinimumHeight(CHAIN_ACTION_MINIMUM_HEIGHT);
    cancelButton->setCursor(run.cancellationRequested ? Qt::ArrowCursor : Qt::PointingHandCursor);
    cancelButton->setEnabled(!run.cancellationRequested);
    connect(cancelButton,
            &QPushButton::clicked,
            this,
            [this, runId = run.runId]()
            {
                if (confirmRunCancellation(runId))
                {
                    runModel.requestRunCancellation(runId);
                }
            });
    runsTable->setCellWidget(tableRow, static_cast<int>(RunColumn::ACTION), cancelButton);
}

void OperationChainsPage::updateActionError()
{
    actionError->setText(runModel.getActionError());
    actionError->setVisible(!runModel.getActionError().isEmpty());
}

bool OperationChainsPage::confirmRunStart(const string &definitionName)
{
    QMessageBox confirmation(QMessageBox::Warning,
                             "Start operation chain?",
                             "Start a new run of " + QString::fromStdString(definitionName) + "?",
                             QMessageBox::NoButton,
                             this);
    confirmation.setObjectName("startOperationChainConfirmationDialog");
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setInformativeText("This workflow may place real exchange orders. Review your active configuration "
                                    "before starting it.");
    auto *startButton = confirmation.addButton("Start run", QMessageBox::AcceptRole);
    startButton->setObjectName("confirmOperationChainStartButton");
    auto *keepButton = confirmation.addButton("Keep idle", QMessageBox::RejectRole);
    keepButton->setObjectName("keepOperationChainIdleButton");
    confirmation.setDefaultButton(keepButton);
    confirmation.setEscapeButton(keepButton);
    confirmation.exec();
    return confirmation.clickedButton() == startButton;
}

bool OperationChainsPage::confirmRunCancellation(OperationChainRunId runId)
{
    const optional<OperationChainRunSnapshot> run = runModel.getRun(runId);
    if (!run.has_value() || !canCancelRun(run.value()) || run->cancellationRequested)
    {
        return false;
    }

    QMessageBox confirmation(QMessageBox::Warning,
                             "Cancel / Stop operation-chain run?",
                             "Cancel / Stop " + QString::fromStdString(run->chainSnapshot.definitionName) + " run #" +
                                 QString::number(runId) + "?",
                             QMessageBox::NoButton,
                             this);
    confirmation.setObjectName("cancelOperationChainRunConfirmationDialog");
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setInformativeText("The current exchange order will be cancelled when possible, and later steps "
                                    "will not execute. The run changes to Cancelled only after confirmation.");
    auto *cancelButton = confirmation.addButton("Cancel / Stop run", QMessageBox::AcceptRole);
    cancelButton->setObjectName("confirmOperationChainRunCancellationButton");
    auto *keepButton = confirmation.addButton("Keep running", QMessageBox::RejectRole);
    keepButton->setObjectName("keepOperationChainRunningButton");
    confirmation.setDefaultButton(keepButton);
    confirmation.setEscapeButton(keepButton);
    confirmation.exec();
    return confirmation.clickedButton() == cancelButton;
}
