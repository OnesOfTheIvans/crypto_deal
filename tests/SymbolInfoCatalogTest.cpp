#include "graphical/models/SymbolInfoCatalog.hpp"
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
#include <string>

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
        static char applicationName[] = "SymbolInfoCatalogTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }

    SymbolInfo createSymbolInfo(const string &symbol, const string &baseAsset)
    {
        SymbolInfo symbolInfo;
        symbolInfo.symbol = symbol;
        symbolInfo.baseAsset = baseAsset;
        symbolInfo.quoteAsset = "USDT";
        symbolInfo.tickSize = DecimalConverter::parseDecimal("0.01");
        symbolInfo.stepSize = DecimalConverter::parseDecimal("0.001");
        symbolInfo.minQty = DecimalConverter::parseDecimal("0.001");
        return symbolInfo;
    }
}

TEST(SymbolInfoCatalogTest, LoadsSelectedSymbolsIndependentlyAndCachesSuccessfulResults)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService =
        make_shared<TestDealService>(ExchangerType::BINANCE,
                                     TestDealService::TradablePairLoader{},
                                     [](const string &symbol) { return createSymbolInfo(symbol, "BTC"); });
    auto bybitService =
        make_shared<TestDealService>(ExchangerType::BYBIT,
                                     TestDealService::TradablePairLoader{},
                                     [](const string &symbol) { return createSymbolInfo(symbol, "SOL"); });
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);

    symbolInfoCatalog.loadSymbolInfo(ExchangerType::BINANCE, "BTCUSDT");
    symbolInfoCatalog.loadSymbolInfo(ExchangerType::BYBIT, "SOLUSDT");

    QTRY_COMPARE_WITH_TIMEOUT(symbolInfoCatalog.getLoadState(ExchangerType::BINANCE, "BTCUSDT")->getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    QTRY_COMPARE_WITH_TIMEOUT(symbolInfoCatalog.getLoadState(ExchangerType::BYBIT, "SOLUSDT")->getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    ASSERT_NE(symbolInfoCatalog.getSymbolInfo(ExchangerType::BINANCE, "BTCUSDT"), nullptr);
    ASSERT_NE(symbolInfoCatalog.getSymbolInfo(ExchangerType::BYBIT, "SOLUSDT"), nullptr);
    EXPECT_EQ(symbolInfoCatalog.getSymbolInfo(ExchangerType::BINANCE, "BTCUSDT")->baseAsset, "BTC");
    EXPECT_EQ(symbolInfoCatalog.getSymbolInfo(ExchangerType::BYBIT, "SOLUSDT")->baseAsset, "SOL");

    symbolInfoCatalog.loadSymbolInfo(ExchangerType::BINANCE, "BTCUSDT");
    QApplication::processEvents();

    EXPECT_EQ(binanceService->getSymbolInfoRequestCount(), 1u);
    EXPECT_EQ(bybitService->getSymbolInfoRequestCount(), 1u);
}

TEST(SymbolInfoCatalogTest, KeepsFailureUntilExplicitRetryAndPreservesExactError)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    atomic<int> attempts = 0;
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE,
                                                       TestDealService::TradablePairLoader{},
                                                       [&attempts](const string &symbol)
                                                       {
                                                           if (++attempts == 1)
                                                           {
                                                               throw runtime_error("symbol rules unavailable");
                                                           }
                                                           return createSymbolInfo(symbol, "BTC");
                                                       });
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);

    symbolInfoCatalog.loadSymbolInfo(ExchangerType::BINANCE, "BTCUSDT");
    QTRY_COMPARE_WITH_TIMEOUT(symbolInfoCatalog.getLoadState(ExchangerType::BINANCE, "BTCUSDT")->getStatus(),
                              UiTaskState::Status::FAILED,
                              1000);
    EXPECT_EQ(symbolInfoCatalog.getLoadState(ExchangerType::BINANCE, "BTCUSDT")->getError(),
              QString("symbol rules unavailable"));

    symbolInfoCatalog.loadSymbolInfo(ExchangerType::BINANCE, "BTCUSDT");
    QApplication::processEvents();
    EXPECT_EQ(binanceService->getSymbolInfoRequestCount(), 1u);

    symbolInfoCatalog.retrySymbolInfo(ExchangerType::BINANCE, "BTCUSDT");
    QTRY_COMPARE_WITH_TIMEOUT(symbolInfoCatalog.getLoadState(ExchangerType::BINANCE, "BTCUSDT")->getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_EQ(binanceService->getSymbolInfoRequestCount(), 2u);
}

TEST(SymbolInfoCatalogTest, KeepsOutOfOrderCompletionsAttachedToTheirRequestedSymbols)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    promise<void> releaseBtcPromise;
    promise<void> releaseEthPromise;
    const shared_future<void> releaseBtc = releaseBtcPromise.get_future().share();
    const shared_future<void> releaseEth = releaseEthPromise.get_future().share();
    auto binanceService = make_shared<TestDealService>(ExchangerType::BINANCE,
                                                       TestDealService::TradablePairLoader{},
                                                       [releaseBtc, releaseEth](const string &symbol)
                                                       {
                                                           if (symbol == "BTCUSDT")
                                                           {
                                                               releaseBtc.wait_for(2s);
                                                               return createSymbolInfo(symbol, "BTC");
                                                           }
                                                           releaseEth.wait_for(2s);
                                                           return createSymbolInfo(symbol, "ETH");
                                                       });
    auto bybitService = make_shared<TestDealService>(ExchangerType::BYBIT);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);

    symbolInfoCatalog.loadSymbolInfo(ExchangerType::BINANCE, "BTCUSDT");
    symbolInfoCatalog.loadSymbolInfo(ExchangerType::BINANCE, "ETHUSDT");
    releaseEthPromise.set_value();

    QTRY_COMPARE_WITH_TIMEOUT(symbolInfoCatalog.getLoadState(ExchangerType::BINANCE, "ETHUSDT")->getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_EQ(symbolInfoCatalog.getLoadState(ExchangerType::BINANCE, "BTCUSDT")->getStatus(),
              UiTaskState::Status::LOADING);
    EXPECT_EQ(symbolInfoCatalog.getSymbolInfo(ExchangerType::BINANCE, "ETHUSDT")->baseAsset, "ETH");

    releaseBtcPromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(symbolInfoCatalog.getLoadState(ExchangerType::BINANCE, "BTCUSDT")->getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_EQ(symbolInfoCatalog.getSymbolInfo(ExchangerType::BINANCE, "BTCUSDT")->baseAsset, "BTC");
}
