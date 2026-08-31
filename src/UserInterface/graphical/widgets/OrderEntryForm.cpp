#include "OrderEntryForm.hpp"

#include "common/DecimalConverter.hpp"
#include "graphical/GuiLayoutConstants.hpp"
#include "graphical/async/UiTaskState.hpp"
#include "graphical/models/BalanceCatalog.hpp"
#include "graphical/models/PairCatalog.hpp"
#include "graphical/models/SymbolInfoCatalog.hpp"
#include "graphical/widgets/DecimalInputField.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace GuiLayoutConstants;
using namespace std;

namespace {
    QString formatDecimal(Decimal value)
    {
        return QString::fromStdString(DecimalConverter::formatDecimal(value));
    }

    QString formatByIncrement(Decimal value, Decimal increment)
    {
        return QString::fromStdString(DecimalConverter::formatByStep(value, increment));
    }

    void appendBound(QStringList &parts, const QString &name, Decimal value, Decimal increment)
    {
        if (value > 0)
        {
            parts.push_back(name + " " + formatByIncrement(value, increment));
        }
    }
}

OrderEntryForm::OrderEntryForm(PairCatalog &pairCatalog,
                               BalanceCatalog &balanceCatalog,
                               SymbolInfoCatalog &symbolInfoCatalog,
                               QWidget *parent)
    : QWidget(parent), pairCatalog(pairCatalog), balanceCatalog(balanceCatalog), symbolInfoCatalog(symbolInfoCatalog),
      exchangeSelector(nullptr), categoryField(nullptr), selectedCatalogStatus(nullptr), selectedBalanceStatus(nullptr),
      retryBalanceButton(nullptr), pairDependentControls(nullptr), baseAssetSelector(nullptr),
      quoteAssetSelector(nullptr), operationSelector(nullptr), selectedSymbolInfoStatus(nullptr),
      retrySymbolInfoButton(nullptr), tradingLimits(nullptr), amountLabel(nullptr), amountField(nullptr),
      operationFormStack(nullptr), placeOrderSideSelector(nullptr), placeOrderTypeSelector(nullptr),
      placeOrderLimitFields(nullptr), placeOrderPriceLabel(nullptr), placeOrderPriceField(nullptr),
      ocoSideSelector(nullptr), useOcoStopLimit(nullptr), ocoStopLimitFields(nullptr), ocoLimitPriceLabel(nullptr),
      ocoStopPriceLabel(nullptr), ocoStopLimitPriceLabel(nullptr), ocoLimitPriceField(nullptr),
      ocoStopPriceField(nullptr), ocoStopLimitPriceField(nullptr), formValidationError(nullptr), proceedButton(nullptr)
{
    setObjectName("orderEntryForm");
    createLayout();
    connectCatalogUpdates();
    connectInputUpdates();
    updateSelectedCatalog();
    updateSelectedBalance();
}

ExchangerType OrderEntryForm::getSelectedExchangerType() const
{
    return static_cast<ExchangerType>(exchangeSelector->currentData().toInt());
}

optional<TradablePair> OrderEntryForm::getSelectedPair() const
{
    if (!hasUsableSelectedCatalog())
    {
        return nullopt;
    }

    const string selectedBaseAsset = baseAssetSelector->currentText().toStdString();
    const string selectedQuoteAsset = quoteAssetSelector->currentText().toStdString();
    const vector<TradablePair> &pairs = pairCatalog.getPairs(getSelectedExchangerType());
    const auto selectedPair =
        ranges::find_if(pairs,
                        [&selectedBaseAsset, &selectedQuoteAsset](const TradablePair &pair)
                        { return pair.baseAsset == selectedBaseAsset && pair.quoteAsset == selectedQuoteAsset; });
    if (selectedPair == pairs.end())
    {
        return nullopt;
    }
    return *selectedPair;
}

OperationType OrderEntryForm::getSelectedOperation() const
{
    return static_cast<OperationType>(operationSelector->currentData().toInt());
}

optional<BasicOrderDraft> OrderEntryForm::createBasicOrderDraft() const
{
    const optional<TradablePair> selectedPair = getSelectedPair();
    const SymbolInfo *symbolInfo = getSelectedSymbolInfo();
    if (!selectedPair.has_value() || symbolInfo == nullptr || !hasReadySelectedBalances() ||
        getSelectedOperation() == OperationType::PLACE_OCO)
    {
        return nullopt;
    }

    const DecimalInputValidation amountValidation =
        OrderInputValidation::validateQuantity(amountField->getInput().text(), *symbolInfo);
    if (!amountValidation.isValid())
    {
        return nullopt;
    }

    BasicOrderDraft draft;
    draft.exchangerType = getSelectedExchangerType();
    draft.operation = getSelectedOperation();
    draft.pair = selectedPair.value();
    draft.quantity = amountValidation.value.value();
    draft.quantityText = DecimalConverter::formatByStep(draft.quantity, symbolInfo->stepSize);

    switch (draft.operation)
    {
    case OperationType::BUY_CRYPTO:
        draft.side = OrderOperation::BUY;
        draft.type = OrderType::MARKET;
        break;
    case OperationType::SELL_CRYPTO:
        draft.side = OrderOperation::SELL;
        draft.type = OrderType::MARKET;
        break;
    case OperationType::PLACE_ORDER:
        draft.side = static_cast<OrderOperation>(placeOrderSideSelector->currentData().toInt());
        draft.type = static_cast<OrderType>(placeOrderTypeSelector->currentData().toInt());
        if (draft.type == OrderType::LIMIT)
        {
            const DecimalInputValidation priceValidation =
                OrderInputValidation::validatePrice(placeOrderPriceField->getInput().text(), *symbolInfo);
            if (!priceValidation.isValid() ||
                !OrderInputValidation::validateNotional(draft.quantity, priceValidation.value.value(), *symbolInfo)
                     .isEmpty())
            {
                return nullopt;
            }
            draft.price = priceValidation.value.value();
            draft.priceText = DecimalConverter::formatByStep(draft.price.value(), symbolInfo->tickSize);
            draft.timeInForce = "GTC";
        }
        break;
    default:
        return nullopt;
    }
    return draft;
}

optional<OcoOrderDraft> OrderEntryForm::createOcoOrderDraft() const
{
    const optional<TradablePair> selectedPair = getSelectedPair();
    const SymbolInfo *symbolInfo = getSelectedSymbolInfo();
    if (!selectedPair.has_value() || symbolInfo == nullptr || !hasReadySelectedBalances() ||
        getSelectedOperation() != OperationType::PLACE_OCO)
    {
        return nullopt;
    }

    const DecimalInputValidation amountValidation =
        OrderInputValidation::validateQuantity(amountField->getInput().text(), *symbolInfo);
    const DecimalInputValidation limitPriceValidation =
        OrderInputValidation::validatePrice(ocoLimitPriceField->getInput().text(), *symbolInfo);
    const DecimalInputValidation stopPriceValidation =
        OrderInputValidation::validatePrice(ocoStopPriceField->getInput().text(), *symbolInfo);
    if (!amountValidation.isValid() || !limitPriceValidation.isValid() || !stopPriceValidation.isValid() ||
        !OrderInputValidation::validateNotional(amountValidation.value.value(),
                                                limitPriceValidation.value.value(),
                                                *symbolInfo)
             .isEmpty())
    {
        return nullopt;
    }
    if (!hasActiveOcoStopLimitPrice() && !OrderInputValidation::validateNotional(amountValidation.value.value(),
                                                                                 stopPriceValidation.value.value(),
                                                                                 *symbolInfo)
                                              .isEmpty())
    {
        return nullopt;
    }

    OcoOrderDraft draft;
    draft.exchangerType = getSelectedExchangerType();
    draft.pair = selectedPair.value();
    draft.side = static_cast<OrderOperation>(ocoSideSelector->currentData().toInt());
    draft.quantity = amountValidation.value.value();
    draft.limitPrice = limitPriceValidation.value.value();
    draft.stopPrice = stopPriceValidation.value.value();
    draft.quantityText = DecimalConverter::formatByStep(draft.quantity, symbolInfo->stepSize);
    draft.limitPriceText = DecimalConverter::formatByStep(draft.limitPrice, symbolInfo->tickSize);
    draft.stopPriceText = DecimalConverter::formatByStep(draft.stopPrice, symbolInfo->tickSize);

    if (hasActiveOcoStopLimitPrice())
    {
        const DecimalInputValidation stopLimitPriceValidation =
            OrderInputValidation::validatePrice(ocoStopLimitPriceField->getInput().text(), *symbolInfo);
        if (!stopLimitPriceValidation.isValid() ||
            !OrderInputValidation::validateNotional(draft.quantity, stopLimitPriceValidation.value.value(), *symbolInfo)
                 .isEmpty())
        {
            return nullopt;
        }
        draft.stopLimitPrice = stopLimitPriceValidation.value.value();
        draft.stopLimitTimeInForce = "GTC";
        draft.stopLimitPriceText = DecimalConverter::formatByStep(draft.stopLimitPrice.value(), symbolInfo->tickSize);
    }
    return draft;
}

void OrderEntryForm::createLayout()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(ORDER_FORM_MARGIN, ORDER_FORM_MARGIN, ORDER_FORM_MARGIN, ORDER_FORM_MARGIN);
    layout->setSpacing(ORDER_FORM_SECTION_SPACING);

    auto *contextLayout = new QGridLayout();
    contextLayout->setHorizontalSpacing(ORDER_FORM_COLUMN_SPACING);
    contextLayout->setVerticalSpacing(ORDER_FORM_ROW_SPACING);

    exchangeSelector = new QComboBox(this);
    exchangeSelector->setObjectName("orderExchangeSelector");
    exchangeSelector->setProperty("orderInput", true);
    exchangeSelector->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    exchangeSelector->addItem("Binance", static_cast<int>(ExchangerType::BINANCE));
    exchangeSelector->addItem("Bybit", static_cast<int>(ExchangerType::BYBIT));

    categoryField = new QLineEdit("SPOT", this);
    categoryField->setObjectName("orderCategoryField");
    categoryField->setProperty("orderInput", true);
    categoryField->setProperty("readOnlyContext", true);
    categoryField->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    categoryField->setReadOnly(true);

    contextLayout->addWidget(createFieldLabel("Exchange", *this), 0, 0);
    contextLayout->addWidget(exchangeSelector, 1, 0);
    contextLayout->addWidget(createFieldLabel("Market", *this), 0, 1);
    contextLayout->addWidget(categoryField, 1, 1);
    contextLayout->setColumnStretch(0, 1);
    contextLayout->setColumnStretch(1, 1);

    selectedCatalogStatus = new QLabel(this);
    selectedCatalogStatus->setObjectName("selectedPairCatalogStatus");
    selectedCatalogStatus->setProperty("selectedCatalogStatus", true);
    selectedCatalogStatus->setTextFormat(Qt::PlainText);
    selectedCatalogStatus->setWordWrap(true);

    auto *balanceStatusLayout = new QHBoxLayout();
    balanceStatusLayout->setContentsMargins(0, 0, 0, 0);
    balanceStatusLayout->setSpacing(ORDER_FORM_FEEDBACK_SPACING);

    selectedBalanceStatus = new QLabel(this);
    selectedBalanceStatus->setObjectName("selectedBalanceStatus");
    selectedBalanceStatus->setProperty("symbolInfoStatus", true);
    selectedBalanceStatus->setTextFormat(Qt::PlainText);
    selectedBalanceStatus->setWordWrap(true);

    retryBalanceButton = new QPushButton("Retry", this);
    retryBalanceButton->setObjectName("retryBalanceButton");
    retryBalanceButton->setProperty("secondaryOrderAction", true);
    retryBalanceButton->hide();

    balanceStatusLayout->addWidget(selectedBalanceStatus, 1);
    balanceStatusLayout->addWidget(retryBalanceButton);

    pairDependentControls = new QWidget(this);
    pairDependentControls->setObjectName("orderPairDependentControls");

    auto *pairDependentLayout = new QVBoxLayout(pairDependentControls);
    pairDependentLayout->setContentsMargins(0, 0, 0, 0);
    pairDependentLayout->setSpacing(ORDER_FORM_SECTION_SPACING);

    auto *pairLayout = new QGridLayout();
    pairLayout->setHorizontalSpacing(ORDER_FORM_COLUMN_SPACING);
    pairLayout->setVerticalSpacing(ORDER_FORM_ROW_SPACING);

    baseAssetSelector = new QComboBox(pairDependentControls);
    baseAssetSelector->setObjectName("orderBaseAssetSelector");
    baseAssetSelector->setProperty("orderInput", true);
    baseAssetSelector->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);

    quoteAssetSelector = new QComboBox(pairDependentControls);
    quoteAssetSelector->setObjectName("orderQuoteAssetSelector");
    quoteAssetSelector->setProperty("orderInput", true);
    quoteAssetSelector->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);

    operationSelector = new QComboBox(pairDependentControls);
    operationSelector->setObjectName("orderOperationSelector");
    operationSelector->setProperty("orderInput", true);
    operationSelector->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    operationSelector->addItem("Buy crypto", static_cast<int>(OperationType::BUY_CRYPTO));
    operationSelector->addItem("Sell crypto", static_cast<int>(OperationType::SELL_CRYPTO));
    operationSelector->addItem("Custom order", static_cast<int>(OperationType::PLACE_ORDER));
    operationSelector->addItem("Place OCO", static_cast<int>(OperationType::PLACE_OCO));

    pairLayout->addWidget(createFieldLabel("Base asset", *pairDependentControls), 0, 0);
    pairLayout->addWidget(baseAssetSelector, 1, 0);
    pairLayout->addWidget(createFieldLabel("Quote asset", *pairDependentControls), 0, 1);
    pairLayout->addWidget(quoteAssetSelector, 1, 1);
    pairLayout->addWidget(createFieldLabel("Operation", *pairDependentControls), 2, 0, 1, 2);
    pairLayout->addWidget(operationSelector, 3, 0, 1, 2);
    pairLayout->setColumnStretch(0, 1);
    pairLayout->setColumnStretch(1, 1);

    auto *symbolStatusLayout = new QHBoxLayout();
    symbolStatusLayout->setContentsMargins(0, 0, 0, 0);
    symbolStatusLayout->setSpacing(ORDER_FORM_FEEDBACK_SPACING);

    selectedSymbolInfoStatus = new QLabel(pairDependentControls);
    selectedSymbolInfoStatus->setObjectName("selectedSymbolInfoStatus");
    selectedSymbolInfoStatus->setProperty("symbolInfoStatus", true);
    selectedSymbolInfoStatus->setTextFormat(Qt::PlainText);
    selectedSymbolInfoStatus->setWordWrap(true);

    retrySymbolInfoButton = new QPushButton("Retry", pairDependentControls);
    retrySymbolInfoButton->setObjectName("retrySymbolInfoButton");
    retrySymbolInfoButton->setProperty("secondaryOrderAction", true);
    retrySymbolInfoButton->hide();

    symbolStatusLayout->addWidget(selectedSymbolInfoStatus, 1);
    symbolStatusLayout->addWidget(retrySymbolInfoButton);

    tradingLimits = new QLabel(pairDependentControls);
    tradingLimits->setObjectName("orderTradingLimits");
    tradingLimits->setProperty("tradingLimits", true);
    tradingLimits->setTextFormat(Qt::PlainText);
    tradingLimits->setWordWrap(true);
    tradingLimits->hide();

    auto *divider = new QFrame(pairDependentControls);
    divider->setProperty("orderFormDivider", true);
    divider->setFrameShape(QFrame::HLine);

    amountLabel = createFieldLabel("Amount (base asset)", *pairDependentControls);
    amountLabel->setObjectName("orderAmountLabel");
    amountField = new DecimalInputField("orderAmount", "orderAmountInput", pairDependentControls);

    auto *amountLayout = new QFormLayout();
    amountLayout->setContentsMargins(0, 0, 0, 0);
    amountLayout->setHorizontalSpacing(ORDER_FORM_COLUMN_SPACING);
    amountLayout->setVerticalSpacing(ORDER_FORM_ROW_SPACING);
    amountLayout->addRow(amountLabel, amountField);

    operationFormStack = new QStackedWidget(pairDependentControls);
    operationFormStack->setObjectName("orderOperationFormStack");
    operationFormStack->addWidget(
        createMarketOperationFields("Buy the entered base-asset amount at the current market price.",
                                    "buyCryptoFields"));
    operationFormStack->addWidget(
        createMarketOperationFields("Sell the entered base-asset amount at the current market price.",
                                    "sellCryptoFields"));
    operationFormStack->addWidget(createPlaceOrderFields());
    operationFormStack->addWidget(createOcoFields());

    formValidationError = new QLabel(pairDependentControls);
    formValidationError->setObjectName("orderFormValidationError");
    formValidationError->setProperty("validationError", true);
    formValidationError->setTextFormat(Qt::PlainText);
    formValidationError->setWordWrap(true);
    formValidationError->hide();

    proceedButton = new QPushButton("Proceed to confirmation", pairDependentControls);
    proceedButton->setObjectName("orderProceedButton");
    proceedButton->setProperty("primaryOrderAction", true);
    proceedButton->setMinimumHeight(ORDER_FORM_ACTION_MINIMUM_HEIGHT);
    proceedButton->setEnabled(false);

    pairDependentLayout->addLayout(pairLayout);
    pairDependentLayout->addLayout(symbolStatusLayout);
    pairDependentLayout->addWidget(tradingLimits);
    pairDependentLayout->addWidget(divider);
    pairDependentLayout->addLayout(amountLayout);
    pairDependentLayout->addWidget(operationFormStack);
    pairDependentLayout->addWidget(formValidationError);
    pairDependentLayout->addWidget(proceedButton, 0, Qt::AlignRight);

    layout->addLayout(contextLayout);
    layout->addWidget(selectedCatalogStatus);
    layout->addLayout(balanceStatusLayout);
    layout->addWidget(pairDependentControls);

    updateOperationFields();
    updatePlaceOrderTypeFields();
    updateOcoStopLimitFields();
}

QWidget *OrderEntryForm::createMarketOperationFields(const QString &description, const QString &objectName)
{
    auto *fields = new QWidget(operationFormStack);
    fields->setObjectName(objectName);

    auto *layout = new QVBoxLayout(fields);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *descriptionLabel = new QLabel(description, fields);
    descriptionLabel->setProperty("operationDescription", true);
    descriptionLabel->setWordWrap(true);
    layout->addWidget(descriptionLabel);
    return fields;
}

QWidget *OrderEntryForm::createPlaceOrderFields()
{
    auto *fields = new QWidget(operationFormStack);
    fields->setObjectName("placeOrderFields");

    auto *layout = new QVBoxLayout(fields);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ORDER_FORM_ROW_SPACING);

    auto *mainFields = new QFormLayout();
    mainFields->setContentsMargins(0, 0, 0, 0);
    mainFields->setHorizontalSpacing(ORDER_FORM_COLUMN_SPACING);
    mainFields->setVerticalSpacing(ORDER_FORM_ROW_SPACING);

    placeOrderSideSelector = new QComboBox(fields);
    placeOrderSideSelector->setObjectName("placeOrderSideSelector");
    placeOrderSideSelector->setProperty("orderInput", true);
    placeOrderSideSelector->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    placeOrderSideSelector->addItem("Buy", static_cast<int>(OrderOperation::BUY));
    placeOrderSideSelector->addItem("Sell", static_cast<int>(OrderOperation::SELL));

    placeOrderTypeSelector = new QComboBox(fields);
    placeOrderTypeSelector->setObjectName("placeOrderTypeSelector");
    placeOrderTypeSelector->setProperty("orderInput", true);
    placeOrderTypeSelector->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    placeOrderTypeSelector->addItem("Market", static_cast<int>(OrderType::MARKET));
    placeOrderTypeSelector->addItem("Limit", static_cast<int>(OrderType::LIMIT));

    mainFields->addRow(createFieldLabel("Side", *fields), placeOrderSideSelector);
    mainFields->addRow(createFieldLabel("Order type", *fields), placeOrderTypeSelector);

    placeOrderLimitFields = new QWidget(fields);
    placeOrderLimitFields->setObjectName("placeOrderLimitFields");

    auto *limitLayout = new QFormLayout(placeOrderLimitFields);
    limitLayout->setContentsMargins(0, 0, 0, 0);
    limitLayout->setHorizontalSpacing(ORDER_FORM_COLUMN_SPACING);
    limitLayout->setVerticalSpacing(ORDER_FORM_ROW_SPACING);

    placeOrderPriceLabel = createFieldLabel("Price (quote per base)", *placeOrderLimitFields);
    placeOrderPriceLabel->setObjectName("placeOrderPriceLabel");
    placeOrderPriceField = new DecimalInputField("placeOrderPrice", "placeOrderPriceInput", placeOrderLimitFields);

    auto *timeInForceField = new QLineEdit("GTC", placeOrderLimitFields);
    timeInForceField->setObjectName("placeOrderTimeInForceField");
    timeInForceField->setProperty("orderInput", true);
    timeInForceField->setProperty("readOnlyContext", true);
    timeInForceField->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    timeInForceField->setReadOnly(true);

    limitLayout->addRow(placeOrderPriceLabel, placeOrderPriceField);
    limitLayout->addRow(createFieldLabel("Time in force", *placeOrderLimitFields), timeInForceField);

    layout->addLayout(mainFields);
    layout->addWidget(placeOrderLimitFields);
    return fields;
}

QWidget *OrderEntryForm::createOcoFields()
{
    auto *fields = new QWidget(operationFormStack);
    fields->setObjectName("placeOcoFields");

    auto *layout = new QVBoxLayout(fields);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ORDER_FORM_ROW_SPACING);

    auto *mainFields = new QFormLayout();
    mainFields->setContentsMargins(0, 0, 0, 0);
    mainFields->setHorizontalSpacing(ORDER_FORM_COLUMN_SPACING);
    mainFields->setVerticalSpacing(ORDER_FORM_ROW_SPACING);

    ocoSideSelector = new QComboBox(fields);
    ocoSideSelector->setObjectName("placeOcoSideSelector");
    ocoSideSelector->setProperty("orderInput", true);
    ocoSideSelector->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    ocoSideSelector->addItem("Buy", static_cast<int>(OrderOperation::BUY));
    ocoSideSelector->addItem("Sell", static_cast<int>(OrderOperation::SELL));

    ocoLimitPriceLabel = createFieldLabel("Limit price (quote per base)", *fields);
    ocoLimitPriceLabel->setObjectName("placeOcoLimitPriceLabel");
    ocoLimitPriceField = new DecimalInputField("placeOcoLimitPrice", "placeOcoLimitPriceInput", fields);

    ocoStopPriceLabel = createFieldLabel("Stop price (quote per base)", *fields);
    ocoStopPriceLabel->setObjectName("placeOcoStopPriceLabel");
    ocoStopPriceField = new DecimalInputField("placeOcoStopPrice", "placeOcoStopPriceInput", fields);

    useOcoStopLimit = new QCheckBox("Use a stop-limit price", fields);
    useOcoStopLimit->setObjectName("placeOcoUseStopLimit");

    mainFields->addRow(createFieldLabel("Side", *fields), ocoSideSelector);
    mainFields->addRow(ocoLimitPriceLabel, ocoLimitPriceField);
    mainFields->addRow(ocoStopPriceLabel, ocoStopPriceField);
    mainFields->addRow(QString(), useOcoStopLimit);

    ocoStopLimitFields = new QWidget(fields);
    ocoStopLimitFields->setObjectName("placeOcoStopLimitFields");

    auto *stopLimitLayout = new QFormLayout(ocoStopLimitFields);
    stopLimitLayout->setContentsMargins(0, 0, 0, 0);
    stopLimitLayout->setHorizontalSpacing(ORDER_FORM_COLUMN_SPACING);
    stopLimitLayout->setVerticalSpacing(ORDER_FORM_ROW_SPACING);

    ocoStopLimitPriceLabel = createFieldLabel("Stop-limit price (quote per base)", *ocoStopLimitFields);
    ocoStopLimitPriceLabel->setObjectName("placeOcoStopLimitPriceLabel");
    ocoStopLimitPriceField =
        new DecimalInputField("placeOcoStopLimitPrice", "placeOcoStopLimitPriceInput", ocoStopLimitFields);

    auto *timeInForceField = new QLineEdit("GTC", ocoStopLimitFields);
    timeInForceField->setObjectName("placeOcoStopLimitTimeInForceField");
    timeInForceField->setProperty("orderInput", true);
    timeInForceField->setProperty("readOnlyContext", true);
    timeInForceField->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    timeInForceField->setReadOnly(true);

    stopLimitLayout->addRow(ocoStopLimitPriceLabel, ocoStopLimitPriceField);
    stopLimitLayout->addRow(createFieldLabel("Time in force", *ocoStopLimitFields), timeInForceField);

    layout->addLayout(mainFields);
    layout->addWidget(ocoStopLimitFields);
    return fields;
}

QLabel *OrderEntryForm::createFieldLabel(const QString &text, QWidget &parent) const
{
    auto *label = new QLabel(text, &parent);
    label->setProperty("orderFieldLabel", true);
    return label;
}

void OrderEntryForm::connectCatalogUpdates()
{
    connect(&pairCatalog,
            &PairCatalog::pairsChanged,
            this,
            [this](ExchangerType exchangerType)
            {
                if (exchangerType == getSelectedExchangerType())
                {
                    updateSelectedCatalog();
                }
            });

    for (const ExchangerType exchangerType : {ExchangerType::BINANCE, ExchangerType::BYBIT})
    {
        connect(&pairCatalog.getLoadState(exchangerType),
                &UiTaskState::statusChanged,
                this,
                [this, exchangerType](UiTaskState::Status)
                {
                    if (exchangerType == getSelectedExchangerType())
                    {
                        updateSelectedCatalog();
                    }
                });

        connect(&balanceCatalog.getLoadState(exchangerType),
                &UiTaskState::statusChanged,
                this,
                [this, exchangerType](UiTaskState::Status)
                {
                    if (exchangerType == getSelectedExchangerType())
                    {
                        updateSelectedBalance();
                    }
                });
    }

    connect(&symbolInfoCatalog,
            &SymbolInfoCatalog::symbolInfoChanged,
            this,
            [this](ExchangerType exchangerType, const QString &symbol)
            {
                const optional<TradablePair> selectedPair = getSelectedPair();
                if (selectedPair.has_value() && exchangerType == getSelectedExchangerType() &&
                    symbol == QString::fromStdString(selectedPair.value().symbol))
                {
                    updateSelectedSymbolInfo();
                }
            });
}

void OrderEntryForm::connectInputUpdates()
{
    connect(exchangeSelector,
            &QComboBox::currentIndexChanged,
            this,
            [this](int)
            {
                updateSelectedCatalog();
                updateSelectedBalance();
            });
    connect(baseAssetSelector,
            &QComboBox::currentIndexChanged,
            this,
            [this](int)
            {
                const QString preferredQuoteAsset = quoteAssetSelector->currentText();
                updateQuoteAssets(preferredQuoteAsset);
                updatePairLabels();
                updateSelectedSymbolInfo();
            });
    connect(quoteAssetSelector,
            &QComboBox::currentIndexChanged,
            this,
            [this](int)
            {
                updatePairLabels();
                updateSelectedSymbolInfo();
            });
    connect(operationSelector, &QComboBox::currentIndexChanged, this, [this](int) { updateOperationFields(); });
    connect(placeOrderTypeSelector,
            &QComboBox::currentIndexChanged,
            this,
            [this](int) { updatePlaceOrderTypeFields(); });
    connect(useOcoStopLimit, &QCheckBox::toggled, this, [this](bool) { updateOcoStopLimitFields(); });
    connect(proceedButton, &QPushButton::clicked, this, &OrderEntryForm::requestConfirmation);

    for (DecimalInputField *field :
         {amountField, placeOrderPriceField, ocoLimitPriceField, ocoStopPriceField, ocoStopLimitPriceField})
    {
        connect(field, &DecimalInputField::inputChanged, this, [this](const QString &) { validateForm(); });
        connect(field, &DecimalInputField::requestValidation, this, [this]() { validateForm(); });
    }

    connect(retrySymbolInfoButton,
            &QPushButton::clicked,
            this,
            [this]()
            {
                const optional<TradablePair> selectedPair = getSelectedPair();
                if (selectedPair.has_value())
                {
                    symbolInfoCatalog.retrySymbolInfo(getSelectedExchangerType(), selectedPair.value().symbol);
                    updateSelectedSymbolInfo();
                }
            });

    connect(retryBalanceButton,
            &QPushButton::clicked,
            this,
            [this]()
            {
                balanceCatalog.retryBalances(getSelectedExchangerType());
                updateSelectedBalance();
            });
}

void OrderEntryForm::updateSelectedCatalog()
{
    const UiTaskState &loadState = pairCatalog.getLoadState(getSelectedExchangerType());
    const QString exchangeName = getSelectedExchangeName();
    const qsizetype pairCount = static_cast<qsizetype>(pairCatalog.getPairs(getSelectedExchangerType()).size());

    switch (loadState.getStatus())
    {
    case UiTaskState::Status::IDLE:
        selectedCatalogStatus->setText(exchangeName + " pair selection is waiting for the startup catalog.");
        break;
    case UiTaskState::Status::LOADING:
        selectedCatalogStatus->setText(exchangeName + " pairs are loading. Pair controls will become available soon.");
        break;
    case UiTaskState::Status::SUCCEEDED:
        selectedCatalogStatus->setText(pairCount == 0 ? exchangeName + " has no tradable SPOT pairs available."
                                                      : QString("%1 pair selection is ready (%2 %3).")
                                                            .arg(exchangeName)
                                                            .arg(pairCount)
                                                            .arg(pairCount == 1 ? "pair" : "pairs"));
        break;
    case UiTaskState::Status::FAILED:
        selectedCatalogStatus->setText(exchangeName + " pairs are unavailable: " + loadState.getError());
        break;
    }
    updatePairSelectors();
}

void OrderEntryForm::updateSelectedBalance()
{
    const UiTaskState &loadState = balanceCatalog.getLoadState(getSelectedExchangerType());
    const QString exchangeName = getSelectedExchangeName();
    retryBalanceButton->hide();

    switch (loadState.getStatus())
    {
    case UiTaskState::Status::IDLE:
        selectedBalanceStatus->setText(exchangeName + " balances are waiting for the startup load.");
        break;
    case UiTaskState::Status::LOADING:
        selectedBalanceStatus->setText("Loading " + exchangeName + " balances required for placement...");
        break;
    case UiTaskState::Status::SUCCEEDED:
        selectedBalanceStatus->setText(exchangeName + " balances are ready for placement.");
        break;
    case UiTaskState::Status::FAILED:
        selectedBalanceStatus->setText(exchangeName + " balances are unavailable: " + loadState.getError());
        retryBalanceButton->show();
        break;
    }
    validateForm();
}

void OrderEntryForm::updatePairSelectors()
{
    const QString preferredBaseAsset = baseAssetSelector->currentText();
    const QString preferredQuoteAsset = quoteAssetSelector->currentText();
    const QSignalBlocker baseAssetBlocker(baseAssetSelector);
    const QSignalBlocker quoteAssetBlocker(quoteAssetSelector);

    baseAssetSelector->clear();
    quoteAssetSelector->clear();
    pairDependentControls->setEnabled(hasUsableSelectedCatalog());

    if (!hasUsableSelectedCatalog())
    {
        updatePairLabels();
        updateSelectedSymbolInfo();
        return;
    }

    for (const TradablePair &pair : pairCatalog.getPairs(getSelectedExchangerType()))
    {
        const QString baseAsset = QString::fromStdString(pair.baseAsset);
        if (baseAssetSelector->findText(baseAsset) < 0)
        {
            baseAssetSelector->addItem(baseAsset);
        }
    }

    const int preferredBaseIndex = baseAssetSelector->findText(preferredBaseAsset);
    baseAssetSelector->setCurrentIndex(preferredBaseIndex >= 0 ? preferredBaseIndex : 0);
    updateQuoteAssets(preferredQuoteAsset);
    updatePairLabels();
    updateSelectedSymbolInfo();
}

void OrderEntryForm::updateQuoteAssets(const QString &preferredQuoteAsset)
{
    const QSignalBlocker quoteAssetBlocker(quoteAssetSelector);
    quoteAssetSelector->clear();

    if (!hasUsableSelectedCatalog() || baseAssetSelector->currentIndex() < 0)
    {
        return;
    }

    const string selectedBaseAsset = baseAssetSelector->currentText().toStdString();
    for (const TradablePair &pair : pairCatalog.getPairs(getSelectedExchangerType()))
    {
        if (pair.baseAsset != selectedBaseAsset)
        {
            continue;
        }

        const QString quoteAsset = QString::fromStdString(pair.quoteAsset);
        if (quoteAssetSelector->findText(quoteAsset) < 0)
        {
            quoteAssetSelector->addItem(quoteAsset);
        }
    }

    const int preferredQuoteIndex = quoteAssetSelector->findText(preferredQuoteAsset);
    quoteAssetSelector->setCurrentIndex(preferredQuoteIndex >= 0 ? preferredQuoteIndex : 0);
}

void OrderEntryForm::updateOperationFields()
{
    operationFormStack->setCurrentIndex(operationSelector->currentIndex());
    validateForm();
}

void OrderEntryForm::updatePlaceOrderTypeFields()
{
    placeOrderLimitFields->setVisible(hasActivePlaceOrderPrice());
    validateForm();
}

void OrderEntryForm::updateOcoStopLimitFields()
{
    ocoStopLimitFields->setVisible(hasActiveOcoStopLimitPrice());
    validateForm();
}

void OrderEntryForm::updatePairLabels()
{
    const QString baseAsset = baseAssetSelector->currentText();
    const QString quoteAsset = quoteAssetSelector->currentText();
    const QString amountAsset = baseAsset.isEmpty() ? "base asset" : baseAsset;
    const QString priceUnit = baseAsset.isEmpty() || quoteAsset.isEmpty()
                                  ? "quote per base"
                                  : QString("%1 per %2").arg(quoteAsset, baseAsset);

    amountLabel->setText("Amount (" + amountAsset + ")");
    amountField->getInput().setPlaceholderText(baseAsset.isEmpty() ? "Base-asset quantity" : "Amount in " + baseAsset);
    placeOrderPriceLabel->setText("Price (" + priceUnit + ")");
    ocoLimitPriceLabel->setText("Limit price (" + priceUnit + ")");
    ocoStopPriceLabel->setText("Stop price (" + priceUnit + ")");
    ocoStopLimitPriceLabel->setText("Stop-limit price (" + priceUnit + ")");
}

void OrderEntryForm::updateSelectedSymbolInfo()
{
    retrySymbolInfoButton->hide();
    tradingLimits->hide();
    formValidationError->hide();
    proceedButton->setEnabled(false);
    updateInputAvailability(false);
    amountField->clearValidation();
    clearPriceValidation();

    const optional<TradablePair> selectedPair = getSelectedPair();
    if (!selectedPair.has_value())
    {
        selectedSymbolInfoStatus->setText("Select an available pair to load its trading rules.");
        return;
    }

    const ExchangerType exchangerType = getSelectedExchangerType();
    const string &symbol = selectedPair.value().symbol;
    symbolInfoCatalog.loadSymbolInfo(exchangerType, symbol);
    const UiTaskState *loadState = symbolInfoCatalog.getLoadState(exchangerType, symbol);
    if (loadState == nullptr)
    {
        selectedSymbolInfoStatus->setText("Trading rules could not be started for " + QString::fromStdString(symbol) +
                                          ".");
        return;
    }

    switch (loadState->getStatus())
    {
    case UiTaskState::Status::IDLE:
        selectedSymbolInfoStatus->setText("Waiting to load trading rules for " + QString::fromStdString(symbol) + ".");
        break;
    case UiTaskState::Status::LOADING:
        selectedSymbolInfoStatus->setText("Loading trading rules for " + QString::fromStdString(symbol) + "...");
        break;
    case UiTaskState::Status::FAILED:
        selectedSymbolInfoStatus->setText("Trading rules are unavailable for " + QString::fromStdString(symbol) + ": " +
                                          loadState->getError());
        retrySymbolInfoButton->show();
        break;
    case UiTaskState::Status::SUCCEEDED:
        const SymbolInfo *symbolInfo = symbolInfoCatalog.getSymbolInfo(exchangerType, symbol);
        if (symbolInfo == nullptr)
        {
            selectedSymbolInfoStatus->setText("Trading rules returned no details for " +
                                              QString::fromStdString(symbol) + ".");
            break;
        }
        selectedSymbolInfoStatus->setText("Trading rules are ready for " + QString::fromStdString(symbol) + ".");
        updateTradingLimits(*symbolInfo);
        updateInputAvailability(true);
        validateForm();
        break;
    }
}

void OrderEntryForm::updateTradingLimits(const SymbolInfo &symbolInfo)
{
    const optional<TradablePair> selectedPair = getSelectedPair();
    if (!selectedPair.has_value())
    {
        tradingLimits->hide();
        return;
    }

    QStringList quantityParts{"step " + formatByIncrement(symbolInfo.stepSize, symbolInfo.stepSize)};
    appendBound(quantityParts, "min", symbolInfo.minQty, symbolInfo.stepSize);
    appendBound(quantityParts, "max", symbolInfo.maxQty, symbolInfo.stepSize);

    QStringList priceParts{"tick " + formatByIncrement(symbolInfo.tickSize, symbolInfo.tickSize)};
    appendBound(priceParts, "min", symbolInfo.minPrice, symbolInfo.tickSize);
    appendBound(priceParts, "max", symbolInfo.maxPrice, symbolInfo.tickSize);

    QStringList lines;
    lines.push_back("Amount (" + QString::fromStdString(selectedPair.value().baseAsset) +
                    "): " + quantityParts.join(" · "));
    lines.push_back("Price (" + QString::fromStdString(selectedPair.value().quoteAsset) +
                    "): " + priceParts.join(" · "));

    QStringList notionalParts;
    if (symbolInfo.minNotional > 0)
    {
        notionalParts.push_back("min " + formatDecimal(symbolInfo.minNotional));
    }
    if (symbolInfo.maxNotional > 0)
    {
        notionalParts.push_back("max " + formatDecimal(symbolInfo.maxNotional));
    }
    if (!notionalParts.empty())
    {
        lines.push_back("Notional (" + QString::fromStdString(selectedPair.value().quoteAsset) +
                        "): " + notionalParts.join(" · "));
    }

    tradingLimits->setText(lines.join("\n"));
    tradingLimits->show();
}

void OrderEntryForm::updateInputAvailability(bool enabled)
{
    amountField->setInputEnabled(enabled);
    placeOrderPriceField->setInputEnabled(enabled);
    ocoLimitPriceField->setInputEnabled(enabled);
    ocoStopPriceField->setInputEnabled(enabled);
    ocoStopLimitPriceField->setInputEnabled(enabled);
}

void OrderEntryForm::validateForm()
{
    const SymbolInfo *symbolInfo = getSelectedSymbolInfo();
    if (symbolInfo == nullptr)
    {
        proceedButton->setEnabled(false);
        return;
    }

    const DecimalInputValidation amountValidation =
        OrderInputValidation::validateQuantity(amountField->getInput().text(), *symbolInfo);
    bool formIsValid = showFieldValidation(*amountField, amountValidation, symbolInfo->stepSize);

    DecimalInputValidation placeOrderPriceValidation;
    DecimalInputValidation ocoLimitPriceValidation;
    DecimalInputValidation ocoStopPriceValidation;
    DecimalInputValidation ocoStopLimitPriceValidation;

    switch (getSelectedOperation())
    {
    case OperationType::BUY_CRYPTO:
    case OperationType::SELL_CRYPTO:
        clearPriceValidation();
        break;
    case OperationType::PLACE_ORDER:
        ocoLimitPriceField->clearValidation();
        ocoStopPriceField->clearValidation();
        ocoStopLimitPriceField->clearValidation();
        if (hasActivePlaceOrderPrice())
        {
            placeOrderPriceValidation =
                OrderInputValidation::validatePrice(placeOrderPriceField->getInput().text(), *symbolInfo);
            formIsValid = showFieldValidation(*placeOrderPriceField, placeOrderPriceValidation, symbolInfo->tickSize) &&
                          formIsValid;
        }
        else
        {
            placeOrderPriceField->clearValidation();
        }
        break;
    case OperationType::PLACE_OCO:
        placeOrderPriceField->clearValidation();
        ocoLimitPriceValidation =
            OrderInputValidation::validatePrice(ocoLimitPriceField->getInput().text(), *symbolInfo);
        ocoStopPriceValidation = OrderInputValidation::validatePrice(ocoStopPriceField->getInput().text(), *symbolInfo);
        formIsValid =
            showFieldValidation(*ocoLimitPriceField, ocoLimitPriceValidation, symbolInfo->tickSize) && formIsValid;
        formIsValid =
            showFieldValidation(*ocoStopPriceField, ocoStopPriceValidation, symbolInfo->tickSize) && formIsValid;
        if (hasActiveOcoStopLimitPrice())
        {
            ocoStopLimitPriceValidation =
                OrderInputValidation::validatePrice(ocoStopLimitPriceField->getInput().text(), *symbolInfo);
            formIsValid =
                showFieldValidation(*ocoStopLimitPriceField, ocoStopLimitPriceValidation, symbolInfo->tickSize) &&
                formIsValid;
        }
        else
        {
            ocoStopLimitPriceField->clearValidation();
        }
        break;
    default:
        clearPriceValidation();
        formIsValid = false;
        break;
    }

    const QString notionalError = validateActiveNotional(*symbolInfo,
                                                         amountValidation,
                                                         placeOrderPriceValidation,
                                                         ocoLimitPriceValidation,
                                                         ocoStopPriceValidation,
                                                         ocoStopLimitPriceValidation);
    formValidationError->setText(notionalError);
    formValidationError->setVisible(shouldShowNotionalValidation(notionalError));
    proceedButton->setEnabled(formIsValid && notionalError.isEmpty() && hasReadySelectedBalances());
}

bool OrderEntryForm::showFieldValidation(DecimalInputField &field,
                                         const DecimalInputValidation &validation,
                                         Decimal increment)
{
    optional<QString> correction;
    if (validation.correction.has_value())
    {
        correction = formatByIncrement(validation.correction.value(), increment);
    }
    field.showValidation(validation.error, correction);
    return validation.isValid();
}

QString OrderEntryForm::validateActiveNotional(const SymbolInfo &symbolInfo,
                                               const DecimalInputValidation &amountValidation,
                                               const DecimalInputValidation &placeOrderPriceValidation,
                                               const DecimalInputValidation &ocoLimitPriceValidation,
                                               const DecimalInputValidation &ocoStopPriceValidation,
                                               const DecimalInputValidation &ocoStopLimitPriceValidation) const
{
    if (!amountValidation.isValid())
    {
        return {};
    }

    if (getSelectedOperation() == OperationType::PLACE_ORDER && hasActivePlaceOrderPrice() &&
        placeOrderPriceValidation.isValid())
    {
        return OrderInputValidation::validateNotional(amountValidation.value.value(),
                                                      placeOrderPriceValidation.value.value(),
                                                      symbolInfo);
    }
    if (getSelectedOperation() != OperationType::PLACE_OCO || !ocoLimitPriceValidation.isValid())
    {
        return {};
    }

    QString error = OrderInputValidation::validateNotional(amountValidation.value.value(),
                                                           ocoLimitPriceValidation.value.value(),
                                                           symbolInfo);
    if (!error.isEmpty())
    {
        return "Limit leg: " + error;
    }
    if (!hasActiveOcoStopLimitPrice() && ocoStopPriceValidation.isValid())
    {
        error = OrderInputValidation::validateNotional(amountValidation.value.value(),
                                                       ocoStopPriceValidation.value.value(),
                                                       symbolInfo);
        if (!error.isEmpty())
        {
            return "Stop leg: " + error;
        }
    }
    if (hasActiveOcoStopLimitPrice() && ocoStopLimitPriceValidation.isValid())
    {
        error = OrderInputValidation::validateNotional(amountValidation.value.value(),
                                                       ocoStopLimitPriceValidation.value.value(),
                                                       symbolInfo);
        if (!error.isEmpty())
        {
            return "Stop-limit leg: " + error;
        }
    }
    return {};
}

bool OrderEntryForm::shouldShowNotionalValidation(const QString &error) const
{
    if (error.isEmpty() || !amountField->hasValidationFeedbackEnabled())
    {
        return false;
    }
    if (getSelectedOperation() == OperationType::PLACE_ORDER)
    {
        return placeOrderPriceField->hasValidationFeedbackEnabled();
    }
    if (error.startsWith("Limit leg:"))
    {
        return ocoLimitPriceField->hasValidationFeedbackEnabled();
    }
    if (error.startsWith("Stop leg:"))
    {
        return ocoStopPriceField->hasValidationFeedbackEnabled();
    }
    if (error.startsWith("Stop-limit leg:"))
    {
        return ocoStopLimitPriceField->hasValidationFeedbackEnabled();
    }
    return false;
}

void OrderEntryForm::clearPriceValidation()
{
    placeOrderPriceField->clearValidation();
    ocoLimitPriceField->clearValidation();
    ocoStopPriceField->clearValidation();
    ocoStopLimitPriceField->clearValidation();
}

bool OrderEntryForm::hasUsableSelectedCatalog() const
{
    const ExchangerType exchangerType = getSelectedExchangerType();
    return pairCatalog.getLoadState(exchangerType).getStatus() == UiTaskState::Status::SUCCEEDED &&
           !pairCatalog.getPairs(exchangerType).empty();
}

bool OrderEntryForm::hasReadySelectedBalances() const
{
    return balanceCatalog.getLoadState(getSelectedExchangerType()).getStatus() == UiTaskState::Status::SUCCEEDED;
}

bool OrderEntryForm::hasActivePlaceOrderPrice() const
{
    return getSelectedOperation() == OperationType::PLACE_ORDER && placeOrderTypeSelector->currentText() == "Limit";
}

bool OrderEntryForm::hasActiveOcoStopLimitPrice() const
{
    return getSelectedOperation() == OperationType::PLACE_OCO && useOcoStopLimit->isChecked();
}

QString OrderEntryForm::getSelectedExchangeName() const
{
    return exchangeSelector->currentText();
}

const SymbolInfo *OrderEntryForm::getSelectedSymbolInfo() const
{
    const optional<TradablePair> selectedPair = getSelectedPair();
    if (!selectedPair.has_value())
    {
        return nullptr;
    }

    const UiTaskState *loadState =
        symbolInfoCatalog.getLoadState(getSelectedExchangerType(), selectedPair.value().symbol);
    if (loadState == nullptr || loadState->getStatus() != UiTaskState::Status::SUCCEEDED)
    {
        return nullptr;
    }
    return symbolInfoCatalog.getSymbolInfo(getSelectedExchangerType(), selectedPair.value().symbol);
}
