#ifndef CRYPTO_DEAL_WINDOW_H
#define CRYPTO_DEAL_WINDOW_H

#include "Page.hpp"

#include <QMainWindow>

class QPushButton;
class QStackedWidget;
class QString;
class QWidget;
class BalanceCatalog;
class PairCatalog;
class OrderSessionModel;
class SymbolInfoCatalog;

class CryptoDealWindow final : public QMainWindow
{
  private:
    PairCatalog &pairCatalog;
    BalanceCatalog &balanceCatalog;
    SymbolInfoCatalog &symbolInfoCatalog;
    OrderSessionModel &orderSessionModel;
    QStackedWidget *pageStack;

    void createLayout();

    QPushButton *createNavigationButton(const QString &text, const QString &objectName);

    void showPage(Page page);

    void applyStyle();

  public:
    CryptoDealWindow(PairCatalog &pairCatalog,
                     BalanceCatalog &balanceCatalog,
                     SymbolInfoCatalog &symbolInfoCatalog,
                     OrderSessionModel &orderSessionModel,
                     QWidget *parent = nullptr);
};

#endif
