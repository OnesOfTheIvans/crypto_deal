#include "OrderConfirmationDialog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>

#include <utility>

using namespace std;

namespace {
    QString getExchangeName(ExchangerType exchangerType)
    {
        return exchangerType == ExchangerType::BINANCE ? "Binance" : "Bybit";
    }

    QString getOperationName(OperationType operation)
    {
        switch (operation)
        {
        case OperationType::BUY_CRYPTO:
            return "Buy crypto";
        case OperationType::SELL_CRYPTO:
            return "Sell crypto";
        case OperationType::PLACE_ORDER:
            return "Custom order";
        case OperationType::PLACE_OCO:
            return "Place OCO";
        default:
            return "Unsupported operation";
        }
    }

    QString getSideName(OrderOperation side)
    {
        return side == OrderOperation::BUY ? "Buy" : "Sell";
    }

    QString getOrderTypeName(OrderType type)
    {
        return type == OrderType::MARKET ? "Market" : "Limit";
    }
}

OrderConfirmationDialog::OrderConfirmationDialog(BasicOrderDraft draft, QWidget *parent)
    : QDialog(parent), draft(std::move(draft))
{
    createLayout();
}

OrderConfirmationDialog::OrderConfirmationDialog(OcoOrderDraft draft, QWidget *parent)
    : QDialog(parent), draft(std::move(draft))
{
    createLayout();
}

void OrderConfirmationDialog::createLayout()
{
    setObjectName("orderConfirmationDialog");
    setWindowTitle("Confirm order");
    setModal(true);
    setMinimumWidth(460);

    auto *layout = new QVBoxLayout(this);

    auto *title = new QLabel("Review order", this);
    title->setObjectName("orderConfirmationTitle");
    title->setProperty("confirmationTitle", true);

    auto *description = new QLabel("Check the exchange-bound values before placing this order.", this);
    description->setProperty("placeholderDescription", true);
    description->setWordWrap(true);

    auto *summary = new QLabel(buildSummary(), this);
    summary->setObjectName("orderConfirmationSummary");
    summary->setProperty("confirmationSummary", true);
    summary->setTextFormat(Qt::PlainText);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary->setWordWrap(true);

    auto *buttonBox = new QDialogButtonBox(this);
    auto *cancelButton = buttonBox->addButton(QDialogButtonBox::Cancel);
    auto *placeButton = buttonBox->addButton("Place order", QDialogButtonBox::AcceptRole);
    cancelButton->setObjectName("cancelOrderConfirmationButton");
    placeButton->setObjectName("placeConfirmedOrderButton");
    placeButton->setProperty("primaryOrderAction", true);
    cancelButton->setDefault(true);
    placeButton->setDefault(false);
    placeButton->setAutoDefault(false);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addWidget(summary);
    layout->addWidget(buttonBox);
}

QString OrderConfirmationDialog::buildSummary() const
{
    if (holds_alternative<OcoOrderDraft>(draft))
    {
        return buildOcoOrderSummary(get<OcoOrderDraft>(draft));
    }
    return buildBasicOrderSummary(get<BasicOrderDraft>(draft));
}

QString OrderConfirmationDialog::buildBasicOrderSummary(const BasicOrderDraft &basicOrderDraft) const
{
    QStringList lines;
    lines.push_back("Exchange: " + getExchangeName(basicOrderDraft.exchangerType));
    lines.push_back("Market: SPOT");
    lines.push_back("Pair: " + QString::fromStdString(basicOrderDraft.pair.baseAsset) + "/" +
                    QString::fromStdString(basicOrderDraft.pair.quoteAsset) + " (" +
                    QString::fromStdString(basicOrderDraft.pair.symbol) + ")");
    lines.push_back("Operation: " + getOperationName(basicOrderDraft.operation));
    lines.push_back("Side: " + getSideName(basicOrderDraft.side));
    lines.push_back("Order type: " + getOrderTypeName(basicOrderDraft.type));
    lines.push_back("Amount: " + QString::fromStdString(basicOrderDraft.quantityText) + " " +
                    QString::fromStdString(basicOrderDraft.pair.baseAsset));
    if (basicOrderDraft.priceText.has_value())
    {
        lines.push_back("Price: " + QString::fromStdString(basicOrderDraft.priceText.value()) + " " +
                        QString::fromStdString(basicOrderDraft.pair.quoteAsset) + " per " +
                        QString::fromStdString(basicOrderDraft.pair.baseAsset));
    }
    if (basicOrderDraft.timeInForce.has_value())
    {
        lines.push_back("Time in force: " + QString::fromStdString(basicOrderDraft.timeInForce.value()));
    }
    return lines.join("\n");
}

QString OrderConfirmationDialog::buildOcoOrderSummary(const OcoOrderDraft &ocoOrderDraft) const
{
    const QString baseAsset = QString::fromStdString(ocoOrderDraft.pair.baseAsset);
    const QString quoteAsset = QString::fromStdString(ocoOrderDraft.pair.quoteAsset);
    QStringList lines;
    lines.push_back("Exchange: " + getExchangeName(ocoOrderDraft.exchangerType));
    lines.push_back("Market: SPOT");
    lines.push_back("Pair: " + baseAsset + "/" + quoteAsset + " (" + QString::fromStdString(ocoOrderDraft.pair.symbol) +
                    ")");
    lines.push_back("Operation: Place OCO");
    lines.push_back("Side: " + getSideName(ocoOrderDraft.side));
    lines.push_back("Amount: " + QString::fromStdString(ocoOrderDraft.quantityText) + " " + baseAsset);
    lines.push_back("Limit leg: " + QString::fromStdString(ocoOrderDraft.limitPriceText) + " " + quoteAsset + " per " +
                    baseAsset);
    lines.push_back("Stop trigger: " + QString::fromStdString(ocoOrderDraft.stopPriceText) + " " + quoteAsset +
                    " per " + baseAsset);
    if (ocoOrderDraft.stopLimitPriceText.has_value())
    {
        lines.push_back("Stop-limit leg: " + QString::fromStdString(ocoOrderDraft.stopLimitPriceText.value()) + " " +
                        quoteAsset + " per " + baseAsset);
    }
    if (ocoOrderDraft.stopLimitTimeInForce.has_value())
    {
        lines.push_back("Time in force: " + QString::fromStdString(ocoOrderDraft.stopLimitTimeInForce.value()));
    }
    return lines.join("\n");
}
