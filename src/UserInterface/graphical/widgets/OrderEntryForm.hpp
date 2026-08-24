#ifndef ORDER_ENTRY_FORM_H
#define ORDER_ENTRY_FORM_H

#include "ExchangerType.hpp"
#include "OperationType.hpp"
#include "common/domain/TradablePair.hpp"

#include <QString>
#include <QWidget>

#include <optional>

class PairCatalog;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QStackedWidget;

class OrderEntryForm final : public QWidget
{
  private:
    PairCatalog &pairCatalog;
    QComboBox *exchangeSelector;
    QLineEdit *categoryField;
    QLabel *selectedCatalogStatus;
    QWidget *pairDependentControls;
    QComboBox *baseAssetSelector;
    QComboBox *quoteAssetSelector;
    QComboBox *operationSelector;
    QLabel *amountLabel;
    QLineEdit *amountInput;
    QStackedWidget *operationFormStack;
    QComboBox *placeOrderTypeSelector;
    QWidget *placeOrderLimitFields;
    QLabel *placeOrderPriceLabel;
    QLineEdit *placeOrderPriceInput;
    QCheckBox *useOcoStopLimit;
    QWidget *ocoStopLimitFields;
    QLabel *ocoLimitPriceLabel;
    QLabel *ocoStopPriceLabel;
    QLabel *ocoStopLimitPriceLabel;
    QLineEdit *ocoLimitPriceInput;
    QLineEdit *ocoStopPriceInput;
    QLineEdit *ocoStopLimitPriceInput;

    void createLayout();

    QWidget *createMarketOperationFields(const QString &description, const QString &objectName);

    QWidget *createPlaceOrderFields();

    QWidget *createOcoFields();

    QLabel *createFieldLabel(const QString &text, QWidget &parent) const;

    void connectCatalogUpdates();

    void updateSelectedCatalog();

    void updatePairSelectors();

    void updateQuoteAssets(const QString &preferredQuoteAsset = {});

    void updateOperationFields();

    void updatePlaceOrderTypeFields();

    void updateOcoStopLimitFields();

    void updatePairLabels();

    bool hasUsableSelectedCatalog() const;

    QString getSelectedExchangeName() const;

  public:
    explicit OrderEntryForm(PairCatalog &pairCatalog, QWidget *parent = nullptr);

    ExchangerType getSelectedExchangerType() const;

    std::optional<TradablePair> getSelectedPair() const;

    OperationType getSelectedOperation() const;
};

#endif
