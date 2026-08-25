#ifndef CRYPTO_DEAL_WINDOW_H
#define CRYPTO_DEAL_WINDOW_H

#include "Page.hpp"

#include <QMainWindow>

class QPushButton;
class QStackedWidget;
class QString;
class QWidget;
class PairCatalog;
class SymbolInfoCatalog;

class CryptoDealWindow final : public QMainWindow
{
  private:
    PairCatalog &pairCatalog;
    SymbolInfoCatalog &symbolInfoCatalog;
    QStackedWidget *pageStack;

    void createLayout();

    QPushButton *createNavigationButton(const QString &text, const QString &objectName);

    void showPage(Page page);

    void applyStyle();

  public:
    CryptoDealWindow(PairCatalog &pairCatalog, SymbolInfoCatalog &symbolInfoCatalog, QWidget *parent = nullptr);
};

#endif
