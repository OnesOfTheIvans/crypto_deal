#ifndef ORDERS_PAGE_H
#define ORDERS_PAGE_H

#include "ExchangerType.hpp"

#include <QWidget>

class QLabel;
class BalanceCatalog;
class OrderEntryForm;
class OrderSessionModel;
class PairCatalog;
class QString;
class QTreeWidget;
class SymbolInfoCatalog;
class QWidget;

class OrdersPage final : public QWidget
{
  private:
    PairCatalog &pairCatalog;
    BalanceCatalog &balanceCatalog;
    SymbolInfoCatalog &symbolInfoCatalog;
    OrderSessionModel &orderSessionModel;
    QLabel *binanceCatalogStatus;
    QLabel *bybitCatalogStatus;
    OrderEntryForm *orderEntryForm;
    QLabel *activeOrdersEmptyState;
    QLabel *allSessionOrdersEmptyState;
    QTreeWidget *activeOrdersTable;
    QTreeWidget *allSessionOrdersTable;

    void updateCatalogStatus(ExchangerType exchangerType, QLabel &statusLabel, const QString &exchangeName);

    void requestOrderConfirmation();

    void updateSessionOrderTables();

    void updateSessionOrderTable(QTreeWidget &table, QLabel &emptyState, bool showOnlyActiveOrders);

  public:
    OrdersPage(PairCatalog &pairCatalog,
               BalanceCatalog &balanceCatalog,
               SymbolInfoCatalog &symbolInfoCatalog,
               OrderSessionModel &orderSessionModel,
               QWidget *parent = nullptr);
};

#endif
