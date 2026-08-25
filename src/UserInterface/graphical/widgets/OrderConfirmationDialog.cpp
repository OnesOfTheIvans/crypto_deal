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
    QStringList lines;
    lines.push_back("Exchange: " + getExchangeName(draft.exchangerType));
    lines.push_back("Market: SPOT");
    lines.push_back("Pair: " + QString::fromStdString(draft.pair.baseAsset) + "/" +
                    QString::fromStdString(draft.pair.quoteAsset) + " (" + QString::fromStdString(draft.pair.symbol) +
                    ")");
    lines.push_back("Operation: " + getOperationName(draft.operation));
    lines.push_back("Side: " + getSideName(draft.side));
    lines.push_back("Order type: " + getOrderTypeName(draft.type));
    lines.push_back("Amount: " + QString::fromStdString(draft.quantityText) + " " +
                    QString::fromStdString(draft.pair.baseAsset));
    if (draft.priceText.has_value())
    {
        lines.push_back("Price: " + QString::fromStdString(draft.priceText.value()) + " " +
                        QString::fromStdString(draft.pair.quoteAsset) + " per " +
                        QString::fromStdString(draft.pair.baseAsset));
    }
    if (draft.timeInForce.has_value())
    {
        lines.push_back("Time in force: " + QString::fromStdString(draft.timeInForce.value()));
    }
    return lines.join("\n");
}
