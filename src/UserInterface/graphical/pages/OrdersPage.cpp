#include "OrdersPage.hpp"

#include "common/DecimalConverter.hpp"
#include "graphical/GuiLayoutConstants.hpp"
#include "graphical/async/UiTaskState.hpp"
#include "graphical/models/BalanceCatalog.hpp"
#include "graphical/models/OrderSessionModel.hpp"
#include "graphical/models/PairCatalog.hpp"
#include "graphical/models/SymbolInfoCatalog.hpp"
#include "graphical/widgets/OrderConfirmationDialog.hpp"
#include "graphical/widgets/OrderEntryForm.hpp"

#include <QDateTime>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QScrollArea>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <optional>

using namespace std;

using namespace GuiLayoutConstants;

namespace {
    enum SessionOrderColumn
    {
        UPDATED_AT_COLUMN,
        EXCHANGE_COLUMN,
        PAIR_COLUMN,
        IDENTIFIER_COLUMN,
        SIDE_AND_TYPE_COLUMN,
        PRICE_COLUMN,
        QUANTITY_COLUMN,
        STATUS_COLUMN,
        SESSION_ORDER_COLUMN_COUNT
    };

    QString getExchangeName(ExchangerType exchangerType)
    {
        return exchangerType == ExchangerType::BINANCE ? "Binance" : "Bybit";
    }

    QString getSideName(OrderOperation side)
    {
        return side == OrderOperation::BUY ? "Buy" : "Sell";
    }

    QString getOrderTypeName(OrderType type)
    {
        return type == OrderType::MARKET ? "Market" : "Limit";
    }

    QString getBasicOperationName(OperationType operation)
    {
        switch (operation)
        {
        case OperationType::BUY_CRYPTO:
            return "Buy crypto · Market";
        case OperationType::SELL_CRYPTO:
            return "Sell crypto · Market";
        case OperationType::PLACE_ORDER:
            return "Custom order";
        default:
            return "Order";
        }
    }

    QString getPairName(const TradablePair &pair)
    {
        return QString::fromStdString(pair.baseAsset + "/" + pair.quoteAsset);
    }

    QString formatDecimal(Decimal value)
    {
        return QString::fromStdString(DecimalConverter::formatDecimal(value));
    }

    QString formatTimestamp(qint64 timestampMs)
    {
        return timestampMs > 0 ? QDateTime::fromMSecsSinceEpoch(timestampMs).toString(Qt::ISODateWithMs) : "—";
    }

    qint64 getOrderTimestamp(const OrderInfo &orderInfo, qint64 fallbackTimestampMs)
    {
        if (orderInfo.updatedTimeMs > 0)
        {
            return orderInfo.updatedTimeMs;
        }
        if (orderInfo.createdTimeMs > 0)
        {
            return orderInfo.createdTimeMs;
        }
        return fallbackTimestampMs;
    }

    const OrderInfo *getDisplayedBasicOrder(const OrderSessionModel::Entry &entry)
    {
        if (entry.terminalOrder.has_value())
        {
            return &entry.terminalOrder.value();
        }
        if (entry.acceptedOrder.has_value())
        {
            return &entry.acceptedOrder.value();
        }
        return nullptr;
    }

    QString getBasicPrice(const OrderSessionModel::Entry &entry)
    {
        const OrderInfo *orderInfo = getDisplayedBasicOrder(entry);
        if (orderInfo != nullptr && orderInfo->price > 0)
        {
            return formatDecimal(orderInfo->price);
        }
        if (entry.basicDraft.has_value() && entry.basicDraft->priceText.has_value())
        {
            return QString::fromStdString(entry.basicDraft->priceText.value());
        }
        return "Market";
    }

    QString getBasicQuantity(const OrderSessionModel::Entry &entry)
    {
        QString requestedQuantity = "—";
        QString executedQuantity = "—";
        if (entry.basicDraft.has_value())
        {
            requestedQuantity = QString::fromStdString(entry.basicDraft->quantityText);
        }

        const OrderInfo *orderInfo = getDisplayedBasicOrder(entry);
        if (orderInfo != nullptr)
        {
            if (orderInfo->origQty > 0)
            {
                requestedQuantity = formatDecimal(orderInfo->origQty);
            }
            executedQuantity = formatDecimal(orderInfo->executedQty);
        }
        return "Requested: " + requestedQuantity + "\nExecuted: " + executedQuantity;
    }

    QString getOcoPrice(const OcoOrderDraft &draft)
    {
        QString price = "Limit: " + QString::fromStdString(draft.limitPriceText) +
                        "\nStop: " + QString::fromStdString(draft.stopPriceText);
        if (draft.stopLimitPriceText.has_value())
        {
            price += "\nStop-limit: " + QString::fromStdString(draft.stopLimitPriceText.value());
        }
        return price;
    }

    QString getOcoQuantity(const OcoOrderDraft &draft)
    {
        return "Requested: " + QString::fromStdString(draft.quantityText) + "\nExecuted: —";
    }

    QString getOrderQuantity(const OrderInfo &orderInfo, const QString &fallbackQuantity)
    {
        const QString requestedQuantity = orderInfo.origQty > 0 ? formatDecimal(orderInfo.origQty) : fallbackQuantity;
        return "Requested: " + requestedQuantity + "\nExecuted: " + formatDecimal(orderInfo.executedQty);
    }

    QString getOrderPrice(const OrderInfo &orderInfo)
    {
        return orderInfo.price > 0 ? formatDecimal(orderInfo.price) : "Market";
    }

    QString getOrderSideAndType(const OrderInfo &orderInfo)
    {
        const QString side = orderInfo.side.empty() ? "—" : QString::fromStdString(orderInfo.side);
        const QString type = orderInfo.type.empty() ? "—" : QString::fromStdString(orderInfo.type);
        return side + " · " + type;
    }

    QString getEntryStatus(const OrderSessionModel::Entry &entry)
    {
        QString status;
        switch (entry.status)
        {
        case OrderSessionModel::Status::SUBMITTING:
            status = entry.type == OrderSessionModel::EntryType::OCO_GROUP ? "Submitting OCO" : "Submitting";
            break;
        case OrderSessionModel::Status::ACCEPTED:
            status = entry.type == OrderSessionModel::EntryType::OCO_GROUP ? "OCO accepted" : "Accepted";
            break;
        case OrderSessionModel::Status::WAITING:
            status = entry.type == OrderSessionModel::EntryType::OCO_GROUP ? "Waiting for confirmed child fill"
                                                                           : "Waiting for confirmed fill";
            break;
        case OrderSessionModel::Status::FILLED:
            status = entry.type == OrderSessionModel::EntryType::OCO_GROUP ? "Confirmed child filled; sibling terminal"
                                                                           : "Filled";
            break;
        case OrderSessionModel::Status::SUBMISSION_FAILED:
            status = "Submission failed";
            break;
        case OrderSessionModel::Status::MONITORING_FAILED:
            status = "Monitoring failed";
            break;
        }
        if (!entry.error.isEmpty())
        {
            status += "\n" + entry.error;
        }
        return status;
    }

    void setEntryId(QTreeWidgetItem &item, OrderSessionModel::EntryId entryId)
    {
        item.setData(UPDATED_AT_COLUMN, Qt::UserRole, QVariant::fromValue<qulonglong>(entryId));
    }

    void populateBasicEntry(QTreeWidget &table, const OrderSessionModel::Entry &entry)
    {
        const BasicOrderDraft &draft = entry.basicDraft.value();
        const OrderInfo *orderInfo = getDisplayedBasicOrder(entry);
        auto *item = new QTreeWidgetItem(&table);
        item->setText(UPDATED_AT_COLUMN,
                      formatTimestamp(orderInfo == nullptr ? entry.updatedAtMs
                                                           : getOrderTimestamp(*orderInfo, entry.updatedAtMs)));
        item->setText(EXCHANGE_COLUMN, getExchangeName(draft.exchangerType));
        item->setText(PAIR_COLUMN, getPairName(draft.pair));
        item->setText(IDENTIFIER_COLUMN,
                      orderInfo == nullptr || orderInfo->orderId.empty() ? "Session #" + QString::number(entry.id)
                                                                         : QString::fromStdString(orderInfo->orderId));
        item->setText(SIDE_AND_TYPE_COLUMN,
                      draft.operation == OperationType::PLACE_ORDER
                          ? getSideName(draft.side) + " · " + getOrderTypeName(draft.type)
                          : getBasicOperationName(draft.operation));
        item->setText(PRICE_COLUMN, getBasicPrice(entry));
        item->setText(QUANTITY_COLUMN, getBasicQuantity(entry));
        item->setText(STATUS_COLUMN, getEntryStatus(entry));
        setEntryId(*item, entry.id);
    }

    void populateOcoChild(QTreeWidgetItem &parent,
                          const QString &childName,
                          const OcoOrderDraft &draft,
                          const OrderInfo *orderInfo,
                          const OrderInfo *terminalOrder,
                          qint64 fallbackTimestampMs,
                          OrderSessionModel::EntryId entryId)
    {
        const OrderInfo *displayedOrder = orderInfo;
        if (terminalOrder != nullptr && orderInfo != nullptr && terminalOrder->orderId == orderInfo->orderId)
        {
            displayedOrder = terminalOrder;
        }

        auto *item = new QTreeWidgetItem(&parent);
        item->setText(UPDATED_AT_COLUMN,
                      formatTimestamp(displayedOrder == nullptr
                                          ? fallbackTimestampMs
                                          : getOrderTimestamp(*displayedOrder, fallbackTimestampMs)));
        item->setText(EXCHANGE_COLUMN, getExchangeName(draft.exchangerType));
        item->setText(PAIR_COLUMN, getPairName(draft.pair));
        item->setText(IDENTIFIER_COLUMN,
                      childName + (displayedOrder == nullptr || displayedOrder->orderId.empty()
                                       ? " pending"
                                       : " · " + QString::fromStdString(displayedOrder->orderId)));
        item->setText(SIDE_AND_TYPE_COLUMN,
                      displayedOrder == nullptr ? getSideName(draft.side) + " · OCO child"
                                                : getOrderSideAndType(*displayedOrder));
        item->setText(PRICE_COLUMN,
                      displayedOrder == nullptr ? "Pending exchange price" : getOrderPrice(*displayedOrder));
        item->setText(QUANTITY_COLUMN,
                      displayedOrder == nullptr
                          ? "Requested: " + QString::fromStdString(draft.quantityText) + "\nExecuted: —"
                          : getOrderQuantity(*displayedOrder, QString::fromStdString(draft.quantityText)));
        item->setText(STATUS_COLUMN,
                      displayedOrder == nullptr ? "Awaiting OCO acceptance"
                                                : QString::fromStdString(displayedOrder->status));
        setEntryId(*item, entryId);
    }

    void populateOcoEntry(QTreeWidget &table, const OrderSessionModel::Entry &entry)
    {
        const OcoOrderDraft &draft = entry.ocoDraft.value();
        const OcoInfo *ocoInfo = entry.acceptedOco.has_value() ? &entry.acceptedOco.value() : nullptr;
        const OrderInfo *terminalOrder = entry.terminalOrder.has_value() ? &entry.terminalOrder.value() : nullptr;
        auto *item = new QTreeWidgetItem(&table);
        item->setText(UPDATED_AT_COLUMN, formatTimestamp(entry.updatedAtMs));
        item->setText(EXCHANGE_COLUMN, getExchangeName(draft.exchangerType));
        item->setText(PAIR_COLUMN, getPairName(draft.pair));
        item->setText(IDENTIFIER_COLUMN,
                      ocoInfo == nullptr || ocoInfo->orderListId.empty()
                          ? "Session #" + QString::number(entry.id) + " · OCO"
                          : "OCO group · " + QString::fromStdString(ocoInfo->orderListId));
        item->setText(SIDE_AND_TYPE_COLUMN, getSideName(draft.side) + " · OCO");
        item->setText(PRICE_COLUMN, getOcoPrice(draft));
        item->setText(QUANTITY_COLUMN, getOcoQuantity(draft));
        item->setText(STATUS_COLUMN, getEntryStatus(entry));
        item->setExpanded(true);
        setEntryId(*item, entry.id);

        populateOcoChild(*item,
                         "Take-profit child",
                         draft,
                         ocoInfo == nullptr ? nullptr : &ocoInfo->takeProfitOrder,
                         terminalOrder,
                         entry.updatedAtMs,
                         entry.id);
        populateOcoChild(*item,
                         "Stop-loss child",
                         draft,
                         ocoInfo == nullptr ? nullptr : &ocoInfo->stopLossOrder,
                         terminalOrder,
                         entry.updatedAtMs,
                         entry.id);
    }

    QWidget *createSessionOrderView(QWidget *parent,
                                    const QString &tableObjectName,
                                    const QString &emptyStateText,
                                    QLabel *&emptyState,
                                    QTreeWidget *&table)
    {
        auto *view = new QWidget(parent);
        auto *layout = new QVBoxLayout(view);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(ORDER_FORM_FEEDBACK_SPACING);

        emptyState = new QLabel(emptyStateText, view);
        emptyState->setProperty("sessionOrdersEmptyState", true);
        emptyState->setWordWrap(true);

        table = new QTreeWidget(view);
        table->setObjectName(tableObjectName);
        table->setProperty("sessionOrdersTable", true);
        table->setColumnCount(SESSION_ORDER_COLUMN_COUNT);
        table->setHeaderLabels({"Updated",
                                "Exchange",
                                "Pair",
                                "Order / group ID",
                                "Side / type",
                                "Price",
                                "Requested / executed",
                                "Status"});
        table->setRootIsDecorated(true);
        table->setItemsExpandable(false);
        table->setWordWrap(true);
        table->setAlternatingRowColors(true);
        table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        table->setMinimumHeight(220);
        table->header()->setStretchLastSection(true);
        table->header()->setSectionResizeMode(UPDATED_AT_COLUMN, QHeaderView::ResizeToContents);
        table->header()->setSectionResizeMode(EXCHANGE_COLUMN, QHeaderView::ResizeToContents);
        table->header()->setSectionResizeMode(PAIR_COLUMN, QHeaderView::ResizeToContents);
        table->header()->setSectionResizeMode(IDENTIFIER_COLUMN, QHeaderView::ResizeToContents);
        table->header()->setSectionResizeMode(SIDE_AND_TYPE_COLUMN, QHeaderView::ResizeToContents);
        table->header()->setSectionResizeMode(PRICE_COLUMN, QHeaderView::ResizeToContents);
        table->header()->setSectionResizeMode(QUANTITY_COLUMN, QHeaderView::ResizeToContents);

        layout->addWidget(emptyState);
        layout->addWidget(table);
        return view;
    }
}

OrdersPage::OrdersPage(PairCatalog &pairCatalog,
                       BalanceCatalog &balanceCatalog,
                       SymbolInfoCatalog &symbolInfoCatalog,
                       OrderSessionModel &orderSessionModel,
                       QWidget *parent)
    : QWidget(parent), pairCatalog(pairCatalog), balanceCatalog(balanceCatalog), symbolInfoCatalog(symbolInfoCatalog),
      orderSessionModel(orderSessionModel), binanceCatalogStatus(nullptr), bybitCatalogStatus(nullptr),
      orderEntryForm(nullptr), activeOrdersEmptyState(nullptr), allSessionOrdersEmptyState(nullptr),
      activeOrdersTable(nullptr), allSessionOrdersTable(nullptr)
{
    setObjectName("ordersPage");
    setProperty("primaryPage", true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(PAGE_HORIZONTAL_MARGIN,
                               PAGE_VERTICAL_MARGIN,
                               PAGE_HORIZONTAL_MARGIN,
                               PAGE_VERTICAL_MARGIN);
    layout->setSpacing(PAGE_LAYOUT_SPACING);

    auto *title = new QLabel("Orders", this);
    title->setProperty("pageTitle", true);

    auto *description = new QLabel("Place spot orders and follow their session status.", this);
    description->setProperty("pageDescription", true);

    auto *ordersWorkspaceTabs = new QTabWidget(this);
    ordersWorkspaceTabs->setObjectName("ordersWorkspaceTabs");
    ordersWorkspaceTabs->setProperty("ordersWorkspaceTabs", true);

    auto *workspaceScrollArea = new QScrollArea(ordersWorkspaceTabs);
    workspaceScrollArea->setObjectName("orderWorkspaceScrollArea");
    workspaceScrollArea->setFrameShape(QFrame::NoFrame);
    workspaceScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    workspaceScrollArea->setWidgetResizable(true);

    auto *workspaceContent = new QWidget(workspaceScrollArea);
    workspaceContent->setObjectName("orderWorkspaceContent");

    auto *workspaceContentLayout = new QVBoxLayout(workspaceContent);
    workspaceContentLayout->setContentsMargins(0, 0, ORDER_WORKSPACE_SCROLL_MARGIN, 0);
    workspaceContentLayout->setSpacing(0);

    auto *workspaceCard = new QWidget(workspaceContent);
    workspaceCard->setObjectName("orderWorkspaceCard");
    workspaceCard->setProperty("orderCard", true);

    auto *workspaceLayout = new QVBoxLayout(workspaceCard);
    workspaceLayout->setContentsMargins(PLACEHOLDER_HORIZONTAL_MARGIN,
                                        PLACEHOLDER_VERTICAL_MARGIN,
                                        PLACEHOLDER_HORIZONTAL_MARGIN,
                                        PLACEHOLDER_VERTICAL_MARGIN);
    workspaceLayout->setSpacing(PLACEHOLDER_LAYOUT_SPACING);

    auto *workspaceTitle = new QLabel("New order", workspaceCard);
    workspaceTitle->setProperty("placeholderTitle", true);

    auto *workspaceDescription =
        new QLabel("Choose a tradable SPOT pair and prepare the fields for one placement operation.", workspaceCard);
    workspaceDescription->setProperty("placeholderDescription", true);
    workspaceDescription->setWordWrap(true);

    binanceCatalogStatus = new QLabel(workspaceCard);
    binanceCatalogStatus->setObjectName("binancePairCatalogStatus");
    binanceCatalogStatus->setProperty("pairCatalogStatus", true);
    binanceCatalogStatus->setTextFormat(Qt::PlainText);
    binanceCatalogStatus->setWordWrap(true);

    bybitCatalogStatus = new QLabel(workspaceCard);
    bybitCatalogStatus->setObjectName("bybitPairCatalogStatus");
    bybitCatalogStatus->setProperty("pairCatalogStatus", true);
    bybitCatalogStatus->setTextFormat(Qt::PlainText);
    bybitCatalogStatus->setWordWrap(true);

    connect(&pairCatalog.getLoadState(ExchangerType::BINANCE),
            &UiTaskState::statusChanged,
            this,
            [this](UiTaskState::Status)
            { updateCatalogStatus(ExchangerType::BINANCE, *binanceCatalogStatus, "Binance"); });
    connect(&pairCatalog.getLoadState(ExchangerType::BYBIT),
            &UiTaskState::statusChanged,
            this,
            [this](UiTaskState::Status) { updateCatalogStatus(ExchangerType::BYBIT, *bybitCatalogStatus, "Bybit"); });

    updateCatalogStatus(ExchangerType::BINANCE, *binanceCatalogStatus, "Binance");
    updateCatalogStatus(ExchangerType::BYBIT, *bybitCatalogStatus, "Bybit");

    auto *catalogDivider = new QFrame(workspaceCard);
    catalogDivider->setProperty("orderFormDivider", true);
    catalogDivider->setFrameShape(QFrame::HLine);

    workspaceLayout->addWidget(workspaceTitle);
    workspaceLayout->addWidget(workspaceDescription);
    workspaceLayout->addWidget(binanceCatalogStatus);
    workspaceLayout->addWidget(bybitCatalogStatus);
    workspaceLayout->addWidget(catalogDivider);
    orderEntryForm = new OrderEntryForm(pairCatalog, balanceCatalog, symbolInfoCatalog, workspaceCard);

    auto *sessionOrdersWorkspace = new QWidget(ordersWorkspaceTabs);
    auto *sessionOrdersLayout = new QVBoxLayout(sessionOrdersWorkspace);
    sessionOrdersLayout->setContentsMargins(0, 0, 0, 0);
    sessionOrdersLayout->setSpacing(PLACEHOLDER_LAYOUT_SPACING);

    auto *sessionOrdersTitle = new QLabel("Session orders", sessionOrdersWorkspace);
    sessionOrdersTitle->setObjectName("sessionOrdersTitle");
    sessionOrdersTitle->setProperty("placeholderTitle", true);

    auto *sessionOrdersDescription =
        new QLabel("Orders placed in this session only. Refresh and cancellation actions arrive in the next step.",
                   sessionOrdersWorkspace);
    sessionOrdersDescription->setProperty("placeholderDescription", true);
    sessionOrdersDescription->setWordWrap(true);

    auto *sessionOrdersTabs = new QTabWidget(sessionOrdersWorkspace);
    sessionOrdersTabs->setObjectName("sessionOrdersTabs");
    sessionOrdersTabs->setProperty("sessionOrdersTabs", true);
    sessionOrdersTabs->addTab(createSessionOrderView(sessionOrdersTabs,
                                                     "activeOrdersTable",
                                                     "No active session orders.",
                                                     activeOrdersEmptyState,
                                                     activeOrdersTable),
                              "Active orders");
    sessionOrdersTabs->addTab(createSessionOrderView(sessionOrdersTabs,
                                                     "allSessionOrdersTable",
                                                     "No orders have been placed in this session.",
                                                     allSessionOrdersEmptyState,
                                                     allSessionOrdersTable),
                              "All session orders");

    connect(orderEntryForm, &OrderEntryForm::requestConfirmation, this, &OrdersPage::requestOrderConfirmation);
    connect(&orderSessionModel, &OrderSessionModel::entriesChanged, this, &OrdersPage::updateSessionOrderTables);

    workspaceLayout->addWidget(orderEntryForm);
    sessionOrdersLayout->addWidget(sessionOrdersTitle);
    sessionOrdersLayout->addWidget(sessionOrdersDescription);
    sessionOrdersLayout->addWidget(sessionOrdersTabs, 1);
    updateSessionOrderTables();

    workspaceContentLayout->addWidget(workspaceCard);
    workspaceContentLayout->addStretch();
    workspaceScrollArea->setWidget(workspaceContent);

    ordersWorkspaceTabs->addTab(workspaceScrollArea, "New order");
    ordersWorkspaceTabs->addTab(sessionOrdersWorkspace, "Session orders");

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addSpacing(PAGE_PLACEHOLDER_SPACING);
    layout->addWidget(ordersWorkspaceTabs, 1);
}

void OrdersPage::requestOrderConfirmation()
{
    if (orderEntryForm->getSelectedOperation() == OperationType::PLACE_OCO)
    {
        const optional<OcoOrderDraft> draft = orderEntryForm->createOcoOrderDraft();
        if (!draft.has_value())
        {
            return;
        }

        OrderConfirmationDialog confirmationDialog(draft.value(), this);
        if (confirmationDialog.exec() == QDialog::Accepted)
        {
            orderSessionModel.placeOco(draft.value());
        }
        return;
    }

    const optional<BasicOrderDraft> draft = orderEntryForm->createBasicOrderDraft();
    if (!draft.has_value())
    {
        return;
    }

    OrderConfirmationDialog confirmationDialog(draft.value(), this);
    if (confirmationDialog.exec() == QDialog::Accepted)
    {
        orderSessionModel.placeOrder(draft.value());
    }
}

void OrdersPage::updateSessionOrderTables()
{
    updateSessionOrderTable(*activeOrdersTable, *activeOrdersEmptyState, true);
    updateSessionOrderTable(*allSessionOrdersTable, *allSessionOrdersEmptyState, false);
}

void OrdersPage::updateSessionOrderTable(QTreeWidget &table, QLabel &emptyState, bool showOnlyActiveOrders)
{
    table.clear();
    for (const OrderSessionModel::Entry &entry : orderSessionModel.getEntries())
    {
        if (showOnlyActiveOrders && !OrderSessionModel::isActiveStatus(entry.status))
        {
            continue;
        }

        if (entry.type == OrderSessionModel::EntryType::BASIC_ORDER && entry.basicDraft.has_value())
        {
            populateBasicEntry(table, entry);
        }
        else if (entry.type == OrderSessionModel::EntryType::OCO_GROUP && entry.ocoDraft.has_value())
        {
            populateOcoEntry(table, entry);
        }
    }

    const bool hasRows = table.topLevelItemCount() > 0;
    emptyState.setVisible(!hasRows);
    table.setVisible(hasRows);
}

void OrdersPage::updateCatalogStatus(ExchangerType exchangerType, QLabel &statusLabel, const QString &exchangeName)
{
    const UiTaskState &loadState = pairCatalog.getLoadState(exchangerType);
    const qsizetype pairCount = static_cast<qsizetype>(pairCatalog.getPairs(exchangerType).size());
    QString status;

    switch (loadState.getStatus())
    {
    case UiTaskState::Status::IDLE:
        status = "Waiting for startup load";
        break;
    case UiTaskState::Status::LOADING:
        status = "Loading tradable pairs...";
        break;
    case UiTaskState::Status::SUCCEEDED:
        status = pairCount == 0
                     ? "No tradable pairs available"
                     : QString("%1 tradable %2 available").arg(pairCount).arg(pairCount == 1 ? "pair" : "pairs");
        break;
    case UiTaskState::Status::FAILED:
        status = "Unable to load pairs: " + loadState.getError();
        break;
    }

    statusLabel.setText(exchangeName + ": " + status);
}
