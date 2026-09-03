#ifndef ORDERS_PAGE_H
#define ORDERS_PAGE_H

#include "ExchangerType.hpp"

#include <QWidget>

#include <cstdint>
#include <optional>

class QLabel;
class BalanceCatalog;
class OrderEntryForm;
class OrderSessionModel;
class PairCatalog;
class QPushButton;
class QString;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
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
    QTabWidget *sessionOrdersTabs;
    QLabel *sessionOrderActionContext;
    QPushButton *refreshSessionOrderButton;
    QPushButton *cancelSessionOrderButton;
    QPushButton *cancelAllSessionOrdersButton;
    std::optional<std::uint64_t> selectedSessionEntryId;
    bool updatingSessionOrderTables;

    void updateCatalogStatus(ExchangerType exchangerType, QLabel &statusLabel, const QString &exchangeName);

    void requestOrderConfirmation();

    void updateSessionOrderTables();

    void updateSessionOrderTable(QTreeWidget &table, QLabel &emptyState, bool showOnlyActiveOrders);

    void updateSessionOrderSelection();

    void updateSessionOrderActions();

    void restoreSessionOrderSelection(QTreeWidget &table);

    QTreeWidget *getCurrentSessionOrderTable() const;

    QTreeWidgetItem *findSessionOrderItem(QTreeWidget &table, std::uint64_t entryId) const;

    void refreshSelectedSessionOrder();

    void cancelSelectedSessionOrder();

    void cancelAllSelectedSessionOrders();

    bool confirmSelectedOrderCancellation();

    bool confirmSelectedPairCancellation();

  public:
    OrdersPage(PairCatalog &pairCatalog,
               BalanceCatalog &balanceCatalog,
               SymbolInfoCatalog &symbolInfoCatalog,
               OrderSessionModel &orderSessionModel,
               QWidget *parent = nullptr);
};

#endif
