#ifndef ORDERS_PAGE_H
#define ORDERS_PAGE_H

#include "ExchangerType.hpp"

#include <QWidget>

class QLabel;
class PairCatalog;
class QString;
class SymbolInfoCatalog;

class OrdersPage final : public QWidget
{
  private:
    PairCatalog &pairCatalog;
    SymbolInfoCatalog &symbolInfoCatalog;
    QLabel *binanceCatalogStatus;
    QLabel *bybitCatalogStatus;

    void updateCatalogStatus(ExchangerType exchangerType, QLabel &statusLabel, const QString &exchangeName);

  public:
    OrdersPage(PairCatalog &pairCatalog, SymbolInfoCatalog &symbolInfoCatalog, QWidget *parent = nullptr);
};

#endif
