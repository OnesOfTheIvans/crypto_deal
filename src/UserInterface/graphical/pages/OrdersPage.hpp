#ifndef ORDERS_PAGE_H
#define ORDERS_PAGE_H

#include "ExchangerType.hpp"

#include <QWidget>

class QLabel;
class BalanceCatalog;
class OrderEntryForm;
class OrderPlacementModel;
class PairCatalog;
class QString;
class SymbolInfoCatalog;
class QWidget;

class OrdersPage final : public QWidget
{
  private:
    PairCatalog &pairCatalog;
    BalanceCatalog &balanceCatalog;
    SymbolInfoCatalog &symbolInfoCatalog;
    OrderPlacementModel &orderPlacementModel;
    QLabel *binanceCatalogStatus;
    QLabel *bybitCatalogStatus;
    OrderEntryForm *orderEntryForm;
    QWidget *placementStatusPanel;
    QLabel *placementStatusTitle;
    QLabel *placementStatusDetails;
    QLabel *placementStatusError;

    void updateCatalogStatus(ExchangerType exchangerType, QLabel &statusLabel, const QString &exchangeName);

    void requestOrderConfirmation();

    void updatePlacementStatus();

  public:
    OrdersPage(PairCatalog &pairCatalog,
               BalanceCatalog &balanceCatalog,
               SymbolInfoCatalog &symbolInfoCatalog,
               OrderPlacementModel &orderPlacementModel,
               QWidget *parent = nullptr);
};

#endif
