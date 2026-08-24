#include "OrderEntryForm.hpp"

#include "graphical/GuiLayoutConstants.hpp"
#include "graphical/async/UiTaskState.hpp"
#include "graphical/models/PairCatalog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <string>
#include <vector>

using namespace GuiLayoutConstants;
using namespace std;

OrderEntryForm::OrderEntryForm(PairCatalog &pairCatalog, QWidget *parent)
    : QWidget(parent), pairCatalog(pairCatalog), exchangeSelector(nullptr), categoryField(nullptr),
      selectedCatalogStatus(nullptr), pairDependentControls(nullptr), baseAssetSelector(nullptr),
      quoteAssetSelector(nullptr), operationSelector(nullptr), amountLabel(nullptr), amountInput(nullptr),
      operationFormStack(nullptr), placeOrderTypeSelector(nullptr), placeOrderLimitFields(nullptr),
      placeOrderPriceLabel(nullptr), placeOrderPriceInput(nullptr), useOcoStopLimit(nullptr),
      ocoStopLimitFields(nullptr), ocoLimitPriceLabel(nullptr), ocoStopPriceLabel(nullptr),
      ocoStopLimitPriceLabel(nullptr), ocoLimitPriceInput(nullptr), ocoStopPriceInput(nullptr),
      ocoStopLimitPriceInput(nullptr)
{
    setObjectName("orderEntryForm");
    createLayout();
    connectCatalogUpdates();
    updateSelectedCatalog();
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

    auto *divider = new QFrame(pairDependentControls);
    divider->setProperty("orderFormDivider", true);
    divider->setFrameShape(QFrame::HLine);

    amountLabel = createFieldLabel("Amount (base asset)", *pairDependentControls);
    amountLabel->setObjectName("orderAmountLabel");

    amountInput = new QLineEdit(pairDependentControls);
    amountInput->setObjectName("orderAmountInput");
    amountInput->setProperty("orderInput", true);
    amountInput->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);

    auto *amountLayout = new QFormLayout();
    amountLayout->setContentsMargins(0, 0, 0, 0);
    amountLayout->setHorizontalSpacing(ORDER_FORM_COLUMN_SPACING);
    amountLayout->setVerticalSpacing(ORDER_FORM_ROW_SPACING);
    amountLayout->addRow(amountLabel, amountInput);

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

    pairDependentLayout->addLayout(pairLayout);
    pairDependentLayout->addWidget(divider);
    pairDependentLayout->addLayout(amountLayout);
    pairDependentLayout->addWidget(operationFormStack);

    layout->addLayout(contextLayout);
    layout->addWidget(selectedCatalogStatus);
    layout->addWidget(pairDependentControls);

    connect(exchangeSelector, &QComboBox::currentIndexChanged, this, [this](int) { updateSelectedCatalog(); });
    connect(baseAssetSelector,
            &QComboBox::currentIndexChanged,
            this,
            [this](int)
            {
                const QString preferredQuoteAsset = quoteAssetSelector->currentText();
                updateQuoteAssets(preferredQuoteAsset);
                updatePairLabels();
            });
    connect(quoteAssetSelector, &QComboBox::currentIndexChanged, this, [this](int) { updatePairLabels(); });
    connect(operationSelector, &QComboBox::currentIndexChanged, this, [this](int) { updateOperationFields(); });
    connect(placeOrderTypeSelector,
            &QComboBox::currentIndexChanged,
            this,
            [this](int) { updatePlaceOrderTypeFields(); });
    connect(useOcoStopLimit, &QCheckBox::toggled, this, [this](bool) { updateOcoStopLimitFields(); });

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

    auto *sideSelector = new QComboBox(fields);
    sideSelector->setObjectName("placeOrderSideSelector");
    sideSelector->setProperty("orderInput", true);
    sideSelector->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    sideSelector->addItems({"Buy", "Sell"});

    placeOrderTypeSelector = new QComboBox(fields);
    placeOrderTypeSelector->setObjectName("placeOrderTypeSelector");
    placeOrderTypeSelector->setProperty("orderInput", true);
    placeOrderTypeSelector->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    placeOrderTypeSelector->addItems({"Market", "Limit"});

    mainFields->addRow(createFieldLabel("Side", *fields), sideSelector);
    mainFields->addRow(createFieldLabel("Order type", *fields), placeOrderTypeSelector);

    placeOrderLimitFields = new QWidget(fields);
    placeOrderLimitFields->setObjectName("placeOrderLimitFields");

    auto *limitLayout = new QFormLayout(placeOrderLimitFields);
    limitLayout->setContentsMargins(0, 0, 0, 0);
    limitLayout->setHorizontalSpacing(ORDER_FORM_COLUMN_SPACING);
    limitLayout->setVerticalSpacing(ORDER_FORM_ROW_SPACING);

    placeOrderPriceLabel = createFieldLabel("Price (quote per base)", *placeOrderLimitFields);
    placeOrderPriceLabel->setObjectName("placeOrderPriceLabel");
    placeOrderPriceInput = new QLineEdit(placeOrderLimitFields);
    placeOrderPriceInput->setObjectName("placeOrderPriceInput");
    placeOrderPriceInput->setProperty("orderInput", true);
    placeOrderPriceInput->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);

    auto *timeInForceField = new QLineEdit("GTC", placeOrderLimitFields);
    timeInForceField->setObjectName("placeOrderTimeInForceField");
    timeInForceField->setProperty("orderInput", true);
    timeInForceField->setProperty("readOnlyContext", true);
    timeInForceField->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    timeInForceField->setReadOnly(true);

    limitLayout->addRow(placeOrderPriceLabel, placeOrderPriceInput);
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

    auto *sideSelector = new QComboBox(fields);
    sideSelector->setObjectName("placeOcoSideSelector");
    sideSelector->setProperty("orderInput", true);
    sideSelector->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    sideSelector->addItems({"Buy", "Sell"});

    ocoLimitPriceLabel = createFieldLabel("Limit price (quote per base)", *fields);
    ocoLimitPriceLabel->setObjectName("placeOcoLimitPriceLabel");
    ocoLimitPriceInput = new QLineEdit(fields);
    ocoLimitPriceInput->setObjectName("placeOcoLimitPriceInput");
    ocoLimitPriceInput->setProperty("orderInput", true);
    ocoLimitPriceInput->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);

    ocoStopPriceLabel = createFieldLabel("Stop price (quote per base)", *fields);
    ocoStopPriceLabel->setObjectName("placeOcoStopPriceLabel");
    ocoStopPriceInput = new QLineEdit(fields);
    ocoStopPriceInput->setObjectName("placeOcoStopPriceInput");
    ocoStopPriceInput->setProperty("orderInput", true);
    ocoStopPriceInput->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);

    useOcoStopLimit = new QCheckBox("Use a stop-limit price", fields);
    useOcoStopLimit->setObjectName("placeOcoUseStopLimit");

    mainFields->addRow(createFieldLabel("Side", *fields), sideSelector);
    mainFields->addRow(ocoLimitPriceLabel, ocoLimitPriceInput);
    mainFields->addRow(ocoStopPriceLabel, ocoStopPriceInput);
    mainFields->addRow(QString(), useOcoStopLimit);

    ocoStopLimitFields = new QWidget(fields);
    ocoStopLimitFields->setObjectName("placeOcoStopLimitFields");

    auto *stopLimitLayout = new QFormLayout(ocoStopLimitFields);
    stopLimitLayout->setContentsMargins(0, 0, 0, 0);
    stopLimitLayout->setHorizontalSpacing(ORDER_FORM_COLUMN_SPACING);
    stopLimitLayout->setVerticalSpacing(ORDER_FORM_ROW_SPACING);

    ocoStopLimitPriceLabel = createFieldLabel("Stop-limit price (quote per base)", *ocoStopLimitFields);
    ocoStopLimitPriceLabel->setObjectName("placeOcoStopLimitPriceLabel");
    ocoStopLimitPriceInput = new QLineEdit(ocoStopLimitFields);
    ocoStopLimitPriceInput->setObjectName("placeOcoStopLimitPriceInput");
    ocoStopLimitPriceInput->setProperty("orderInput", true);
    ocoStopLimitPriceInput->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);

    auto *timeInForceField = new QLineEdit("GTC", ocoStopLimitFields);
    timeInForceField->setObjectName("placeOcoStopLimitTimeInForceField");
    timeInForceField->setProperty("orderInput", true);
    timeInForceField->setProperty("readOnlyContext", true);
    timeInForceField->setMinimumHeight(ORDER_FORM_CONTROL_MINIMUM_HEIGHT);
    timeInForceField->setReadOnly(true);

    stopLimitLayout->addRow(ocoStopLimitPriceLabel, ocoStopLimitPriceInput);
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
    }
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
}

void OrderEntryForm::updatePlaceOrderTypeFields()
{
    placeOrderLimitFields->setVisible(placeOrderTypeSelector->currentText() == "Limit");
}

void OrderEntryForm::updateOcoStopLimitFields()
{
    ocoStopLimitFields->setVisible(useOcoStopLimit->isChecked());
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
    amountInput->setPlaceholderText(baseAsset.isEmpty() ? "Base-asset quantity" : "Amount in " + baseAsset);
    placeOrderPriceLabel->setText("Price (" + priceUnit + ")");
    ocoLimitPriceLabel->setText("Limit price (" + priceUnit + ")");
    ocoStopPriceLabel->setText("Stop price (" + priceUnit + ")");
    ocoStopLimitPriceLabel->setText("Stop-limit price (" + priceUnit + ")");
}

bool OrderEntryForm::hasUsableSelectedCatalog() const
{
    const ExchangerType exchangerType = getSelectedExchangerType();
    return pairCatalog.getLoadState(exchangerType).getStatus() == UiTaskState::Status::SUCCEEDED &&
           !pairCatalog.getPairs(exchangerType).empty();
}

QString OrderEntryForm::getSelectedExchangeName() const
{
    return exchangeSelector->currentText();
}
