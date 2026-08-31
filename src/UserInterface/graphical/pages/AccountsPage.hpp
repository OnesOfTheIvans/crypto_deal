#ifndef ACCOUNTS_PAGE_H
#define ACCOUNTS_PAGE_H

#include "ExchangerType.hpp"

#include <QWidget>

class BalanceCatalog;
class QLabel;
class QPushButton;
class QTableWidget;
class QTabWidget;

class AccountsPage final : public QWidget
{
  private:
    struct ExchangeView
    {
        QWidget *page = nullptr;
        QLabel *status = nullptr;
        QPushButton *refreshButton = nullptr;
        QTableWidget *table = nullptr;
        QLabel *emptyState = nullptr;
    };

    BalanceCatalog &balanceCatalog;
    QTabWidget *exchangeTabs;
    ExchangeView binanceView;
    ExchangeView bybitView;

    void createLayout();

    ExchangeView createExchangeView(ExchangerType exchangerType);

    ExchangeView &getExchangeView(ExchangerType exchangerType);

    void connectCatalogUpdates();

    void updateExchange(ExchangerType exchangerType);

    void updateBalanceRows(ExchangerType exchangerType);

  public:
    explicit AccountsPage(BalanceCatalog &balanceCatalog, QWidget *parent = nullptr);
};

#endif
