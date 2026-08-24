#ifndef CRYPTO_DEAL_WINDOW_H
#define CRYPTO_DEAL_WINDOW_H

#include "Page.hpp"

#include <QMainWindow>

class QPushButton;
class QStackedWidget;
class QString;
class QWidget;
class PairCatalog;

class CryptoDealWindow final : public QMainWindow
{
  private:
    PairCatalog &pairCatalog;
    QStackedWidget *pageStack;

    void createLayout();

    QPushButton *createNavigationButton(const QString &text, const QString &objectName);

    void showPage(Page page);

    void applyStyle();

  public:
    explicit CryptoDealWindow(PairCatalog &pairCatalog, QWidget *parent = nullptr);
};

#endif
