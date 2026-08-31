#include "graphical/models/BalanceCatalog.hpp"
#include "TestDealService.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"
#include "graphical/async/UiTaskState.hpp"

#include <QApplication>
#include <QtTest/QTest>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>

using namespace std;
using namespace std::chrono_literals;

namespace {
    QApplication &getApplication()
    {
        auto *existingApplication = qobject_cast<QApplication *>(QApplication::instance());
        if (existingApplication != nullptr)
        {
            return *existingApplication;
        }

        static int argumentCount = 1;
        static char applicationName[] = "BalanceCatalogTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }

    BalanceCatalog::BalanceSnapshot createBalances()
    {
        AssetBalance usdtBalance;
        usdtBalance.asset = "USDT";
        usdtBalance.free = DecimalConverter::parseDecimal("100");
        return {{"USDT", usdtBalance}};
    }
}

TEST(BalanceCatalogTest, KeepsSuccessfulAndFailedExchangeResultsIndependent)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE,
                                                       TestDealService::TradablePairLoader{},
                                                       TestDealService::SymbolInfoLoader{},
                                                       []() { return createBalances(); });
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT,
                                                     TestDealService::TradablePairLoader{},
                                                     TestDealService::SymbolInfoLoader{},
                                                     []() -> BalanceCatalog::BalanceSnapshot
                                                     { throw runtime_error("Bybit balances unavailable"); });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);

    balanceCatalog.loadBalances();

    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::FAILED,
                              1000);
    ASSERT_TRUE(balanceCatalog.getBalances(ExchangerType::BINANCE).contains("USDT"));
    EXPECT_EQ(balanceCatalog.getBalances(ExchangerType::BINANCE).at("USDT").free,
              DecimalConverter::parseDecimal("100"));
    EXPECT_TRUE(balanceCatalog.getBalances(ExchangerType::BYBIT).empty());
    EXPECT_EQ(balanceCatalog.getLoadState(ExchangerType::BYBIT).getError(), QString("Bybit balances unavailable"));
    EXPECT_TRUE(balanceCatalog.hasSuccessfulSnapshot(ExchangerType::BINANCE));
    EXPECT_FALSE(balanceCatalog.hasSuccessfulSnapshot(ExchangerType::BYBIT));
    EXPECT_EQ(binanceService->getBalanceRequestCount(), 1u);
    EXPECT_EQ(bybitService->getBalanceRequestCount(), 1u);
}

TEST(BalanceCatalogTest, StartsBothLoadsWithoutBlockingAndTreatsEmptySnapshotsAsReady)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    promise<void> releasePromise;
    const shared_future<void> release = releasePromise.get_future().share();
    atomic<int> startedRequestCount = 0;
    auto createService = [&startedRequestCount, release](ExchangerType exchangerType)
    {
        return make_shared<TestDealService>(exchangerType,
                                            TestDealService::TradablePairLoader{},
                                            TestDealService::SymbolInfoLoader{},
                                            [&startedRequestCount, release]()
                                            {
                                                ++startedRequestCount;
                                                release.wait();
                                                return BalanceCatalog::BalanceSnapshot{};
                                            });
    };
    auto binanceService = createService(ExchangerType::BINANCE);
    auto bybitService = createService(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);

    balanceCatalog.loadBalances();

    QTRY_COMPARE_WITH_TIMEOUT(startedRequestCount.load(), 2, 1000);
    EXPECT_EQ(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(), UiTaskState::Status::LOADING);
    EXPECT_EQ(balanceCatalog.getLoadState(ExchangerType::BYBIT).getStatus(), UiTaskState::Status::LOADING);
    EXPECT_EQ(taskExecutor.getActiveTaskCount(), 2u);

    releasePromise.set_value();

    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_TRUE(balanceCatalog.getBalances(ExchangerType::BINANCE).empty());
    EXPECT_TRUE(balanceCatalog.getBalances(ExchangerType::BYBIT).empty());
    EXPECT_TRUE(balanceCatalog.hasSuccessfulSnapshot(ExchangerType::BINANCE));
    EXPECT_TRUE(balanceCatalog.hasSuccessfulSnapshot(ExchangerType::BYBIT));
}

TEST(BalanceCatalogTest, RetriesOnlyAFailedExchange)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    atomic<int> bybitAttempts = 0;
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT,
                                                     TestDealService::TradablePairLoader{},
                                                     TestDealService::SymbolInfoLoader{},
                                                     [&bybitAttempts]()
                                                     {
                                                         if (++bybitAttempts == 1)
                                                         {
                                                             throw runtime_error("temporary Bybit balance failure");
                                                         }
                                                         return createBalances();
                                                     });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);

    balanceCatalog.loadBalances();
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::FAILED,
                              1000);

    balanceCatalog.retryBalances(ExchangerType::BYBIT);

    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_EQ(bybitService->getBalanceRequestCount(), 2u);
    EXPECT_EQ(binanceService->getBalanceRequestCount(), 1u);
    EXPECT_TRUE(balanceCatalog.getBalances(ExchangerType::BYBIT).contains("USDT"));
    EXPECT_TRUE(balanceCatalog.hasSuccessfulSnapshot(ExchangerType::BYBIT));
}

TEST(BalanceCatalogTest, PreservesLastSuccessfulSnapshotAcrossRefreshFailureAndReplacement)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    atomic<int> binanceAttempts = 0;
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE,
                                                       TestDealService::TradablePairLoader{},
                                                       TestDealService::SymbolInfoLoader{},
                                                       [&binanceAttempts]()
                                                       {
                                                           const int attempt = ++binanceAttempts;
                                                           if (attempt == 2)
                                                           {
                                                               throw runtime_error("Binance refresh failed");
                                                           }

                                                           BalanceCatalog::BalanceSnapshot balances = createBalances();
                                                           balances.at("USDT").free = DecimalConverter::parseDecimal(
                                                               attempt == 1 ? "100" : "250");
                                                           return balances;
                                                       });
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);

    balanceCatalog.loadBalances();
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);

    balanceCatalog.refreshBalances(ExchangerType::BINANCE);
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::FAILED,
                              1000);
    EXPECT_TRUE(balanceCatalog.hasSuccessfulSnapshot(ExchangerType::BINANCE));
    EXPECT_EQ(balanceCatalog.getBalances(ExchangerType::BINANCE).at("USDT").free,
              DecimalConverter::parseDecimal("100"));
    EXPECT_EQ(balanceCatalog.getLoadState(ExchangerType::BINANCE).getError(), QString("Binance refresh failed"));

    balanceCatalog.refreshBalances(ExchangerType::BINANCE);
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_EQ(balanceCatalog.getBalances(ExchangerType::BINANCE).at("USDT").free,
              DecimalConverter::parseDecimal("250"));
    EXPECT_EQ(binanceService->getBalanceRequestCount(), 3u);
    EXPECT_EQ(bybitService->getBalanceRequestCount(), 1u);
}

TEST(BalanceCatalogTest, KeepsSnapshotVisibleAndSuppressesOverlappingRefreshes)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    promise<void> releaseRefreshPromise;
    const shared_future<void> releaseRefresh = releaseRefreshPromise.get_future().share();
    atomic<int> binanceAttempts = 0;
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE,
                                                       TestDealService::TradablePairLoader{},
                                                       TestDealService::SymbolInfoLoader{},
                                                       [&binanceAttempts, releaseRefresh]()
                                                       {
                                                           const int attempt = ++binanceAttempts;
                                                           BalanceCatalog::BalanceSnapshot balances = createBalances();
                                                           if (attempt > 1)
                                                           {
                                                               releaseRefresh.wait();
                                                               balances.at("USDT").free =
                                                                   DecimalConverter::parseDecimal("300");
                                                           }
                                                           return balances;
                                                       });
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);

    balanceCatalog.loadBalances();
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);

    balanceCatalog.refreshBalances(ExchangerType::BINANCE);
    QTRY_COMPARE_WITH_TIMEOUT(binanceService->getBalanceRequestCount(), 2u, 1000);
    EXPECT_EQ(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(), UiTaskState::Status::LOADING);
    EXPECT_EQ(balanceCatalog.getBalances(ExchangerType::BINANCE).at("USDT").free,
              DecimalConverter::parseDecimal("100"));

    balanceCatalog.refreshBalances(ExchangerType::BINANCE);
    QTest::qWait(20);
    EXPECT_EQ(binanceService->getBalanceRequestCount(), 2u);

    releaseRefreshPromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(balanceCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_EQ(balanceCatalog.getBalances(ExchangerType::BINANCE).at("USDT").free,
              DecimalConverter::parseDecimal("300"));
}
