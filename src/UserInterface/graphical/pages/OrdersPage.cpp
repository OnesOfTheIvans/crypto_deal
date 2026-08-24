#include "OrdersPage.hpp"

#include "graphical/GuiLayoutConstants.hpp"
#include "graphical/async/UiTaskState.hpp"
#include "graphical/models/PairCatalog.hpp"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

using namespace GuiLayoutConstants;

OrdersPage::OrdersPage(PairCatalog &pairCatalog, QWidget *parent)
    : QWidget(parent), pairCatalog(pairCatalog), binanceCatalogStatus(nullptr), bybitCatalogStatus(nullptr)
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

    auto *placeholderCard = new QWidget(this);
    placeholderCard->setProperty("placeholderCard", true);
    placeholderCard->setMinimumHeight(PLACEHOLDER_MINIMUM_HEIGHT);

    auto *placeholderLayout = new QVBoxLayout(placeholderCard);
    placeholderLayout->setContentsMargins(PLACEHOLDER_HORIZONTAL_MARGIN,
                                          PLACEHOLDER_VERTICAL_MARGIN,
                                          PLACEHOLDER_HORIZONTAL_MARGIN,
                                          PLACEHOLDER_VERTICAL_MARGIN);
    placeholderLayout->setSpacing(PLACEHOLDER_LAYOUT_SPACING);

    auto *placeholderTitle = new QLabel("Order workspace", placeholderCard);
    placeholderTitle->setProperty("placeholderTitle", true);

    auto *placeholderDescription =
        new QLabel("Pair selection and order controls will be added in the next Orders steps.", placeholderCard);
    placeholderDescription->setProperty("placeholderDescription", true);
    placeholderDescription->setWordWrap(true);

    binanceCatalogStatus = new QLabel(placeholderCard);
    binanceCatalogStatus->setObjectName("binancePairCatalogStatus");
    binanceCatalogStatus->setProperty("pairCatalogStatus", true);
    binanceCatalogStatus->setTextFormat(Qt::PlainText);
    binanceCatalogStatus->setWordWrap(true);

    bybitCatalogStatus = new QLabel(placeholderCard);
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

    placeholderLayout->addWidget(placeholderTitle);
    placeholderLayout->addWidget(placeholderDescription);
    placeholderLayout->addWidget(binanceCatalogStatus);
    placeholderLayout->addWidget(bybitCatalogStatus);
    placeholderLayout->addStretch();

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addSpacing(PAGE_PLACEHOLDER_SPACING);
    layout->addWidget(placeholderCard);
    layout->addStretch();
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
