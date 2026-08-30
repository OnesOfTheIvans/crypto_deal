#include "OrdersPage.hpp"

#include "graphical/GuiLayoutConstants.hpp"
#include "graphical/async/UiTaskState.hpp"
#include "graphical/models/BalanceCatalog.hpp"
#include "graphical/models/OrderPlacementModel.hpp"
#include "graphical/models/PairCatalog.hpp"
#include "graphical/models/SymbolInfoCatalog.hpp"
#include "graphical/widgets/OrderConfirmationDialog.hpp"
#include "graphical/widgets/OrderEntryForm.hpp"

#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <optional>

using namespace std;

using namespace GuiLayoutConstants;

OrdersPage::OrdersPage(PairCatalog &pairCatalog,
                       BalanceCatalog &balanceCatalog,
                       SymbolInfoCatalog &symbolInfoCatalog,
                       OrderPlacementModel &orderPlacementModel,
                       QWidget *parent)
    : QWidget(parent), pairCatalog(pairCatalog), balanceCatalog(balanceCatalog), symbolInfoCatalog(symbolInfoCatalog),
      orderPlacementModel(orderPlacementModel), binanceCatalogStatus(nullptr), bybitCatalogStatus(nullptr),
      orderEntryForm(nullptr), placementStatusPanel(nullptr), placementStatusTitle(nullptr),
      placementStatusDetails(nullptr), placementStatusError(nullptr)
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

    auto *workspaceScrollArea = new QScrollArea(this);
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

    placementStatusPanel = new QWidget(workspaceCard);
    placementStatusPanel->setObjectName("orderPlacementStatusPanel");
    placementStatusPanel->setProperty("placementStatusPanel", true);

    auto *placementStatusLayout = new QVBoxLayout(placementStatusPanel);
    placementStatusLayout->setContentsMargins(PLACEHOLDER_HORIZONTAL_MARGIN,
                                              PLACEHOLDER_VERTICAL_MARGIN,
                                              PLACEHOLDER_HORIZONTAL_MARGIN,
                                              PLACEHOLDER_VERTICAL_MARGIN);
    placementStatusLayout->setSpacing(ORDER_FORM_FEEDBACK_SPACING);

    placementStatusTitle = new QLabel(placementStatusPanel);
    placementStatusTitle->setObjectName("orderPlacementStatusTitle");
    placementStatusTitle->setProperty("placementStatusTitle", true);

    placementStatusDetails = new QLabel(placementStatusPanel);
    placementStatusDetails->setObjectName("orderPlacementStatusDetails");
    placementStatusDetails->setProperty("placementStatusDetails", true);
    placementStatusDetails->setTextFormat(Qt::PlainText);
    placementStatusDetails->setWordWrap(true);

    placementStatusError = new QLabel(placementStatusPanel);
    placementStatusError->setObjectName("orderPlacementStatusError");
    placementStatusError->setProperty("validationError", true);
    placementStatusError->setTextFormat(Qt::PlainText);
    placementStatusError->setWordWrap(true);
    placementStatusError->hide();

    placementStatusLayout->addWidget(placementStatusTitle);
    placementStatusLayout->addWidget(placementStatusDetails);
    placementStatusLayout->addWidget(placementStatusError);

    connect(orderEntryForm, &OrderEntryForm::requestConfirmation, this, &OrdersPage::requestOrderConfirmation);
    connect(&orderPlacementModel,
            &OrderPlacementModel::statusChanged,
            this,
            [this](OrderPlacementModel::Status) { updatePlacementStatus(); });

    workspaceLayout->addWidget(orderEntryForm);
    workspaceLayout->addWidget(placementStatusPanel);
    updatePlacementStatus();

    workspaceContentLayout->addWidget(workspaceCard);
    workspaceContentLayout->addStretch();
    workspaceScrollArea->setWidget(workspaceContent);

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addSpacing(PAGE_PLACEHOLDER_SPACING);
    layout->addWidget(workspaceScrollArea, 1);
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
            orderPlacementModel.placeOco(draft.value());
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
        orderPlacementModel.placeOrder(draft.value());
    }
}

void OrdersPage::updatePlacementStatus()
{
    const OrderPlacementModel::Status status = orderPlacementModel.getStatus();
    const optional<BasicOrderDraft> &draft = orderPlacementModel.getCurrentDraft();
    const optional<OcoOrderDraft> &ocoDraft = orderPlacementModel.getCurrentOcoDraft();
    const optional<OrderInfo> &acceptedOrder = orderPlacementModel.getAcceptedOrder();
    const optional<OcoInfo> &acceptedOco = orderPlacementModel.getAcceptedOco();
    const optional<OrderInfo> &terminalOrder = orderPlacementModel.getTerminalOrder();
    const bool isOcoPlacement = ocoDraft.has_value();
    QString details;

    if (draft.has_value())
    {
        details = QString::fromStdString(draft->pair.baseAsset + "/" + draft->pair.quoteAsset) + " · " +
                  QString::fromStdString(draft->quantityText) + " " + QString::fromStdString(draft->pair.baseAsset);
    }
    else if (ocoDraft.has_value())
    {
        details = QString::fromStdString(ocoDraft->pair.baseAsset + "/" + ocoDraft->pair.quoteAsset) + " · " +
                  QString::fromStdString(ocoDraft->quantityText) + " " +
                  QString::fromStdString(ocoDraft->pair.baseAsset) +
                  "\nLimit leg: " + QString::fromStdString(ocoDraft->limitPriceText) + " " +
                  QString::fromStdString(ocoDraft->pair.quoteAsset) +
                  "\nStop trigger: " + QString::fromStdString(ocoDraft->stopPriceText) + " " +
                  QString::fromStdString(ocoDraft->pair.quoteAsset);
        if (ocoDraft->stopLimitPriceText.has_value())
        {
            details += "\nStop-limit leg: " + QString::fromStdString(ocoDraft->stopLimitPriceText.value()) + " " +
                       QString::fromStdString(ocoDraft->pair.quoteAsset);
        }
    }
    if (acceptedOrder.has_value())
    {
        details += "\nOrder ID: " + QString::fromStdString(acceptedOrder->orderId) +
                   " · Exchange status: " + QString::fromStdString(acceptedOrder->status);
    }
    else if (acceptedOco.has_value())
    {
        details += "\nOCO group ID: " + QString::fromStdString(acceptedOco->orderListId) +
                   "\nTake-profit child ID: " + QString::fromStdString(acceptedOco->takeProfitOrder.orderId) +
                   " · Exchange status: " + QString::fromStdString(acceptedOco->takeProfitOrder.status) +
                   "\nStop-loss child ID: " + QString::fromStdString(acceptedOco->stopLossOrder.orderId) +
                   " · Exchange status: " + QString::fromStdString(acceptedOco->stopLossOrder.status);
    }

    placementStatusError->hide();
    placementStatusError->clear();
    switch (status)
    {
    case OrderPlacementModel::Status::IDLE:
        placementStatusPanel->hide();
        break;
    case OrderPlacementModel::Status::SUBMITTING:
        placementStatusTitle->setText(isOcoPlacement ? "Submitting OCO order" : "Submitting order");
        placementStatusDetails->setText(details);
        placementStatusPanel->show();
        break;
    case OrderPlacementModel::Status::ACCEPTED:
        placementStatusTitle->setText(isOcoPlacement ? "OCO order accepted" : "Order accepted");
        placementStatusDetails->setText(details);
        placementStatusPanel->show();
        break;
    case OrderPlacementModel::Status::WAITING:
        placementStatusTitle->setText(isOcoPlacement ? "OCO order accepted · waiting for a confirmed child fill"
                                                     : "Order accepted · waiting for a confirmed fill");
        placementStatusDetails->setText(details);
        placementStatusPanel->show();
        break;
    case OrderPlacementModel::Status::FILLED:
        if (terminalOrder.has_value())
        {
            if (isOcoPlacement)
            {
                details += "\nConfirmed filled child ID: " + QString::fromStdString(terminalOrder->orderId) +
                           " · Exchange status: " + QString::fromStdString(terminalOrder->status);
            }
            else
            {
                details = QString::fromStdString(terminalOrder->symbol) +
                          "\nOrder ID: " + QString::fromStdString(terminalOrder->orderId) +
                          " · Exchange status: " + QString::fromStdString(terminalOrder->status);
            }
        }
        placementStatusTitle->setText(isOcoPlacement ? "OCO child order filled" : "Order filled");
        placementStatusDetails->setText(details);
        placementStatusPanel->show();
        break;
    case OrderPlacementModel::Status::SUBMISSION_FAILED:
        placementStatusTitle->setText(isOcoPlacement ? "OCO placement failed" : "Order placement failed");
        placementStatusDetails->setText(details);
        placementStatusError->setText(orderPlacementModel.getError());
        placementStatusError->show();
        placementStatusPanel->show();
        break;
    case OrderPlacementModel::Status::WAIT_FAILED:
        placementStatusTitle->setText(isOcoPlacement ? "OCO accepted · status monitoring or cleanup failed"
                                                     : "Order accepted · status monitoring failed");
        placementStatusDetails->setText(details);
        placementStatusError->setText(orderPlacementModel.getError());
        placementStatusError->show();
        placementStatusPanel->show();
        break;
    }
    orderEntryForm->setPlacementActive(orderPlacementModel.hasActivePlacement());
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
