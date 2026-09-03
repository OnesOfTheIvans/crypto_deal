#include "graphical/pages/AccountsPage.hpp"
#include "TestDealService.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"
#include "graphical/async/UiTaskState.hpp"
#include "graphical/models/BalanceCatalog.hpp"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTabWidget>
#include <QTableWidget>
#include <QtTest/QTest>
#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>

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
        static char applicationName[] = "AccountsPageTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }

    AssetBalance createBalance(const string &asset, const string &free, const string &locked)
    {
        return {asset, DecimalConverter::parseDecimal(free), DecimalConverter::parseDecimal(locked)};
    }

    BalanceCatalog::BalanceSnapshot createBinanceBalances()
    {
        return {{"BTC", createBalance("BTC", "1.25", "0.5")}, {"ZERO", createBalance("ZERO", "0", "0")}};
    }

    BalanceCatalog::BalanceSnapshot createBybitBalances()
    {
        return {{"USDT", createBalance("USDT", "20", "5")}};
    }
}

TEST(AccountsPageTest, ShowsExchangeTabsExactTotalsAndOmitsZeroBalances)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE,
                                                       TestDealService::TradablePairLoader{},
                                                       TestDealService::SymbolInfoLoader{},
                                                       []() { return createBinanceBalances(); });
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT,
                                                     TestDealService::TradablePairLoader{},
                                                     TestDealService::SymbolInfoLoader{},
                                                     []() { return createBybitBalances(); });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    AccountsPage page(balanceCatalog);
    page.show();

    auto *tabs = page.findChild<QTabWidget *>("accountsExchangeTabs");
    auto *binanceTable = page.findChild<QTableWidget *>("binanceBalancesTable");
    auto *bybitTable = page.findChild<QTableWidget *>("bybitBalancesTable");
    ASSERT_NE(tabs, nullptr);
    ASSERT_NE(binanceTable, nullptr);
    ASSERT_NE(bybitTable, nullptr);
    EXPECT_EQ(page.findChild<QWidget *>("showZeroBalancesCheckBox"), nullptr);
    ASSERT_EQ(tabs->count(), 2);
    EXPECT_EQ(tabs->tabText(0), QString("Binance"));
    EXPECT_EQ(tabs->tabText(1), QString("Bybit"));
    EXPECT_EQ(tabs->currentIndex(), 0);

    balanceCatalog.loadBalances();

    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    ASSERT_EQ(binanceTable->rowCount(), 1);
    EXPECT_EQ(binanceTable->item(0, 0)->text(), QString("BTC"));
    EXPECT_EQ(binanceTable->item(0, 1)->text(), QString("1.25"));
    EXPECT_EQ(binanceTable->item(0, 2)->text(), QString("0.5"));
    EXPECT_EQ(binanceTable->item(0, 3)->text(), QString("1.75"));
    ASSERT_EQ(bybitTable->rowCount(), 1);
    EXPECT_EQ(bybitTable->item(0, 0)->text(), QString("USDT"));
    EXPECT_EQ(bybitTable->item(0, 3)->text(), QString("25"));
    EXPECT_EQ(binanceService->getBalanceRequestCount(), 1u);
    EXPECT_EQ(bybitService->getBalanceRequestCount(), 1u);

    tabs->setCurrentIndex(1);
    EXPECT_EQ(tabs->currentIndex(), 1);
    EXPECT_TRUE(bybitTable->isVisible());
}

TEST(AccountsPageTest, PresentsIndependentLoadingFailureAndEmptyStates)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    promise<void> releaseBinancePromise;
    const shared_future<void> releaseBinance = releaseBinancePromise.get_future().share();
    atomic<int> binanceStarted = 0;
    atomic<int> bybitAttempts = 0;
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE,
                                                       TestDealService::TradablePairLoader{},
                                                       TestDealService::SymbolInfoLoader{},
                                                       [&binanceStarted, releaseBinance]()
                                                       {
                                                           ++binanceStarted;
                                                           releaseBinance.wait();
                                                           return BalanceCatalog::BalanceSnapshot{
                                                               {"ZERO", createBalance("ZERO", "0", "0")}};
                                                       });
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT,
                                                     TestDealService::TradablePairLoader{},
                                                     TestDealService::SymbolInfoLoader{},
                                                     [&bybitAttempts]()
                                                     {
                                                         if (++bybitAttempts == 1)
                                                         {
                                                             throw runtime_error("Bybit startup failure");
                                                         }
                                                         return BalanceCatalog::BalanceSnapshot{};
                                                     });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    AccountsPage page(balanceCatalog);
    page.show();

    auto *binanceStatus = page.findChild<QLabel *>("binanceBalancesStatus");
    auto *binanceRefresh = page.findChild<QPushButton *>("binanceBalancesRefreshButton");
    auto *binanceEmptyState = page.findChild<QLabel *>("binanceBalancesEmptyState");
    auto *bybitStatus = page.findChild<QLabel *>("bybitBalancesStatus");
    auto *bybitRefresh = page.findChild<QPushButton *>("bybitBalancesRefreshButton");
    auto *bybitEmptyState = page.findChild<QLabel *>("bybitBalancesEmptyState");
    ASSERT_NE(binanceStatus, nullptr);
    ASSERT_NE(binanceRefresh, nullptr);
    ASSERT_NE(binanceEmptyState, nullptr);
    ASSERT_NE(bybitStatus, nullptr);
    ASSERT_NE(bybitRefresh, nullptr);
    ASSERT_NE(bybitEmptyState, nullptr);

    balanceCatalog.loadBalances();

    QTRY_COMPARE_WITH_TIMEOUT(binanceStarted.load(), 1, 1000);
    EXPECT_EQ(binanceStatus->text(), QString("Loading Binance balances..."));
    EXPECT_EQ(binanceStatus->property("statusPresentation").toString(), QString("loading"));
    EXPECT_FALSE(binanceRefresh->isEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::FAILED,
                              1000);
    EXPECT_EQ(bybitStatus->text(), QString("Bybit balances are unavailable: Bybit startup failure"));
    EXPECT_EQ(bybitStatus->property("statusPresentation").toString(), QString("error"));
    EXPECT_EQ(bybitEmptyState->text(), QString("A successful Bybit balance snapshot is not available yet."));
    EXPECT_TRUE(bybitRefresh->isEnabled());

    QTest::mouseClick(bybitRefresh, Qt::LeftButton);

    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_EQ(bybitStatus->text(), QString("Bybit returned an empty balance snapshot."));
    EXPECT_EQ(bybitStatus->property("statusPresentation").toString(), QString("success"));
    EXPECT_EQ(bybitEmptyState->text(), QString("No balances were returned for Bybit."));
    EXPECT_EQ(bybitService->getBalanceRequestCount(), 2u);

    releaseBinancePromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_EQ(binanceStatus->text(), QString("Binance balances are ready."));
    EXPECT_EQ(binanceStatus->property("statusPresentation").toString(), QString("success"));
    EXPECT_EQ(binanceEmptyState->text(), QString("No non-zero balances were returned for Binance."));
}

TEST(AccountsPageTest, KeepsVisibleSnapshotThroughFailedAndOverlappingRefreshes)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    promise<void> releaseRefreshPromise;
    const shared_future<void> releaseRefresh = releaseRefreshPromise.get_future().share();
    atomic<int> binanceAttempts = 0;
    auto binanceService = make_shared<TestDealService>(
        ExchangerType::BINANCE,
        TestDealService::TradablePairLoader{},
        TestDealService::SymbolInfoLoader{},
        [&binanceAttempts, releaseRefresh]()
        {
            const int attempt = ++binanceAttempts;
            if (attempt == 2)
            {
                throw runtime_error("Binance manual refresh failure");
            }

            BalanceCatalog::BalanceSnapshot balances{
                {"USDT", createBalance("USDT", attempt == 1 ? "100" : "250", attempt == 1 ? "10" : "25")}};
            if (attempt > 2)
            {
                releaseRefresh.wait();
            }
            return balances;
        });
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT,
                                                     TestDealService::TradablePairLoader{},
                                                     TestDealService::SymbolInfoLoader{},
                                                     []() { return createBybitBalances(); });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    AccountsPage page(balanceCatalog);
    page.show();

    auto *binanceStatus = page.findChild<QLabel *>("binanceBalancesStatus");
    auto *binanceRefresh = page.findChild<QPushButton *>("binanceBalancesRefreshButton");
    auto *bybitRefresh = page.findChild<QPushButton *>("bybitBalancesRefreshButton");
    auto *binanceTable = page.findChild<QTableWidget *>("binanceBalancesTable");
    ASSERT_NE(binanceStatus, nullptr);
    ASSERT_NE(binanceRefresh, nullptr);
    ASSERT_NE(bybitRefresh, nullptr);
    ASSERT_NE(binanceTable, nullptr);

    balanceCatalog.loadBalances();
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    ASSERT_EQ(binanceTable->rowCount(), 1);
    EXPECT_EQ(binanceTable->item(0, 3)->text(), QString("110"));

    QTest::mouseClick(binanceRefresh, Qt::LeftButton);

    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::FAILED,
                              1000);
    EXPECT_EQ(binanceStatus->text(),
              QString("Binance balance refresh failed. Showing the latest cached snapshot: "
                      "Binance manual refresh failure"));
    EXPECT_EQ(binanceTable->item(0, 3)->text(), QString("110"));
    EXPECT_TRUE(balanceCatalog.hasSuccessfulSnapshot(ExchangerType::BINANCE));

    QTest::mouseClick(binanceRefresh, Qt::LeftButton);

    QTRY_COMPARE_WITH_TIMEOUT(binanceService->getBalanceRequestCount(), 3u, 1000);
    EXPECT_EQ(binanceStatus->text(),
              QString("Refreshing Binance balances. The latest cached snapshot remains visible."));
    EXPECT_FALSE(binanceRefresh->isEnabled());
    EXPECT_TRUE(bybitRefresh->isEnabled());
    EXPECT_EQ(binanceTable->item(0, 3)->text(), QString("110"));

    balanceCatalog.refreshBalances(ExchangerType::BINANCE);
    QTest::qWait(20);
    EXPECT_EQ(binanceService->getBalanceRequestCount(), 3u);

    releaseRefreshPromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_EQ(binanceStatus->text(), QString("Binance balances are ready."));
    EXPECT_TRUE(binanceRefresh->isEnabled());
    EXPECT_EQ(binanceTable->item(0, 1)->text(), QString("250"));
    EXPECT_EQ(binanceTable->item(0, 2)->text(), QString("25"));
    EXPECT_EQ(binanceTable->item(0, 3)->text(), QString("275"));
}

TEST(AccountsPageTest, AppliesLiveRowsAndPresentsIndependentStreamFailure)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE,
                                                       TestDealService::TradablePairLoader{},
                                                       TestDealService::SymbolInfoLoader{},
                                                       []() { return createBinanceBalances(); });
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT,
                                                     TestDealService::TradablePairLoader{},
                                                     TestDealService::SymbolInfoLoader{},
                                                     []() { return createBybitBalances(); });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    AccountsPage page(balanceCatalog);
    page.show();

    auto *binanceLiveStatus = page.findChild<QLabel *>("binanceBalancesLiveStatus");
    auto *bybitLiveStatus = page.findChild<QLabel *>("bybitBalancesLiveStatus");
    auto *binanceTable = page.findChild<QTableWidget *>("binanceBalancesTable");
    ASSERT_NE(binanceLiveStatus, nullptr);
    ASSERT_NE(bybitLiveStatus, nullptr);
    ASSERT_NE(binanceTable, nullptr);

    balanceCatalog.loadBalances();
    balanceCatalog.startLiveUpdates();
    QTRY_COMPARE_WITH_TIMEOUT(binanceLiveStatus->text(), QString("Binance live balance updates are connected."), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(bybitLiveStatus->text(), QString("Bybit live balance updates are connected."), 1000);
    EXPECT_EQ(binanceLiveStatus->property("statusPresentation").toString(), QString("success"));
    EXPECT_EQ(bybitLiveStatus->property("statusPresentation").toString(), QString("success"));

    binanceService->publishBalanceUpdate(createBalance("BTC", "0", "0"));
    binanceService->publishBalanceUpdate(createBalance("ETH", "3.5", "0.25"));
    QTRY_COMPARE_WITH_TIMEOUT(binanceTable->rowCount(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(binanceTable->item(0, 0)->text(), QString("ETH"), 1000);
    EXPECT_EQ(binanceTable->item(0, 3)->text(), QString("3.75"));

    bybitService->publishUserStreamStatus(StreamStatus::ERROR, "Bybit socket read failed");
    QTRY_COMPARE_WITH_TIMEOUT(
        bybitLiveStatus->text(),
        QString("Bybit live balance updates are unavailable: Bybit socket read failed. Reconnecting automatically..."),
        1000);
    EXPECT_EQ(bybitLiveStatus->property("statusPresentation").toString(), QString("warning"));
    EXPECT_EQ(binanceLiveStatus->text(), QString("Binance live balance updates are connected."));
}
