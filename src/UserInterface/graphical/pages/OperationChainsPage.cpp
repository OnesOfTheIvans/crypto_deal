#include "OperationChainsPage.hpp"

#include "graphical/GuiLayoutConstants.hpp"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

using namespace GuiLayoutConstants;

OperationChainsPage::OperationChainsPage(QWidget *parent) : QWidget(parent)
{
    setObjectName("operationChainsPage");
    setProperty("primaryPage", true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(PAGE_HORIZONTAL_MARGIN,
                               PAGE_VERTICAL_MARGIN,
                               PAGE_HORIZONTAL_MARGIN,
                               PAGE_VERTICAL_MARGIN);
    layout->setSpacing(PAGE_LAYOUT_SPACING);

    auto *title = new QLabel("Operation Chains", this);
    title->setProperty("pageTitle", true);

    auto *description = new QLabel("Start and monitor reusable trading workflows.", this);
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

    auto *placeholderTitle = new QLabel("Chain workspace", placeholderCard);
    placeholderTitle->setProperty("placeholderTitle", true);

    auto *placeholderDescription =
        new QLabel("Available definitions and session run history will appear here.", placeholderCard);
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
