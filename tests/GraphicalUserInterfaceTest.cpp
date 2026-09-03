#include "graphical/GraphicalUserInterface.hpp"
#include "OperationChainRunManager.hpp"
#include "TestDealService.hpp"
#include "graphical/CryptoDealWindow.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"
#include "graphical/models/BalanceCatalog.hpp"
#include "graphical/models/OperationChainRunModel.hpp"
#include "graphical/models/OrderSessionModel.hpp"
#include "graphical/models/PairCatalog.hpp"
#include "graphical/models/SymbolInfoCatalog.hpp"

#include <QApplication>
#include <QPushButton>
#include <QSize>
#include <QStackedWidget>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QtTest/QTest>
#include <gtest/gtest.h>

#include <memory>

using namespace std;

namespace {
    QApplication &getApplication()
    {
        auto *existingApplication = qobject_cast<QApplication *>(QApplication::instance());
        if (existingApplication != nullptr)
        {
            return *existingApplication;
        }

        static int argumentCount = 1;
        static char applicationName[] = "GraphicalUserInterfaceTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }
}

TEST(GraphicalUserInterfaceTest, ShowsOrdersPageByDefault)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderSessionModel orderSessionModel(taskExecutor, binanceService, bybitService);
    OperationChainRunManager chainRunManager({}, binanceService, bybitService);
    OperationChainRunModel chainRunModel(chainRunManager);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderSessionModel, chainRunModel);
    window.show();
    QApplication::processEvents();

    auto *pageStack = window.findChild<QStackedWidget *>("primaryPageStack");
    auto *ordersButton = window.findChild<QPushButton *>("ordersNavigationButton");

    ASSERT_NE(pageStack, nullptr);
    ASSERT_NE(ordersButton, nullptr);
    ASSERT_NE(pageStack->currentWidget(), nullptr);
    EXPECT_EQ(pageStack->count(), 3);
    EXPECT_EQ(pageStack->currentWidget()->objectName(), QString("ordersPage"));
    EXPECT_TRUE(ordersButton->isChecked());
}

TEST(GraphicalUserInterfaceTest, SeparatesNewOrderAndSessionOrderWorkspaces)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderSessionModel orderSessionModel(taskExecutor, binanceService, bybitService);
    OperationChainRunManager chainRunManager({}, binanceService, bybitService);
    OperationChainRunModel chainRunModel(chainRunManager);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderSessionModel, chainRunModel);

    auto *workspaceTabs = window.findChild<QTabWidget *>("ordersWorkspaceTabs");
    auto *sessionTabs = window.findChild<QTabWidget *>("sessionOrdersTabs");
    auto *orderEntryForm = window.findChild<QWidget *>("orderEntryForm");

    ASSERT_NE(workspaceTabs, nullptr);
    ASSERT_NE(sessionTabs, nullptr);
    ASSERT_NE(orderEntryForm, nullptr);
    ASSERT_EQ(workspaceTabs->count(), 2);
    EXPECT_EQ(workspaceTabs->tabText(0), QString("New order"));
    EXPECT_EQ(workspaceTabs->tabText(1), QString("Session orders"));
    EXPECT_EQ(workspaceTabs->currentIndex(), 0);
    EXPECT_TRUE(workspaceTabs->widget(0)->isAncestorOf(orderEntryForm));
    EXPECT_EQ(sessionTabs->parentWidget(), workspaceTabs->widget(1));
    EXPECT_EQ(sessionTabs->tabText(0), QString("Active orders"));
    EXPECT_EQ(sessionTabs->tabText(1), QString("All session orders"));
}

TEST(GraphicalUserInterfaceTest, SwitchesPagesAndKeepsNavigationSelectionSynchronized)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderSessionModel orderSessionModel(taskExecutor, binanceService, bybitService);
    OperationChainRunManager chainRunManager({}, binanceService, bybitService);
    OperationChainRunModel chainRunModel(chainRunManager);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderSessionModel, chainRunModel);
    window.show();
    QApplication::processEvents();

    auto *pageStack = window.findChild<QStackedWidget *>("primaryPageStack");
    auto *ordersButton = window.findChild<QPushButton *>("ordersNavigationButton");
    auto *operationChainsButton = window.findChild<QPushButton *>("operationChainsNavigationButton");
    auto *accountsButton = window.findChild<QPushButton *>("accountsNavigationButton");

    ASSERT_NE(pageStack, nullptr);
    ASSERT_NE(ordersButton, nullptr);
    ASSERT_NE(operationChainsButton, nullptr);
    ASSERT_NE(accountsButton, nullptr);

    QTest::mouseClick(operationChainsButton, Qt::LeftButton);
    EXPECT_EQ(pageStack->currentWidget()->objectName(), QString("operationChainsPage"));
    EXPECT_TRUE(operationChainsButton->isChecked());
    EXPECT_FALSE(ordersButton->isChecked());
    EXPECT_FALSE(accountsButton->isChecked());

    QTest::mouseClick(accountsButton, Qt::LeftButton);
    EXPECT_EQ(pageStack->currentWidget()->objectName(), QString("accountsPage"));
    EXPECT_TRUE(accountsButton->isChecked());
    EXPECT_FALSE(ordersButton->isChecked());
    EXPECT_FALSE(operationChainsButton->isChecked());

    QTest::mouseClick(ordersButton, Qt::LeftButton);
    EXPECT_EQ(pageStack->currentWidget()->objectName(), QString("ordersPage"));
    EXPECT_TRUE(ordersButton->isChecked());
    EXPECT_FALSE(operationChainsButton->isChecked());
    EXPECT_FALSE(accountsButton->isChecked());
}

TEST(GraphicalUserInterfaceTest, UsesApprovedWindowDimensions)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderSessionModel orderSessionModel(taskExecutor, binanceService, bybitService);
    OperationChainRunManager chainRunManager({}, binanceService, bybitService);
    OperationChainRunModel chainRunModel(chainRunManager);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderSessionModel, chainRunModel);

    EXPECT_EQ(window.size(), QSize(1180, 760));
    EXPECT_EQ(window.minimumSize(), QSize(960, 640));
}

TEST(GraphicalUserInterfaceTest, AutomaticallyAddsThreeSimulatedRunsAfterStartup)
{
    QApplication &application = getApplication();
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    GraphicalUserInterface graphicalUserInterface(binanceService, bybitService, {});
    bool inspected = false;

    QTimer::singleShot(100,
                       [&application, &inspected]()
                       {
                           QWidget *window = nullptr;
                           for (QWidget *candidate : QApplication::topLevelWidgets())
                           {
                               if (candidate->objectName() == "cryptoDealWindow")
                               {
                                   window = candidate;
                                   break;
                               }
                           }

                           EXPECT_NE(window, nullptr);
                           if (window != nullptr)
                           {
                               auto *runsTable = window->findChild<QTableWidget *>("operationChainRunsTable");
                               EXPECT_NE(runsTable, nullptr);
                               if (runsTable != nullptr)
                               {
                                   EXPECT_EQ(runsTable->rowCount(), 3);
                                   for (int row = 0; row < runsTable->rowCount(); ++row)
                                   {
                                       QTableWidgetItem *item = runsTable->item(row, 0);
                                       EXPECT_NE(item, nullptr);
                                       if (item != nullptr)
                                       {
                                           EXPECT_TRUE(item->text().contains("Simulated · Run #"));
                                       }
                                   }
                                   inspected = true;
                               }
                           }
                           application.quit();
                       });

    EXPECT_EQ(graphicalUserInterface.run(), 0);
    EXPECT_TRUE(inspected);
}
