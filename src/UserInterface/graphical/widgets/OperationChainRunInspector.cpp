#include "OperationChainRunInspector.hpp"

#include "common/DecimalConverter.hpp"

#include <QAbstractItemView>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QString>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace std;

namespace {
    constexpr int INSPECTOR_MARGIN = 12;
    constexpr int INSPECTOR_SPACING = 8;
    constexpr int PROGRESSION_SPACING = 6;
    constexpr int STEP_BUTTON_MINIMUM_WIDTH = 132;
    constexpr int STEP_BUTTON_MINIMUM_HEIGHT = 60;
    constexpr int PROGRESSION_MINIMUM_HEIGHT = 82;
    constexpr int DETAILS_ROW_HEIGHT = 28;

    using DetailRows = vector<pair<QString, QString>>;

    QString getExchangeName(ExchangerType exchangerType)
    {
        return exchangerType == ExchangerType::BINANCE ? "Binance" : "Bybit";
    }

    QString getOperationName(OperationType type)
    {
        switch (type)
        {
        case OperationType::BUY_CRYPTO:
            return "Buy crypto";
        case OperationType::SELL_CRYPTO:
            return "Sell crypto";
        case OperationType::PLACE_ORDER:
            return "Custom order";
        case OperationType::PLACE_OCO:
            return "Place OCO";
        case OperationType::SEND_TO:
            return "Send to exchange";
        }
        return "Unknown operation";
    }

    QString getStepStatusName(OperationStepStatus status)
    {
        switch (status)
        {
        case OperationStepStatus::PENDING:
            return "Pending";
        case OperationStepStatus::RUNNING:
            return "Running";
        case OperationStepStatus::AWAITING:
            return "Awaiting";
        case OperationStepStatus::SUCCEEDED:
            return "Succeeded";
        case OperationStepStatus::FAILED:
            return "Failed";
        case OperationStepStatus::CANCELLED:
            return "Cancelled";
        }
        return "Unknown";
    }

    QString getStepStatusKey(OperationStepStatus status)
    {
        return getStepStatusName(status).toLower();
    }

    QString getSideName(OrderOperation side)
    {
        return side == OrderOperation::BUY ? "Buy" : "Sell";
    }

    QString getOrderTypeName(OrderType type)
    {
        return type == OrderType::MARKET ? "Market" : "Limit";
    }

    QString formatDecimal(Decimal value)
    {
        return QString::fromStdString(DecimalConverter::formatDecimal(value));
    }

    QString formatTime(OperationChainTimePoint timePoint)
    {
        const auto milliseconds = chrono::duration_cast<chrono::milliseconds>(timePoint.time_since_epoch()).count();
        return QDateTime::fromMSecsSinceEpoch(milliseconds).toString("yyyy-MM-dd HH:mm:ss");
    }

    QString formatOptionalTime(const optional<OperationChainTimePoint> &timePoint, const QString &missingText)
    {
        return timePoint.has_value() ? formatTime(timePoint.value()) : missingText;
    }

    QString formatOptionalString(const optional<string> &value)
    {
        return value.has_value() ? QString::fromStdString(value.value()) : "Not provided";
    }

    QString formatOptionalDecimal(const optional<Decimal> &value)
    {
        return value.has_value() ? formatDecimal(value.value()) : "Not provided";
    }

    void addConfigurationRows(DetailRows &rows, const BaseConfig &config)
    {
        rows.emplace_back("Configured output asset", QString::fromStdString(config.outAsset));
    }

    void addConfigurationRows(DetailRows &rows, const PlaceOrderConfig &config)
    {
        rows.emplace_back("Configured output asset", QString::fromStdString(config.outAsset));
        rows.emplace_back("Side", getSideName(config.side));
        rows.emplace_back("Order type", getOrderTypeName(config.type));
        rows.emplace_back("Price", config.type == OrderType::LIMIT ? formatDecimal(config.price) : "Not applicable");
        rows.emplace_back("Time in force", formatOptionalString(config.timeInForce));
        rows.emplace_back("Trigger price", formatOptionalString(config.triggerPrice));
        rows.emplace_back("Order filter", formatOptionalString(config.orderFilter));
        rows.emplace_back("Market unit", formatOptionalString(config.marketUnit));
    }

    void addConfigurationRows(DetailRows &rows, const PlaceOcoConfig &config)
    {
        rows.emplace_back("Configured output asset", QString::fromStdString(config.outAsset));
        rows.emplace_back("Side", getSideName(config.side));
        rows.emplace_back("Limit price", formatDecimal(config.price));
        rows.emplace_back("Stop price", formatDecimal(config.stopPrice));
        rows.emplace_back("Stop-limit price", formatOptionalDecimal(config.stopLimitPrice));
        rows.emplace_back("Stop-limit time in force", formatOptionalString(config.stopLimitTimeInForce));
        rows.emplace_back("List client order ID", formatOptionalString(config.listClientOrderId));
        rows.emplace_back("Limit client order ID", formatOptionalString(config.limitClientOrderId));
        rows.emplace_back("Stop client order ID", formatOptionalString(config.stopClientOrderId));
    }

    void addConfigurationRows(DetailRows &rows, const SendToConfig &config)
    {
        rows.emplace_back("Destination exchange", getExchangeName(config.destinationExchanger));
        rows.emplace_back("Transfer chain", QString::fromStdString(config.chain));
        rows.emplace_back("Destination address", QString::fromStdString(config.address));
    }

    void addContextRows(DetailRows &rows, const QString &prefix, const optional<OperationContextSnapshot> &context)
    {
        if (!context.has_value())
        {
            rows.emplace_back(prefix + " exchange", "Not available");
            rows.emplace_back(prefix + " asset", "Not available");
            rows.emplace_back(prefix + " quantity", "Not available");
            return;
        }

        const OperationContextSnapshot &value = context.value();
        rows.emplace_back(prefix + " exchange", getExchangeName(value.exchangerType));
        rows.emplace_back(prefix + " asset", QString::fromStdString(value.asset));
        rows.emplace_back(prefix + " quantity", formatDecimal(value.quantity));
    }

    void addIdentifierRows(DetailRows &rows, const OperationStepSnapshot &step)
    {
        const OperationAcceptedIdentifiers &identifiers = step.acceptedIdentifiers;
        switch (step.type)
        {
        case OperationType::BUY_CRYPTO:
        case OperationType::SELL_CRYPTO:
        case OperationType::PLACE_ORDER:
            rows.emplace_back("Order ID",
                              identifiers.orderId.has_value() ? QString::fromStdString(identifiers.orderId.value())
                                                              : QString("Not available"));
            return;
        case OperationType::PLACE_OCO:
            rows.emplace_back("OCO group ID",
                              identifiers.ocoGroupId.has_value()
                                  ? QString::fromStdString(identifiers.ocoGroupId.value())
                                  : QString("Not available"));
            rows.emplace_back("Take-profit order ID",
                              identifiers.takeProfitOrderId.has_value()
                                  ? QString::fromStdString(identifiers.takeProfitOrderId.value())
                                  : QString("Not available"));
            rows.emplace_back("Stop-loss order ID",
                              identifiers.stopLossOrderId.has_value()
                                  ? QString::fromStdString(identifiers.stopLossOrderId.value())
                                  : QString("Not available"));
            return;
        case OperationType::SEND_TO:
            rows.emplace_back("Exchange identifiers", "None");
            return;
        }
    }

    QTableWidgetItem *createDetailItem(const QString &text)
    {
        auto *item = new QTableWidgetItem(text);
        item->setToolTip(text);
        return item;
    }
}

OperationChainRunInspector::OperationChainRunInspector(QWidget *parent)
    : QWidget(parent), emptyState(nullptr), content(nullptr), runTitle(nullptr), selectionNotice(nullptr),
      progressScroll(nullptr), progressContent(nullptr), progressLayout(nullptr), detailsTitle(nullptr),
      detailsTable(nullptr)
{
    setObjectName("operationChainRunInspector");
    setProperty("operationChainRunInspector", true);
    setMinimumHeight(180);
    createLayout();
    setRun(nullopt);
}

void OperationChainRunInspector::createLayout()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(INSPECTOR_MARGIN, INSPECTOR_MARGIN, INSPECTOR_MARGIN, INSPECTOR_MARGIN);
    layout->setSpacing(INSPECTOR_SPACING);

    emptyState = new QLabel("Select a session run to inspect its steps.", this);
    emptyState->setObjectName("operationChainRunInspectorEmptyState");
    emptyState->setProperty("operationChainEmptyState", true);
    emptyState->setAlignment(Qt::AlignCenter);

    content = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(INSPECTOR_SPACING);

    runTitle = new QLabel(content);
    runTitle->setObjectName("operationChainRunInspectorTitle");
    runTitle->setProperty("operationChainInspectorTitle", true);

    selectionNotice = new QLabel(content);
    selectionNotice->setObjectName("operationChainRunInspectorNotice");
    selectionNotice->setProperty("operationChainInspectorNotice", true);
    selectionNotice->setWordWrap(true);

    auto *progressTitle = new QLabel("Step progression", content);
    progressTitle->setProperty("operationChainInspectorHeading", true);

    progressScroll = new QScrollArea(content);
    progressScroll->setObjectName("operationChainStepProgressScroll");
    progressScroll->setProperty("operationChainStepProgress", true);
    progressScroll->setWidgetResizable(true);
    progressScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    progressScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    progressScroll->setFrameShape(QFrame::NoFrame);
    progressScroll->setMinimumHeight(PROGRESSION_MINIMUM_HEIGHT);

    progressContent = new QWidget(progressScroll);
    progressContent->setObjectName("operationChainStepProgressContent");
    progressContent->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    progressLayout = new QHBoxLayout(progressContent);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(PROGRESSION_SPACING);
    progressLayout->setSizeConstraint(QLayout::SetMinimumSize);
    progressScroll->setWidget(progressContent);

    detailsTitle = new QLabel(content);
    detailsTitle->setObjectName("operationChainStepDetailsTitle");
    detailsTitle->setProperty("operationChainInspectorHeading", true);

    detailsTable = new QTableWidget(content);
    detailsTable->setObjectName("operationChainStepDetailsTable");
    detailsTable->setProperty("operationChainStepDetails", true);
    detailsTable->setColumnCount(2);
    detailsTable->setHorizontalHeaderLabels({"Detail", "Value"});
    detailsTable->setAlternatingRowColors(true);
    detailsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    detailsTable->setSelectionMode(QAbstractItemView::NoSelection);
    detailsTable->setShowGrid(false);
    detailsTable->setWordWrap(false);
    detailsTable->verticalHeader()->hide();
    detailsTable->verticalHeader()->setDefaultSectionSize(DETAILS_ROW_HEIGHT);
    detailsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    detailsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    contentLayout->addWidget(runTitle);
    contentLayout->addWidget(selectionNotice);
    contentLayout->addWidget(progressTitle);
    contentLayout->addWidget(progressScroll);
    contentLayout->addWidget(detailsTitle);
    contentLayout->addWidget(detailsTable, 1);

    layout->addWidget(emptyState, 1);
    layout->addWidget(content, 1);
}

void OperationChainRunInspector::clearProgression()
{
    stepButtons.clear();
    while (QLayoutItem *item = progressLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
}

void OperationChainRunInspector::rebuildProgression()
{
    clearProgression();
    if (!runSnapshot.has_value())
    {
        return;
    }

    const OperationChainSnapshot &snapshot = runSnapshot->chainSnapshot;
    for (size_t position = 0; position < snapshot.steps.size(); ++position)
    {
        const OperationStepSnapshot &step = snapshot.steps[position];
        if (position > 0)
        {
            auto *arrow = new QLabel("→", progressContent);
            arrow->setProperty("operationChainStepArrow", true);
            arrow->setAlignment(Qt::AlignCenter);
            progressLayout->addWidget(arrow);
        }

        auto *button = new QToolButton(progressContent);
        button->setObjectName("operationChainStepButton");
        button->setProperty("operationChainStepButton", true);
        button->setProperty("operationChainStepState", getStepStatusKey(step.status));
        button->setProperty("stepIndex", QVariant::fromValue<qulonglong>(step.index));
        button->setMinimumSize(STEP_BUTTON_MINIMUM_WIDTH, STEP_BUTTON_MINIMUM_HEIGHT);
        button->setCursor(Qt::PointingHandCursor);
        const bool isCurrent = snapshot.currentStepIndex.has_value() && snapshot.currentStepIndex.value() == step.index;
        button->setText(QString::number(static_cast<qulonglong>(step.index + 1)) + " · " + getOperationName(step.type) +
                        "\n" + getStepStatusName(step.status) + (isCurrent ? " · Current" : ""));
        button->setToolTip("Open details for step " + QString::number(static_cast<qulonglong>(step.index + 1)));
        button->setAccessibleName(button->text().replace('\n', ' '));
        connect(button, &QToolButton::clicked, this, [this, stepIndex = step.index]() { selectStep(stepIndex); });
        progressLayout->addWidget(button);
        stepButtons.push_back(button);
    }
    progressLayout->addStretch(1);
    updateStepSelection();
}

void OperationChainRunInspector::updateStepSelection()
{
    if (!runSnapshot.has_value())
    {
        return;
    }

    const OperationChainSnapshot &snapshot = runSnapshot->chainSnapshot;
    for (size_t position = 0; position < snapshot.steps.size() && position < stepButtons.size(); ++position)
    {
        const OperationStepSnapshot &step = snapshot.steps[position];
        QToolButton *button = stepButtons[position];
        const bool isSelected = selectedStepIndex.has_value() && selectedStepIndex.value() == step.index;
        const bool isCurrent = snapshot.currentStepIndex.has_value() && snapshot.currentStepIndex.value() == step.index;
        button->setProperty("operationChainStepSelected", isSelected);
        button->setProperty("operationChainStepCurrent", isCurrent);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
}

void OperationChainRunInspector::updateDetails()
{
    detailsTable->setRowCount(0);
    if (!runSnapshot.has_value() || !selectedStepIndex.has_value())
    {
        detailsTitle->setText("Step details");
        return;
    }

    const OperationStepSnapshot *step = getStep(selectedStepIndex.value());
    if (step == nullptr)
    {
        detailsTitle->setText("Step details");
        return;
    }

    detailsTitle->setText("Step " + QString::number(static_cast<qulonglong>(step->index + 1)) + " details");
    DetailRows rows;
    rows.emplace_back("Operation", getOperationName(step->type));
    rows.emplace_back("State", getStepStatusName(step->status));
    visit([&rows](const auto &config) { addConfigurationRows(rows, config); }, step->config);
    addContextRows(rows, "Input", step->inputContext);
    addContextRows(rows, "Result", step->outputContext);
    rows.emplace_back("Started", formatOptionalTime(step->startedAt, "Not started"));
    rows.emplace_back("Finished", formatOptionalTime(step->finishedAt, "Not finished"));
    addIdentifierRows(rows, *step);
    rows.emplace_back("Error", step->error.empty() ? "None" : QString::fromStdString(step->error));

    detailsTable->setRowCount(static_cast<int>(rows.size()));
    for (size_t row = 0; row < rows.size(); ++row)
    {
        detailsTable->setItem(static_cast<int>(row), 0, createDetailItem(rows[row].first));
        detailsTable->setItem(static_cast<int>(row), 1, createDetailItem(rows[row].second));
    }
}

void OperationChainRunInspector::selectStep(size_t stepIndex)
{
    if (getStep(stepIndex) == nullptr)
    {
        return;
    }

    selectedStepIndex = stepIndex;
    updateStepSelection();
    updateDetails();
}

size_t OperationChainRunInspector::getDefaultStepIndex() const
{
    const OperationChainSnapshot &snapshot = runSnapshot->chainSnapshot;
    if (snapshot.currentStepIndex.has_value() && getStep(snapshot.currentStepIndex.value()) != nullptr)
    {
        return snapshot.currentStepIndex.value();
    }

    for (const OperationStepSnapshot &step : snapshot.steps)
    {
        if (step.status == OperationStepStatus::FAILED || step.status == OperationStepStatus::CANCELLED)
        {
            return step.index;
        }
    }
    for (size_t position = snapshot.steps.size(); position > 0; --position)
    {
        const OperationStepSnapshot &step = snapshot.steps[position - 1];
        if (step.status == OperationStepStatus::SUCCEEDED)
        {
            return step.index;
        }
    }
    return snapshot.steps.front().index;
}

const OperationStepSnapshot *OperationChainRunInspector::getStep(size_t stepIndex) const
{
    if (!runSnapshot.has_value())
    {
        return nullptr;
    }

    for (const OperationStepSnapshot &step : runSnapshot->chainSnapshot.steps)
    {
        if (step.index == stepIndex)
        {
            return &step;
        }
    }
    return nullptr;
}

void OperationChainRunInspector::setRun(const optional<OperationChainRunSnapshot> &snapshot, const QString &notice)
{
    if (!snapshot.has_value())
    {
        runSnapshot.reset();
        selectedStepIndex.reset();
        clearProgression();
        detailsTable->setRowCount(0);
        emptyState->setVisible(true);
        content->setVisible(false);
        return;
    }

    const bool isSameRun = runSnapshot.has_value() && runSnapshot->runId == snapshot->runId;
    runSnapshot = snapshot.value();
    if (runSnapshot->chainSnapshot.steps.empty())
    {
        selectedStepIndex.reset();
    }
    else if (!isSameRun || !selectedStepIndex.has_value() || getStep(selectedStepIndex.value()) == nullptr)
    {
        selectedStepIndex = getDefaultStepIndex();
    }

    emptyState->setVisible(false);
    content->setVisible(true);
    const QString kind = runSnapshot->kind == OperationChainRunKind::SIMULATED ? " · Simulated" : "";
    runTitle->setText(QString::fromStdString(runSnapshot->chainSnapshot.definitionName) + kind + " · Run #" +
                      QString::number(runSnapshot->runId));
    selectionNotice->setText(notice);
    selectionNotice->setVisible(!notice.isEmpty());
    rebuildProgression();
    updateDetails();
}

optional<OperationChainRunId> OperationChainRunInspector::getSelectedRunId() const
{
    if (!runSnapshot.has_value())
    {
        return nullopt;
    }
    return runSnapshot->runId;
}

optional<size_t> OperationChainRunInspector::getSelectedStepIndex() const
{
    return selectedStepIndex;
}
