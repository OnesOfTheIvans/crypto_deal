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
#include <QLabel>
#include <QPushButton>
#include <QSize>
#include <QStackedWidget>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QtTest/QTest>
#include <gtest/gtest.h>

#include <future>
#include <memory>
#include <vector>

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

    operationChainsButton->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(operationChainsButton->hasFocus());
    QTest::keyClick(operationChainsButton, Qt::Key_Space);
    EXPECT_EQ(pageStack->currentWidget()->objectName(), QString("operationChainsPage"));
    EXPECT_TRUE(operationChainsButton->isChecked());
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

TEST(GraphicalUserInterfaceTest, KeepsFullBootstrapResponsiveAndStopsInjectedServicesCleanly)
{
    QApplication &application = getApplication();
    promise<void> releaseBinancePromise;
    promise<void> releaseBybitPromise;
    const shared_future<void> releaseBinance = releaseBinancePromise.get_future().share();
    const shared_future<void> releaseBybit = releaseBybitPromise.get_future().share();
    auto binanceService = make_shared<TestDealService>(
        ExchangerType::BINANCE,
        [releaseBinance]()
        {
            releaseBinance.wait();
            return vector<TradablePair>{{"BTCUSDT", "BTC", "USDT"}};
        },
        TestDealService::SymbolInfoLoader{},
        [releaseBinance]()
        {
            releaseBinance.wait();
            return BalanceCatalog::BalanceSnapshot{
                {"USDT", {"USDT", DecimalConverter::parseDecimal("100"), DecimalConverter::parseDecimal("0")}}};
        },
        [releaseBinance](TestDealService &service)
        {
            releaseBinance.wait();
            service.publishUserStreamStatus(StreamStatus::CONNECTED);
        });
    auto bybitService = make_shared<TestDealService>(
        ExchangerType::BYBIT,
        [releaseBybit]()
        {
            releaseBybit.wait();
            return vector<TradablePair>{{"ETHUSDT", "ETH", "USDT"}};
        },
        TestDealService::SymbolInfoLoader{},
        [releaseBybit]()
        {
            releaseBybit.wait();
            return BalanceCatalog::BalanceSnapshot{
                {"ETH", {"ETH", DecimalConverter::parseDecimal("2"), DecimalConverter::parseDecimal("0.1")}}};
        },
        [releaseBybit](TestDealService &service)
        {
            releaseBybit.wait();
            service.publishUserStreamStatus(StreamStatus::CONNECTED);
        });
    GraphicalUserInterface graphicalUserInterface(binanceService, bybitService, {});
    bool binanceReleased = false;
    bool bybitReleased = false;
    bool completed = false;
    int stage = 0;

    auto releaseBinanceWork = [&]()
    {
        if (!binanceReleased)
        {
            binanceReleased = true;
            releaseBinancePromise.set_value();
        }
    };
    auto releaseBybitWork = [&]()
    {
        if (!bybitReleased)
        {
            bybitReleased = true;
            releaseBybitPromise.set_value();
        }
    };
    auto findWindow = []() -> QWidget *
    {
        for (QWidget *candidate : QApplication::topLevelWidgets())
        {
            if (candidate->objectName() == "cryptoDealWindow")
            {
                return candidate;
            }
        }
        return nullptr;
    };
    auto hasPresentation = [](QWidget &window, const char *objectName, const QString &presentation)
    {
        const auto *label = window.findChild<QLabel *>(objectName);
        return label != nullptr && label->property("statusPresentation").toString() == presentation;
    };

    QTimer progressTimer;
    progressTimer.setInterval(5);
    QObject::connect(
        &progressTimer,
        &QTimer::timeout,
        [&]()
        {
            QWidget *window = findWindow();
            if (window == nullptr)
            {
                return;
            }

            if (stage == 0)
            {
                auto *pageStack = window->findChild<QStackedWidget *>("primaryPageStack");
                auto *ordersButton = window->findChild<QPushButton *>("ordersNavigationButton");
                auto *chainsButton = window->findChild<QPushButton *>("operationChainsNavigationButton");
                auto *accountsButton = window->findChild<QPushButton *>("accountsNavigationButton");
                auto *runsTable = window->findChild<QTableWidget *>("operationChainRunsTable");
                if (pageStack == nullptr || ordersButton == nullptr || chainsButton == nullptr ||
                    accountsButton == nullptr || runsTable == nullptr || runsTable->rowCount() != 3 ||
                    binanceService->getTradablePairRequestCount() != 1 ||
                    bybitService->getTradablePairRequestCount() != 1 || binanceService->getBalanceRequestCount() != 1 ||
                    bybitService->getBalanceRequestCount() != 1 || binanceService->getUserStreamStartCount() != 1 ||
                    bybitService->getUserStreamStartCount() != 1)
                {
                    return;
                }

                EXPECT_TRUE(hasPresentation(*window, "binancePairCatalogStatus", "loading"));
                EXPECT_TRUE(hasPresentation(*window, "bybitPairCatalogStatus", "loading"));
                EXPECT_TRUE(hasPresentation(*window, "binanceBalancesStatus", "loading"));
                EXPECT_TRUE(hasPresentation(*window, "bybitBalancesStatus", "loading"));
                EXPECT_TRUE(hasPresentation(*window, "binanceBalancesLiveStatus", "loading"));
                EXPECT_TRUE(hasPresentation(*window, "bybitBalancesLiveStatus", "loading"));

                chainsButton->click();
                EXPECT_EQ(pageStack->currentWidget()->objectName(), QString("operationChainsPage"));
                accountsButton->click();
                EXPECT_EQ(pageStack->currentWidget()->objectName(), QString("accountsPage"));
                ordersButton->click();
                EXPECT_EQ(pageStack->currentWidget()->objectName(), QString("ordersPage"));

                releaseBinanceWork();
                stage = 1;
                return;
            }

            if (stage == 1)
            {
                if (!hasPresentation(*window, "binancePairCatalogStatus", "success") ||
                    !hasPresentation(*window, "binanceBalancesStatus", "success") ||
                    !hasPresentation(*window, "binanceBalancesLiveStatus", "success"))
                {
                    return;
                }

                EXPECT_TRUE(hasPresentation(*window, "bybitPairCatalogStatus", "loading"));
                EXPECT_TRUE(hasPresentation(*window, "bybitBalancesStatus", "loading"));
                EXPECT_TRUE(hasPresentation(*window, "bybitBalancesLiveStatus", "loading"));
                releaseBybitWork();
                stage = 2;
                return;
            }

            if (!hasPresentation(*window, "bybitPairCatalogStatus", "success") ||
                !hasPresentation(*window, "bybitBalancesStatus", "success") ||
                !hasPresentation(*window, "bybitBalancesLiveStatus", "success"))
            {
                return;
            }

            completed = true;
            progressTimer.stop();
            window->close();
            application.quit();
        });

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer,
                     &QTimer::timeout,
                     [&]()
                     {
                         ADD_FAILURE() << "Full GUI bootstrap did not complete within the test timeout";
                         releaseBinanceWork();
                         releaseBybitWork();
                         if (QWidget *window = findWindow(); window != nullptr)
                         {
                             window->close();
                         }
                         application.quit();
                     });
    progressTimer.start();
    timeoutTimer.start(5000);

    EXPECT_EQ(graphicalUserInterface.run(), 0);
    progressTimer.stop();
    timeoutTimer.stop();
    EXPECT_TRUE(completed);
    EXPECT_EQ(binanceService->getTradablePairRequestCount(), 1u);
    EXPECT_EQ(bybitService->getTradablePairRequestCount(), 1u);
    EXPECT_EQ(binanceService->getBalanceRequestCount(), 1u);
    EXPECT_EQ(bybitService->getBalanceRequestCount(), 1u);
    EXPECT_EQ(binanceService->getUserStreamStartCount(), 1u);
    EXPECT_EQ(bybitService->getUserStreamStartCount(), 1u);
    EXPECT_EQ(binanceService->getUserStreamStopCount(), 1u);
    EXPECT_EQ(bybitService->getUserStreamStopCount(), 1u);
}
