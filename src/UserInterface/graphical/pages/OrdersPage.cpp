#include "OrdersPage.hpp"

#include "graphical/GuiLayoutConstants.hpp"
#include "graphical/async/UiTaskState.hpp"
#include "graphical/models/PairCatalog.hpp"
#include "graphical/models/SymbolInfoCatalog.hpp"
#include "graphical/widgets/OrderEntryForm.hpp"

#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

using namespace GuiLayoutConstants;

OrdersPage::OrdersPage(PairCatalog &pairCatalog, SymbolInfoCatalog &symbolInfoCatalog, QWidget *parent)
    : QWidget(parent), pairCatalog(pairCatalog), symbolInfoCatalog(symbolInfoCatalog), binanceCatalogStatus(nullptr),
      bybitCatalogStatus(nullptr)
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
    workspaceLayout->addWidget(new OrderEntryForm(pairCatalog, symbolInfoCatalog, workspaceCard));

    workspaceContentLayout->addWidget(workspaceCard);
    workspaceContentLayout->addStretch();
    workspaceScrollArea->setWidget(workspaceContent);

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addSpacing(PAGE_PLACEHOLDER_SPACING);
    layout->addWidget(workspaceScrollArea, 1);
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
