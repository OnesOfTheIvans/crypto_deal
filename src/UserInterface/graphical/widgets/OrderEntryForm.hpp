#ifndef ORDER_ENTRY_FORM_H
#define ORDER_ENTRY_FORM_H

#include "ExchangerType.hpp"
#include "OperationType.hpp"
#include "common/domain/TradablePair.hpp"
#include "graphical/models/BasicOrderDraft.hpp"
#include "graphical/models/OcoOrderDraft.hpp"
#include "graphical/validation/OrderInputValidation.hpp"

#include <QString>
#include <QWidget>

#include <optional>

class DecimalInputField;
class BalanceCatalog;
class PairCatalog;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class SymbolInfoCatalog;

class OrderEntryForm final : public QWidget
{
    Q_OBJECT

  private:
    PairCatalog &pairCatalog;
    BalanceCatalog &balanceCatalog;
    SymbolInfoCatalog &symbolInfoCatalog;
    QComboBox *exchangeSelector;
    QLineEdit *categoryField;
    QLabel *selectedCatalogStatus;
    QLabel *selectedBalanceStatus;
    QPushButton *retryBalanceButton;
    QWidget *pairDependentControls;
    QComboBox *baseAssetSelector;
    QComboBox *quoteAssetSelector;
    QComboBox *operationSelector;
    QLabel *selectedSymbolInfoStatus;
    QPushButton *retrySymbolInfoButton;
    QLabel *tradingLimits;
    QLabel *amountLabel;
    DecimalInputField *amountField;
    QStackedWidget *operationFormStack;
    QComboBox *placeOrderSideSelector;
    QComboBox *placeOrderTypeSelector;
    QWidget *placeOrderLimitFields;
    QLabel *placeOrderPriceLabel;
    DecimalInputField *placeOrderPriceField;
    QComboBox *ocoSideSelector;
    QCheckBox *useOcoStopLimit;
    QWidget *ocoStopLimitFields;
    QLabel *ocoLimitPriceLabel;
    QLabel *ocoStopPriceLabel;
    QLabel *ocoStopLimitPriceLabel;
    DecimalInputField *ocoLimitPriceField;
    DecimalInputField *ocoStopPriceField;
    DecimalInputField *ocoStopLimitPriceField;
    QLabel *formValidationError;
    QLabel *placementAvailabilityMessage;
    QPushButton *proceedButton;
    bool placementActive;

    void createLayout();

    QWidget *createMarketOperationFields(const QString &description, const QString &objectName);

    QWidget *createPlaceOrderFields();

    QWidget *createOcoFields();

    QLabel *createFieldLabel(const QString &text, QWidget &parent) const;

    void connectCatalogUpdates();

    void connectInputUpdates();

    void updateSelectedCatalog();

    void updateSelectedBalance();

    void updatePairSelectors();

    void updateQuoteAssets(const QString &preferredQuoteAsset = {});

    void updateOperationFields();

    void updatePlaceOrderTypeFields();

    void updateOcoStopLimitFields();

    void updatePairLabels();

    void updateSelectedSymbolInfo();

    void updateTradingLimits(const SymbolInfo &symbolInfo);

    void updateInputAvailability(bool enabled);

    void validateForm();

    void updatePlacementAvailability();

    bool showFieldValidation(DecimalInputField &field, const DecimalInputValidation &validation, Decimal increment);

    QString validateActiveNotional(const SymbolInfo &symbolInfo,
                                   const DecimalInputValidation &amountValidation,
                                   const DecimalInputValidation &placeOrderPriceValidation,
                                   const DecimalInputValidation &ocoLimitPriceValidation,
                                   const DecimalInputValidation &ocoStopPriceValidation,
                                   const DecimalInputValidation &ocoStopLimitPriceValidation) const;

    bool shouldShowNotionalValidation(const QString &error) const;

    void clearPriceValidation();

    bool hasUsableSelectedCatalog() const;

    bool hasReadySelectedBalances() const;

    bool hasActivePlaceOrderPrice() const;

    bool hasActiveOcoStopLimitPrice() const;

    QString getSelectedExchangeName() const;

    const SymbolInfo *getSelectedSymbolInfo() const;

  public:
    OrderEntryForm(PairCatalog &pairCatalog,
                   BalanceCatalog &balanceCatalog,
                   SymbolInfoCatalog &symbolInfoCatalog,
                   QWidget *parent = nullptr);

    ExchangerType getSelectedExchangerType() const;

    std::optional<TradablePair> getSelectedPair() const;

    OperationType getSelectedOperation() const;

    std::optional<BasicOrderDraft> createBasicOrderDraft() const;

    std::optional<OcoOrderDraft> createOcoOrderDraft() const;

    void setPlacementActive(bool active);

  signals:
    void requestConfirmation();
};

#endif
