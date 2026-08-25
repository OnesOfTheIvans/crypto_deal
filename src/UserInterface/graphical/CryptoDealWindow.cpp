#include "CryptoDealWindow.hpp"

#include "GuiLayoutConstants.hpp"
#include "common/exception_handling.hpp"
#include "models/BalanceCatalog.hpp"
#include "models/OrderPlacementModel.hpp"
#include "models/PairCatalog.hpp"
#include "models/SymbolInfoCatalog.hpp"
#include "pages/AccountsPage.hpp"
#include "pages/OperationChainsPage.hpp"
#include "pages/OrdersPage.hpp"

#include <QButtonGroup>
#include <QFile>
#include <QHBoxLayout>
#include <QIODevice>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

using namespace exception_handling;
using namespace GuiLayoutConstants;

CryptoDealWindow::CryptoDealWindow(PairCatalog &pairCatalog,
                                   BalanceCatalog &balanceCatalog,
                                   SymbolInfoCatalog &symbolInfoCatalog,
                                   OrderPlacementModel &orderPlacementModel,
                                   QWidget *parent)
    : QMainWindow(parent), pairCatalog(pairCatalog), balanceCatalog(balanceCatalog),
      symbolInfoCatalog(symbolInfoCatalog), orderPlacementModel(orderPlacementModel), pageStack(nullptr)
{
    setObjectName("cryptoDealWindow");
    setWindowTitle("CryptoDeal");
    setMinimumSize(WINDOW_MINIMUM_WIDTH, WINDOW_MINIMUM_HEIGHT);
    resize(WINDOW_INITIAL_WIDTH, WINDOW_INITIAL_HEIGHT);

    createLayout();
    applyStyle();
}

void CryptoDealWindow::createLayout()
{
    auto *windowContent = new QWidget(this);
    windowContent->setObjectName("windowContent");

    auto *windowLayout = new QHBoxLayout(windowContent);
    windowLayout->setContentsMargins(WINDOW_CONTENT_MARGIN,
                                     WINDOW_CONTENT_MARGIN,
                                     WINDOW_CONTENT_MARGIN,
                                     WINDOW_CONTENT_MARGIN);
    windowLayout->setSpacing(WINDOW_LAYOUT_SPACING);

    auto *sidebar = new QWidget(windowContent);
    sidebar->setObjectName("primarySidebar");
    sidebar->setFixedWidth(SIDEBAR_WIDTH);

    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(SIDEBAR_HORIZONTAL_MARGIN,
                                      SIDEBAR_TOP_MARGIN,
                                      SIDEBAR_HORIZONTAL_MARGIN,
                                      SIDEBAR_BOTTOM_MARGIN);
    sidebarLayout->setSpacing(SIDEBAR_LAYOUT_SPACING);

    auto *brandLabel = new QLabel("CryptoDeal", sidebar);
    brandLabel->setObjectName("brandLabel");

    auto *brandDescription = new QLabel("SPOT TRADING CONSOLE", sidebar);
    brandDescription->setObjectName("brandDescription");

    auto *navigationLabel = new QLabel("WORKSPACE", sidebar);
    navigationLabel->setObjectName("navigationLabel");

    sidebarLayout->addWidget(brandLabel);
    sidebarLayout->addWidget(brandDescription);
    sidebarLayout->addSpacing(BRAND_NAVIGATION_SPACING);
    sidebarLayout->addWidget(navigationLabel);
    sidebarLayout->addSpacing(NAVIGATION_BUTTONS_SPACING);

    auto *ordersButton = createNavigationButton("Orders", "ordersNavigationButton");
    auto *operationChainsButton = createNavigationButton("Operation Chains", "operationChainsNavigationButton");
    auto *accountsButton = createNavigationButton("Accounts", "accountsNavigationButton");

    sidebarLayout->addWidget(ordersButton);
    sidebarLayout->addWidget(operationChainsButton);
    sidebarLayout->addWidget(accountsButton);
    sidebarLayout->addStretch();

    auto *proofOfConceptLabel = new QLabel("PROOF OF CONCEPT", sidebar);
    proofOfConceptLabel->setObjectName("proofOfConceptLabel");
    sidebarLayout->addWidget(proofOfConceptLabel);

    pageStack = new QStackedWidget(windowContent);
    pageStack->setObjectName("primaryPageStack");
    pageStack->addWidget(
        new OrdersPage(pairCatalog, balanceCatalog, symbolInfoCatalog, orderPlacementModel, pageStack));
    pageStack->addWidget(new OperationChainsPage(pageStack));
    pageStack->addWidget(new AccountsPage(pageStack));

    auto *navigationGroup = new QButtonGroup(this);
    navigationGroup->setExclusive(true);
    navigationGroup->addButton(ordersButton, static_cast<int>(Page::ORDERS));
    navigationGroup->addButton(operationChainsButton, static_cast<int>(Page::OPERATION_CHAINS));
    navigationGroup->addButton(accountsButton, static_cast<int>(Page::ACCOUNTS));

    connect(navigationGroup, &QButtonGroup::idClicked, this, [this](int page) { showPage(static_cast<Page>(page)); });

    ordersButton->setChecked(true);
    showPage(Page::ORDERS);

    windowLayout->addWidget(sidebar);
    windowLayout->addWidget(pageStack, PRIMARY_CONTENT_STRETCH);
    setCentralWidget(windowContent);
}

QPushButton *CryptoDealWindow::createNavigationButton(const QString &text, const QString &objectName)
{
    auto *button = new QPushButton(text);
    button->setObjectName(objectName);
    button->setProperty("navigation", true);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(NAVIGATION_BUTTON_MINIMUM_HEIGHT);
    return button;
}

void CryptoDealWindow::showPage(Page page)
{
    pageStack->setCurrentIndex(static_cast<int>(page));
}

void CryptoDealWindow::applyStyle()
{
    QFile styleFile(":/styles/dark.qss");
    throwIf(!styleFile.open(QIODevice::ReadOnly | QIODevice::Text), "Unable to load the graphical interface style");
    setStyleSheet(QString::fromUtf8(styleFile.readAll()));
}
