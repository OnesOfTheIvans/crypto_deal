#ifndef ORDERS_PAGE_H
#define ORDERS_PAGE_H

#include "ExchangerType.hpp"

#include <QWidget>

class QLabel;
class PairCatalog;
class QString;

class OrdersPage final : public QWidget
{
  private:
    PairCatalog &pairCatalog;
    QLabel *binanceCatalogStatus;
    QLabel *bybitCatalogStatus;

    void updateCatalogStatus(ExchangerType exchangerType, QLabel &statusLabel, const QString &exchangeName);

  public:
    explicit OrdersPage(PairCatalog &pairCatalog, QWidget *parent = nullptr);
};

#endif
