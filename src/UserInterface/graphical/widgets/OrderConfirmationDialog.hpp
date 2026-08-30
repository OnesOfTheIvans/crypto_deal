#ifndef ORDER_CONFIRMATION_DIALOG_H
#define ORDER_CONFIRMATION_DIALOG_H

#include "graphical/models/BasicOrderDraft.hpp"
#include "graphical/models/OcoOrderDraft.hpp"

#include <QDialog>

#include <variant>

class QString;
class QWidget;

class OrderConfirmationDialog final : public QDialog
{
  private:
    const std::variant<BasicOrderDraft, OcoOrderDraft> draft;

    void createLayout();

    QString buildSummary() const;

    QString buildBasicOrderSummary(const BasicOrderDraft &basicOrderDraft) const;

    QString buildOcoOrderSummary(const OcoOrderDraft &ocoOrderDraft) const;

  public:
    explicit OrderConfirmationDialog(BasicOrderDraft draft, QWidget *parent = nullptr);

    explicit OrderConfirmationDialog(OcoOrderDraft draft, QWidget *parent = nullptr);
};

#endif
