#include "AccountsPage.hpp"

#include "graphical/GuiLayoutConstants.hpp"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

using namespace GuiLayoutConstants;

AccountsPage::AccountsPage(QWidget *parent) : QWidget(parent)
{
    setObjectName("accountsPage");
    setProperty("primaryPage", true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(PAGE_HORIZONTAL_MARGIN,
                               PAGE_VERTICAL_MARGIN,
                               PAGE_HORIZONTAL_MARGIN,
                               PAGE_VERTICAL_MARGIN);
    layout->setSpacing(PAGE_LAYOUT_SPACING);

    auto *title = new QLabel("Accounts", this);
    title->setProperty("pageTitle", true);

    auto *description = new QLabel("Review Binance and Bybit balances side by side.", this);
    description->setProperty("pageDescription", true);

    auto *placeholderCard = new QWidget(this);
    placeholderCard->setProperty("placeholderCard", true);
    placeholderCard->setMinimumHeight(PLACEHOLDER_MINIMUM_HEIGHT);

    auto *placeholderLayout = new QVBoxLayout(placeholderCard);
    placeholderLayout->setContentsMargins(PLACEHOLDER_HORIZONTAL_MARGIN,
                                          PLACEHOLDER_VERTICAL_MARGIN,
                                          PLACEHOLDER_HORIZONTAL_MARGIN,
                                          PLACEHOLDER_VERTICAL_MARGIN);
    placeholderLayout->setSpacing(PLACEHOLDER_LAYOUT_SPACING);

    auto *placeholderTitle = new QLabel("Account workspace", placeholderCard);
    placeholderTitle->setProperty("placeholderTitle", true);

    auto *placeholderDescription =
        new QLabel("Balance loading and live updates will be added in the Accounts steps.", placeholderCard);
    placeholderDescription->setProperty("placeholderDescription", true);
    placeholderDescription->setWordWrap(true);

    placeholderLayout->addWidget(placeholderTitle);
    placeholderLayout->addWidget(placeholderDescription);
    placeholderLayout->addStretch();

    layout->addWidget(title);
    layout->addWidget(description);
    layout->addSpacing(PAGE_PLACEHOLDER_SPACING);
    layout->addWidget(placeholderCard);
    layout->addStretch();
}
