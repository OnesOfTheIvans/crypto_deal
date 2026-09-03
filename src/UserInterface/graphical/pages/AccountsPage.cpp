#include "AccountsPage.hpp"

#include "common/DecimalConverter.hpp"
#include "graphical/GuiLayoutConstants.hpp"
#include "graphical/StatusPresentation.hpp"
#include "graphical/async/UiTaskState.hpp"
#include "graphical/models/BalanceCatalog.hpp"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

using namespace GuiLayoutConstants;

namespace {
    constexpr int ACCOUNT_TAB_MARGIN = 18;
    constexpr int ACCOUNT_TAB_SPACING = 12;
    constexpr int ACCOUNT_REFRESH_MINIMUM_HEIGHT = 34;
    constexpr int ACCOUNT_TABLE_MINIMUM_HEIGHT = 260;

    QString getExchangeName(ExchangerType exchangerType)
    {
        return exchangerType == ExchangerType::BINANCE ? "Binance" : "Bybit";
    }

    QString getObjectNamePrefix(ExchangerType exchangerType)
    {
        return exchangerType == ExchangerType::BINANCE ? "binance" : "bybit";
    }

    QString formatBalance(Decimal value)
    {
        return QString::fromStdString(DecimalConverter::formatDecimal(value));
    }

    QTableWidgetItem *createBalanceItem(const QString &text, Qt::Alignment alignment)
    {
        auto *item = new QTableWidgetItem(text);
        item->setTextAlignment(alignment);
        return item;
    }
}

AccountsPage::AccountsPage(BalanceCatalog &balanceCatalog, QWidget *parent)
    : QWidget(parent), balanceCatalog(balanceCatalog), exchangeTabs(nullptr)
{
    setObjectName("accountsPage");
    setProperty("primaryPage", true);

    createLayout();
    connectCatalogUpdates();
    updateExchange(ExchangerType::BINANCE);
    updateExchange(ExchangerType::BYBIT);
}

void AccountsPage::createLayout()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(PAGE_HORIZONTAL_MARGIN,
                               PAGE_VERTICAL_MARGIN,
                               PAGE_HORIZONTAL_MARGIN,
                               PAGE_VERTICAL_MARGIN);
    layout->setSpacing(PAGE_LAYOUT_SPACING);

    auto *title = new QLabel("Accounts", this);
    title->setProperty("pageTitle", true);

    auto *description =
        new QLabel("Review Binance and Bybit balances with independent REST refresh and live updates.", this);
    description->setProperty("pageDescription", true);

    exchangeTabs = new QTabWidget(this);
    exchangeTabs->setObjectName("accountsExchangeTabs");
    exchangeTabs->setProperty("accountsExchangeTabs", true);
    binanceView = createExchangeView(ExchangerType::BINANCE);
    bybitView = createExchangeView(ExchangerType::BYBIT);
    exchangeTabs->addTab(binanceView.page, "Binance");
    exchangeTabs->addTab(bybitView.page, "Bybit");
    exchangeTabs->setCurrentIndex(0);

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(exchangeTabs, 1);
}

AccountsPage::ExchangeView AccountsPage::createExchangeView(ExchangerType exchangerType)
{
    const QString exchangeName = getExchangeName(exchangerType);
    const QString objectNamePrefix = getObjectNamePrefix(exchangerType);
    ExchangeView view;
    view.page = new QWidget(exchangeTabs);
    view.page->setObjectName(objectNamePrefix + "BalancesTab");

    auto *layout = new QVBoxLayout(view.page);
    layout->setContentsMargins(ACCOUNT_TAB_MARGIN, ACCOUNT_TAB_MARGIN, ACCOUNT_TAB_MARGIN, ACCOUNT_TAB_MARGIN);
    layout->setSpacing(ACCOUNT_TAB_SPACING);

    auto *actionLayout = new QHBoxLayout();
    actionLayout->setContentsMargins(0, 0, 0, 0);

    auto *snapshotLabel = new QLabel(exchangeName + " REST balance snapshot", view.page);
    snapshotLabel->setProperty("accountSectionTitle", true);
    actionLayout->addWidget(snapshotLabel);
    actionLayout->addStretch();

    view.refreshButton = new QPushButton("Refresh", view.page);
    view.refreshButton->setObjectName(objectNamePrefix + "BalancesRefreshButton");
    view.refreshButton->setProperty("secondaryOrderAction", true);
    view.refreshButton->setMinimumHeight(ACCOUNT_REFRESH_MINIMUM_HEIGHT);
    actionLayout->addWidget(view.refreshButton);

    view.status = new QLabel(view.page);
    view.status->setObjectName(objectNamePrefix + "BalancesStatus");
    view.status->setProperty("accountBalanceStatus", true);
    view.status->setWordWrap(true);

    view.liveStatus = new QLabel(view.page);
    view.liveStatus->setObjectName(objectNamePrefix + "BalancesLiveStatus");
    view.liveStatus->setProperty("accountBalanceStatus", true);
    view.liveStatus->setWordWrap(true);

    view.emptyState = new QLabel(view.page);
    view.emptyState->setObjectName(objectNamePrefix + "BalancesEmptyState");
    view.emptyState->setProperty("accountBalancesEmptyState", true);
    view.emptyState->setAlignment(Qt::AlignCenter);
    view.emptyState->setWordWrap(true);

    view.table = new QTableWidget(view.page);
    view.table->setObjectName(objectNamePrefix + "BalancesTable");
    view.table->setProperty("accountBalancesTable", true);
    view.table->setColumnCount(4);
    view.table->setHorizontalHeaderLabels({"Asset", "Free", "Locked", "Total"});
    view.table->setAlternatingRowColors(true);
    view.table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view.table->setSelectionBehavior(QAbstractItemView::SelectRows);
    view.table->setSelectionMode(QAbstractItemView::SingleSelection);
    view.table->setShowGrid(false);
    view.table->setMinimumHeight(ACCOUNT_TABLE_MINIMUM_HEIGHT);
    view.table->verticalHeader()->hide();
    view.table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    layout->addLayout(actionLayout);
    layout->addWidget(view.status);
    layout->addWidget(view.liveStatus);
    layout->addWidget(view.emptyState, 1);
    layout->addWidget(view.table, 1);

    return view;
}

AccountsPage::ExchangeView &AccountsPage::getExchangeView(ExchangerType exchangerType)
{
    return exchangerType == ExchangerType::BINANCE ? binanceView : bybitView;
}

void AccountsPage::connectCatalogUpdates()
{
    for (const ExchangerType exchangerType : {ExchangerType::BINANCE, ExchangerType::BYBIT})
    {
        connect(&balanceCatalog.getLoadState(exchangerType),
                &UiTaskState::statusChanged,
                this,
                [this, exchangerType](UiTaskState::Status) { updateExchange(exchangerType); });
    }

    connect(&balanceCatalog,
            &BalanceCatalog::balancesChanged,
            this,
            [this](ExchangerType exchangerType) { updateExchange(exchangerType); });
    connect(&balanceCatalog,
            &BalanceCatalog::liveUpdateStatusChanged,
            this,
            [this](ExchangerType exchangerType) { updateLiveStatus(exchangerType); });
    connect(binanceView.refreshButton,
            &QPushButton::clicked,
            this,
            [this]() { balanceCatalog.refreshBalances(ExchangerType::BINANCE); });
    connect(bybitView.refreshButton,
            &QPushButton::clicked,
            this,
            [this]() { balanceCatalog.refreshBalances(ExchangerType::BYBIT); });
}

void AccountsPage::updateExchange(ExchangerType exchangerType)
{
    ExchangeView &view = getExchangeView(exchangerType);
    const UiTaskState &loadState = balanceCatalog.getLoadState(exchangerType);
    const QString exchangeName = getExchangeName(exchangerType);
    const bool hasSuccessfulSnapshot = balanceCatalog.hasSuccessfulSnapshot(exchangerType);
    StatusPresentation presentation = StatusPresentation::NEUTRAL;
    view.refreshButton->setEnabled(!loadState.isLoading());

    switch (loadState.getStatus())
    {
    case UiTaskState::Status::IDLE:
        view.status->setText(exchangeName + " balances are waiting for the startup load.");
        break;
    case UiTaskState::Status::LOADING:
        view.status->setText(hasSuccessfulSnapshot ? "Refreshing " + exchangeName +
                                                         " balances. The latest cached snapshot remains visible."
                                                   : "Loading " + exchangeName + " balances...");
        presentation = StatusPresentation::LOADING;
        break;
    case UiTaskState::Status::SUCCEEDED:
        view.status->setText(balanceCatalog.getBalances(exchangerType).empty()
                                 ? exchangeName + " returned an empty balance snapshot."
                                 : exchangeName + " balances are ready.");
        presentation = StatusPresentation::SUCCESS;
        break;
    case UiTaskState::Status::FAILED:
        view.status->setText(
            hasSuccessfulSnapshot
                ? exchangeName + " balance refresh failed. Showing the latest cached snapshot: " + loadState.getError()
                : exchangeName + " balances are unavailable: " + loadState.getError());
        presentation = hasSuccessfulSnapshot ? StatusPresentation::WARNING : StatusPresentation::ERROR;
        break;
    }
    applyStatusPresentation(*view.status, presentation);

    updateLiveStatus(exchangerType);
    updateBalanceRows(exchangerType);
}

void AccountsPage::updateLiveStatus(ExchangerType exchangerType)
{
    ExchangeView &view = getExchangeView(exchangerType);
    const QString exchangeName = getExchangeName(exchangerType);
    const QString error = balanceCatalog.getLiveUpdateError(exchangerType);
    StatusPresentation presentation = StatusPresentation::NEUTRAL;
    switch (balanceCatalog.getLiveUpdateStatus(exchangerType))
    {
    case BalanceCatalog::LiveUpdateStatus::IDLE:
        view.liveStatus->setText(exchangeName + " live balance updates are waiting to start.");
        break;
    case BalanceCatalog::LiveUpdateStatus::STARTING:
        view.liveStatus->setText(error.isEmpty()
                                     ? "Starting " + exchangeName + " live balance updates..."
                                     : "Restarting " + exchangeName + " live balance updates after: " + error);
        presentation = error.isEmpty() ? StatusPresentation::LOADING : StatusPresentation::WARNING;
        break;
    case BalanceCatalog::LiveUpdateStatus::CONNECTING:
        view.liveStatus->setText("Connecting " + exchangeName + " live balance updates...");
        presentation = StatusPresentation::LOADING;
        break;
    case BalanceCatalog::LiveUpdateStatus::CONNECTED:
        view.liveStatus->setText(exchangeName + " live balance updates are connected.");
        presentation = StatusPresentation::SUCCESS;
        break;
    case BalanceCatalog::LiveUpdateStatus::RETRY_WAITING:
        view.liveStatus->setText(
            error.isEmpty()
                ? exchangeName + " live balance updates stopped unexpectedly. Reconnecting automatically..."
                : exchangeName + " live balance updates are unavailable: " + error + ". Reconnecting automatically...");
        presentation = StatusPresentation::WARNING;
        break;
    case BalanceCatalog::LiveUpdateStatus::STOPPED:
        view.liveStatus->setText(exchangeName + " live balance updates are stopped.");
        break;
    }
    applyStatusPresentation(*view.liveStatus, presentation);
}

void AccountsPage::updateBalanceRows(ExchangerType exchangerType)
{
    ExchangeView &view = getExchangeView(exchangerType);
    const QString exchangeName = getExchangeName(exchangerType);
    const bool hasSuccessfulSnapshot = balanceCatalog.hasSuccessfulSnapshot(exchangerType);
    const BalanceCatalog::BalanceSnapshot &balances = balanceCatalog.getBalances(exchangerType);
    view.table->setRowCount(0);

    if (!hasSuccessfulSnapshot)
    {
        view.table->hide();
        view.emptyState->setText("A successful " + exchangeName + " balance snapshot is not available yet.");
        view.emptyState->show();
        return;
    }

    for (const auto &[asset, balance] : balances)
    {
        if (balance.free == Decimal{} && balance.locked == Decimal{})
        {
            continue;
        }

        const int row = view.table->rowCount();
        view.table->insertRow(row);
        view.table->setItem(row, 0, createBalanceItem(QString::fromStdString(asset), Qt::AlignLeft | Qt::AlignVCenter));
        view.table->setItem(row, 1, createBalanceItem(formatBalance(balance.free), Qt::AlignRight | Qt::AlignVCenter));
        view.table->setItem(row,
                            2,
                            createBalanceItem(formatBalance(balance.locked), Qt::AlignRight | Qt::AlignVCenter));
        view.table->setItem(
            row,
            3,
            createBalanceItem(formatBalance(balance.free + balance.locked), Qt::AlignRight | Qt::AlignVCenter));
    }

    if (view.table->rowCount() > 0)
    {
        view.emptyState->hide();
        view.table->show();
        return;
    }

    view.table->hide();
    view.emptyState->setText(balances.empty() ? "No balances were returned for " + exchangeName + "."
                                              : "No non-zero balances were returned for " + exchangeName + ".");
    view.emptyState->show();
}
