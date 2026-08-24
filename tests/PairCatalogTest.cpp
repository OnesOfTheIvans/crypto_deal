#include "graphical/models/PairCatalog.hpp"
#include "DealService.hpp"
#include "graphical/CryptoDealWindow.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"

#include <QApplication>
#include <QLabel>
#include <QString>
#include <QtTest/QTest>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

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
        static char applicationName[] = "PairCatalogTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }

    class CatalogDealService final : public DealService
    {
      private:
        function<vector<TradablePair>()> loadPairs;
        atomic<size_t> requestCount;

      public:
        CatalogDealService(ExchangerType exchangerType, function<vector<TradablePair>()> loadPairs)
            : DealService("host", "api", "secret", "ws", 5000, exchangerType), loadPairs(move(loadPairs)),
              requestCount(0)
        {}

        size_t getRequestCount() const
        {
            return requestCount.load();
        }

        OrderInfo buyCrypto(const string &, const string &, Decimal) override
        {
            return {};
        }

        OrderInfo sellCrypto(const string &, const string &, Decimal) override
        {
            return {};
        }

        OrderInfo waitUntilOrderFilled(const string &, const string &) override
        {
            return {};
        }

        OrderInfo waitUntilOcoOrderFilled(const OcoInfo &) override
        {
            return {};
        }

        flat_map<string, AssetBalance> getBalances() const override
        {
            return {};
        }

        optional<AssetBalance> getBalance(const string &) const override
        {
            return nullopt;
        }

        void startUserStream() override {}

        void stopUserStream() override {}

        StreamStatus getUserStreamStatus() const override
        {
            return StreamStatus::STOPPED;
        }

        string getUserStreamLastError() const override
        {
            return {};
        }

        OrderInfo placeOrder(const PlaceOrderRequest &) override
        {
            return {};
        }

        OrderInfo cancelOrder(const OrderQuery &) override
        {
            return {};
        }

        OrderInfo getOrder(const OrderQuery &) override
        {
            return {};
        }

        SymbolInfo getSymbolInfo(const string &, OrderCategory = OrderCategory::SPOT) override
        {
            return {};
        }

        vector<TradablePair> getTradablePairs() override
        {
            ++requestCount;
            return loadPairs();
        }

        Decimal ceilQuantityToStep(const string &, Decimal quantity, OrderCategory = OrderCategory::SPOT) override
        {
            return quantity;
        }

        OcoInfo placeOco(const PlaceOcoRequest &) override
        {
            return {};
        }

        void cancelOco(const OrderListQuery &) override {}

        void cancelAllOpenOrders(const string &, OrderCategory) override {}

        flat_map<string, AssetBalance> getBalancesRest() override
        {
            return {};
        }
    };
}

TEST(PairCatalogTest, KeepsSuccessfulAndFailedExchangeResultsIndependentAndLoadsOnlyOnce)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    const vector<TradablePair> binancePairs = {
        {"BTCUSDT", "BTC", "USDT"},
        {"ETHUSDT", "ETH", "USDT"},
    };
    auto binanceService =
        make_shared<CatalogDealService>(ExchangerType::BINANCE, [binancePairs]() { return binancePairs; });
    auto bybitService = make_shared<CatalogDealService>(ExchangerType::BYBIT,
                                                        []() -> vector<TradablePair>
                                                        { throw runtime_error("Bybit catalog unavailable"); });

    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);

    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::FAILED,
                              1000);
    EXPECT_EQ(pairCatalog.getPairs(ExchangerType::BINANCE), binancePairs);
    EXPECT_TRUE(pairCatalog.getPairs(ExchangerType::BYBIT).empty());
    EXPECT_EQ(pairCatalog.getLoadState(ExchangerType::BYBIT).getError(), QString("Bybit catalog unavailable"));

    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);
    QApplication::processEvents();

    EXPECT_EQ(binanceService->getRequestCount(), 1u);
    EXPECT_EQ(bybitService->getRequestCount(), 1u);
    EXPECT_EQ(pairCatalog.getPairs(ExchangerType::BINANCE), binancePairs);
    EXPECT_EQ(pairCatalog.getLoadState(ExchangerType::BYBIT).getStatus(), UiTaskState::Status::FAILED);
}

TEST(PairCatalogTest, StartsBothExchangeLoadsWithoutWaitingForEitherOne)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    promise<void> releasePromise;
    const shared_future<void> release = releasePromise.get_future().share();
    atomic<int> startedRequestCount = 0;
    auto binanceService = make_shared<CatalogDealService>(ExchangerType::BINANCE,
                                                          [&startedRequestCount, release]()
                                                          {
                                                              ++startedRequestCount;
                                                              release.wait();
                                                              return vector<TradablePair>{};
                                                          });
    auto bybitService = make_shared<CatalogDealService>(ExchangerType::BYBIT,
                                                        [&startedRequestCount, release]()
                                                        {
                                                            ++startedRequestCount;
                                                            release.wait();
                                                            return vector<TradablePair>{{"ETHUSDT", "ETH", "USDT"}};
                                                        });

    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);

    QTRY_COMPARE_WITH_TIMEOUT(startedRequestCount.load(), 2, 1000);
    EXPECT_EQ(pairCatalog.getLoadState(ExchangerType::BINANCE).getStatus(), UiTaskState::Status::LOADING);
    EXPECT_EQ(pairCatalog.getLoadState(ExchangerType::BYBIT).getStatus(), UiTaskState::Status::LOADING);
    EXPECT_EQ(taskExecutor.getActiveTaskCount(), 2u);

    releasePromise.set_value();

    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BINANCE).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    QTRY_COMPARE_WITH_TIMEOUT(pairCatalog.getLoadState(ExchangerType::BYBIT).getStatus(),
                              UiTaskState::Status::SUCCEEDED,
                              1000);
    EXPECT_TRUE(pairCatalog.getPairs(ExchangerType::BINANCE).empty());
    EXPECT_EQ(pairCatalog.getPairs(ExchangerType::BYBIT), (vector<TradablePair>{{"ETHUSDT", "ETH", "USDT"}}));
}

TEST(PairCatalogTest, DeliversIndependentCatalogStatusToOrdersPage)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    CryptoDealWindow window(pairCatalog);
    promise<void> releasePromise;
    const shared_future<void> release = releasePromise.get_future().share();
    atomic<bool> binanceRequestStarted = false;
    auto binanceService = make_shared<CatalogDealService>(ExchangerType::BINANCE,
                                                          [&binanceRequestStarted, release]()
                                                          {
                                                              binanceRequestStarted = true;
                                                              release.wait_for(2s);
                                                              return vector<TradablePair>{
                                                                  {"BTCUSDT", "BTC", "USDT"},
                                                                  {"ETHUSDT", "ETH", "USDT"},
                                                              };
                                                          });
    auto bybitService =
        make_shared<CatalogDealService>(ExchangerType::BYBIT,
                                        []() -> vector<TradablePair> { throw runtime_error("Bybit status failure"); });
    auto *binanceStatus = window.findChild<QLabel *>("binancePairCatalogStatus");
    auto *bybitStatus = window.findChild<QLabel *>("bybitPairCatalogStatus");

    ASSERT_NE(binanceStatus, nullptr);
    ASSERT_NE(bybitStatus, nullptr);
    EXPECT_EQ(binanceStatus->text(), QString("Binance: Waiting for startup load"));
    EXPECT_EQ(bybitStatus->text(), QString("Bybit: Waiting for startup load"));

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);

    EXPECT_EQ(binanceStatus->text(), QString("Binance: Loading tradable pairs..."));
    EXPECT_EQ(bybitStatus->text(), QString("Bybit: Loading tradable pairs..."));
    QTRY_VERIFY_WITH_TIMEOUT(binanceRequestStarted.load(), 1000);
    QTRY_COMPARE_WITH_TIMEOUT(bybitStatus->text(), QString("Bybit: Unable to load pairs: Bybit status failure"), 1000);

    releasePromise.set_value();

    QTRY_COMPARE_WITH_TIMEOUT(binanceStatus->text(), QString("Binance: 2 tradable pairs available"), 1000);
}
