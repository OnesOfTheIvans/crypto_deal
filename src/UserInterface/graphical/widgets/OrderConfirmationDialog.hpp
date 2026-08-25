#ifndef ORDER_CONFIRMATION_DIALOG_H
#define ORDER_CONFIRMATION_DIALOG_H

#include "graphical/models/BasicOrderDraft.hpp"

#include <QDialog>

class QString;
class QWidget;

class OrderConfirmationDialog final : public QDialog
{
  private:
    const BasicOrderDraft draft;

    QString buildSummary() const;

  public:
    explicit OrderConfirmationDialog(BasicOrderDraft draft, QWidget *parent = nullptr);
};

#endif
