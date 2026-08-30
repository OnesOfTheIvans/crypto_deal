#include "graphical/models/OrderPlacementModel.hpp"
#include "TestDealService.hpp"
#include "graphical/CryptoDealWindow.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"
#include "graphical/models/BalanceCatalog.hpp"
#include "graphical/models/PairCatalog.hpp"
#include "graphical/models/SymbolInfoCatalog.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QString>
#include <QTimer>
#include <QtTest/QTest>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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
        static char applicationName[] = "OrderPlacementModelTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }

    OrderInfo createOrderInfo(const string &status, const string &orderId = "ORDER-1")
    {
        OrderInfo orderInfo;
        orderInfo.symbol = "BTCUSDT";
        orderInfo.orderId = orderId;
        orderInfo.side = "BUY";
        orderInfo.type = "MARKET";
        orderInfo.status = status;
        orderInfo.origQty = DecimalConverter::parseDecimal("0.01");
        return orderInfo;
    }

    BasicOrderDraft createDraft(ExchangerType exchangerType, OperationType operation)
    {
        BasicOrderDraft draft;
        draft.exchangerType = exchangerType;
        draft.operation = operation;
        draft.pair = {"BTCUSDT", "BTC", "USDT"};
        draft.side = operation == OperationType::SELL_CRYPTO ? OrderOperation::SELL : OrderOperation::BUY;
        draft.type = OrderType::MARKET;
        draft.quantity = DecimalConverter::parseDecimal("0.01");
        draft.quantityText = "0.010";
        return draft;
    }

    OcoOrderDraft createOcoDraft(ExchangerType exchangerType)
    {
        OcoOrderDraft draft;
        draft.exchangerType = exchangerType;
        draft.pair = {"BTCUSDT", "BTC", "USDT"};
        draft.side = OrderOperation::SELL;
        draft.quantity = DecimalConverter::parseDecimal("0.01");
        draft.limitPrice = DecimalConverter::parseDecimal("60000");
        draft.stopPrice = DecimalConverter::parseDecimal("50000");
        draft.stopLimitPrice = DecimalConverter::parseDecimal("49900");
        draft.stopLimitTimeInForce = "GTC";
        draft.quantityText = "0.010";
        draft.limitPriceText = "60000";
        draft.stopPriceText = "50000";
        draft.stopLimitPriceText = "49900";
        return draft;
    }

    OcoInfo
    createOcoInfo(const string &takeProfitStatus, const string &stopLossStatus, const string &groupId = "OCO-GROUP-1")
    {
        OcoInfo ocoInfo;
        ocoInfo.orderListId = groupId;
        ocoInfo.listClientOrderId = groupId;
        ocoInfo.takeProfitOrder = createOrderInfo(takeProfitStatus, "OCO-TP-1");
        ocoInfo.stopLossOrder = createOrderInfo(stopLossStatus, "OCO-SL-1");
        return ocoInfo;
    }

    class PlacementDealService final : public TestDealService
    {
      public:
        using CryptoHandler = function<OrderInfo(const string &, const string &, Decimal)>;
        using OrderHandler = function<OrderInfo(const PlaceOrderRequest &)>;
        using WaitHandler = function<OrderInfo(const string &, const string &)>;
        using OcoHandler = function<OcoInfo(const PlaceOcoRequest &)>;
        using OcoWaitHandler = function<OrderInfo(const OcoInfo &)>;

        struct CryptoCall
        {
            string baseAsset;
            string quoteAsset;
            Decimal quantity{};
        };

      private:
        CryptoHandler buyHandler;
        CryptoHandler sellHandler;
        OrderHandler orderHandler;
        WaitHandler waitHandler;
        OcoHandler ocoHandler;
        OcoWaitHandler ocoWaitHandler;
        atomic<size_t> buyCount;
        atomic<size_t> sellCount;
        atomic<size_t> orderCount;
        atomic<size_t> waitCount;
        atomic<size_t> ocoCount;
        atomic<size_t> ocoWaitCount;
        atomic<size_t> stopCount;
        mutable mutex callMutex;
        optional<CryptoCall> lastBuyCall;
        optional<CryptoCall> lastSellCall;
        optional<PlaceOrderRequest> lastOrderRequest;
        optional<PlaceOcoRequest> lastOcoRequest;

        OrderInfo createDefaultAcceptedOrder() const
        {
            return createOrderInfo(getExchangerType() == ExchangerType::BINANCE ? "FILLED" : "Filled");
        }

      public:
        explicit PlacementDealService(ExchangerType exchangerType, bool providePair = false)
            : TestDealService(
                  exchangerType,
                  providePair ? TradablePairLoader([]() { return vector<TradablePair>{{"BTCUSDT", "BTC", "USDT"}}; })
                              : TradablePairLoader()),
              buyCount(0), sellCount(0), orderCount(0), waitCount(0), ocoCount(0), ocoWaitCount(0), stopCount(0)
        {}

        void setBuyHandler(CryptoHandler handler)
        {
            buyHandler = move(handler);
        }

        void setSellHandler(CryptoHandler handler)
        {
            sellHandler = move(handler);
        }

        void setOrderHandler(OrderHandler handler)
        {
            orderHandler = move(handler);
        }

        void setWaitHandler(WaitHandler handler)
        {
            waitHandler = move(handler);
        }

        void setOcoHandler(OcoHandler handler)
        {
            ocoHandler = move(handler);
        }

        void setOcoWaitHandler(OcoWaitHandler handler)
        {
            ocoWaitHandler = move(handler);
        }

        size_t getBuyCount() const
        {
            return buyCount.load();
        }

        size_t getSellCount() const
        {
            return sellCount.load();
        }

        size_t getOrderCount() const
        {
            return orderCount.load();
        }

        size_t getWaitCount() const
        {
            return waitCount.load();
        }

        size_t getStopCount() const
        {
            return stopCount.load();
        }

        size_t getOcoCount() const
        {
            return ocoCount.load();
        }

        size_t getOcoWaitCount() const
        {
            return ocoWaitCount.load();
        }

        optional<CryptoCall> getLastBuyCall() const
        {
            lock_guard<mutex> lock(callMutex);
            return lastBuyCall;
        }

        optional<CryptoCall> getLastSellCall() const
        {
            lock_guard<mutex> lock(callMutex);
            return lastSellCall;
        }

        optional<PlaceOrderRequest> getLastOrderRequest() const
        {
            lock_guard<mutex> lock(callMutex);
            return lastOrderRequest;
        }

        optional<PlaceOcoRequest> getLastOcoRequest() const
        {
            lock_guard<mutex> lock(callMutex);
            return lastOcoRequest;
        }

        OrderInfo buyCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity) override
        {
            ++buyCount;
            {
                lock_guard<mutex> lock(callMutex);
                lastBuyCall = CryptoCall{baseAsset, quoteAsset, quantity};
            }
            return buyHandler ? buyHandler(baseAsset, quoteAsset, quantity) : createDefaultAcceptedOrder();
        }

        OrderInfo sellCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity) override
        {
            ++sellCount;
            {
                lock_guard<mutex> lock(callMutex);
                lastSellCall = CryptoCall{baseAsset, quoteAsset, quantity};
            }
            return sellHandler ? sellHandler(baseAsset, quoteAsset, quantity) : createDefaultAcceptedOrder();
        }

        OrderInfo placeOrder(const PlaceOrderRequest &request) override
        {
            ++orderCount;
            {
                lock_guard<mutex> lock(callMutex);
                lastOrderRequest = request;
            }
            return orderHandler ? orderHandler(request) : createDefaultAcceptedOrder();
        }

        OrderInfo waitUntilOrderFilled(const string &symbol, const string &orderId) override
        {
            ++waitCount;
            return waitHandler ? waitHandler(symbol, orderId) : createDefaultAcceptedOrder();
        }

        OcoInfo placeOco(const PlaceOcoRequest &request) override
        {
            ++ocoCount;
            {
                lock_guard<mutex> lock(callMutex);
                lastOcoRequest = request;
            }
            return ocoHandler ? ocoHandler(request) : createOcoInfo("NEW", "NEW");
        }

        OrderInfo waitUntilOcoOrderFilled(const OcoInfo &ocoInfo) override
        {
            ++ocoWaitCount;
            return ocoWaitHandler ? ocoWaitHandler(ocoInfo) : createOrderInfo("FILLED", "OCO-TP-1");
        }

        void stopUserStream() override
        {
            ++stopCount;
        }
    };
}

TEST(OrderPlacementModelTest, MapsBuySellAndCustomOrdersToTheSelectedSharedService)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService = make_shared<PlacementDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<PlacementDealService>(ExchangerType::BYBIT);
    OrderPlacementModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::FILLED, 1000);
    ASSERT_TRUE(binanceService->getLastBuyCall().has_value());
    EXPECT_EQ(binanceService->getLastBuyCall()->baseAsset, "BTC");
    EXPECT_EQ(binanceService->getLastBuyCall()->quoteAsset, "USDT");
    EXPECT_EQ(binanceService->getLastBuyCall()->quantity, DecimalConverter::parseDecimal("0.01"));

    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::SELL_CRYPTO)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::FILLED, 1000);
    EXPECT_EQ(binanceService->getSellCount(), 1u);
    ASSERT_TRUE(binanceService->getLastSellCall().has_value());
    EXPECT_EQ(binanceService->getLastSellCall()->baseAsset, "BTC");

    BasicOrderDraft customDraft = createDraft(ExchangerType::BYBIT, OperationType::PLACE_ORDER);
    customDraft.side = OrderOperation::BUY;
    customDraft.type = OrderType::MARKET;
    EXPECT_TRUE(model.placeOrder(customDraft));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::FILLED, 1000);
    const optional<PlaceOrderRequest> requestSnapshot = bybitService->getLastOrderRequest();
    ASSERT_TRUE(requestSnapshot.has_value());
    const PlaceOrderRequest &request = requestSnapshot.value();
    EXPECT_EQ(request.symbol, "BTCUSDT");
    EXPECT_EQ(request.side, OrderOperation::BUY);
    EXPECT_EQ(request.type, OrderType::MARKET);
    EXPECT_EQ(request.category, OrderCategory::SPOT);
    ASSERT_TRUE(request.marketUnit.has_value());
    EXPECT_EQ(request.marketUnit.value(), "baseCoin");

    BasicOrderDraft limitDraft = createDraft(ExchangerType::BINANCE, OperationType::PLACE_ORDER);
    limitDraft.side = OrderOperation::SELL;
    limitDraft.type = OrderType::LIMIT;
    limitDraft.price = DecimalConverter::parseDecimal("5000");
    limitDraft.priceText = "5000";
    limitDraft.timeInForce = "GTC";
    EXPECT_TRUE(model.placeOrder(limitDraft));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::FILLED, 1000);
    const optional<PlaceOrderRequest> limitRequestSnapshot = binanceService->getLastOrderRequest();
    ASSERT_TRUE(limitRequestSnapshot.has_value());
    EXPECT_EQ(limitRequestSnapshot->side, OrderOperation::SELL);
    EXPECT_EQ(limitRequestSnapshot->type, OrderType::LIMIT);
    ASSERT_TRUE(limitRequestSnapshot->price.has_value());
    EXPECT_EQ(limitRequestSnapshot->price.value(), DecimalConverter::parseDecimal("5000"));
    ASSERT_TRUE(limitRequestSnapshot->timeInForce.has_value());
    EXPECT_EQ(limitRequestSnapshot->timeInForce.value(), "GTC");
}

TEST(OrderPlacementModelTest, MapsOcoOrdersAndShowsAcceptedGroupBeforeAConfirmedChildFill)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    promise<void> releaseWaitPromise;
    const shared_future<void> releaseWait = releaseWaitPromise.get_future().share();
    auto binanceService = make_shared<PlacementDealService>(ExchangerType::BINANCE);
    binanceService->setOcoHandler([](const PlaceOcoRequest &) { return createOcoInfo("NEW", "NEW"); });
    binanceService->setOcoWaitHandler(
        [releaseWait](const OcoInfo &)
        {
            releaseWait.wait_for(2s);
            return createOrderInfo("FILLED", "OCO-SL-1");
        });
    auto bybitService = make_shared<PlacementDealService>(ExchangerType::BYBIT);
    OrderPlacementModel model(taskExecutor, binanceService, bybitService);

    const OcoOrderDraft draft = createOcoDraft(ExchangerType::BINANCE);
    EXPECT_TRUE(model.placeOco(draft));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::WAITING, 1000);
    ASSERT_TRUE(model.getAcceptedOco().has_value());
    EXPECT_EQ(model.getAcceptedOco()->orderListId, "OCO-GROUP-1");
    EXPECT_EQ(model.getAcceptedOco()->takeProfitOrder.orderId, "OCO-TP-1");
    EXPECT_EQ(model.getAcceptedOco()->stopLossOrder.orderId, "OCO-SL-1");
    const optional<PlaceOcoRequest> requestSnapshot = binanceService->getLastOcoRequest();
    ASSERT_TRUE(requestSnapshot.has_value());
    const PlaceOcoRequest &request = requestSnapshot.value();
    EXPECT_EQ(request.symbol, "BTCUSDT");
    EXPECT_EQ(request.side, OrderOperation::SELL);
    EXPECT_EQ(request.quantity, DecimalConverter::parseDecimal("0.01"));
    EXPECT_EQ(request.price, DecimalConverter::parseDecimal("60000"));
    EXPECT_EQ(request.stopPrice, DecimalConverter::parseDecimal("50000"));
    ASSERT_TRUE(request.stopLimitPrice.has_value());
    EXPECT_EQ(request.stopLimitPrice.value(), DecimalConverter::parseDecimal("49900"));
    ASSERT_TRUE(request.stopLimitTimeInForce.has_value());
    EXPECT_EQ(request.stopLimitTimeInForce.value(), "GTC");
    EXPECT_FALSE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));

    releaseWaitPromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::FILLED, 1000);
    ASSERT_TRUE(model.getTerminalOrder().has_value());
    EXPECT_EQ(model.getTerminalOrder()->orderId, "OCO-SL-1");
    EXPECT_EQ(model.getTerminalOrder()->status, "FILLED");

    binanceService->setOcoHandler([](const PlaceOcoRequest &) { return createOcoInfo("FILLED", "NEW", "OCO-TP"); });
    EXPECT_TRUE(model.placeOco(createOcoDraft(ExchangerType::BINANCE)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::FILLED, 1000);
    ASSERT_TRUE(model.getTerminalOrder().has_value());
    EXPECT_EQ(model.getTerminalOrder()->orderId, "OCO-TP-1");
    EXPECT_EQ(binanceService->getOcoWaitCount(), 1u);
}

TEST(OrderPlacementModelTest, PreservesOcoPlacementAndMonitoringFailuresWithAcceptedGroupDetails)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService = make_shared<PlacementDealService>(ExchangerType::BINANCE);
    binanceService->setOcoHandler(
        [](const PlaceOcoRequest &) -> OcoInfo
        { throw runtime_error("Binance placeOco: private validation rejected the stop leg"); });
    auto bybitService = make_shared<PlacementDealService>(ExchangerType::BYBIT);
    OrderPlacementModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOco(createOcoDraft(ExchangerType::BINANCE)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::SUBMISSION_FAILED, 1000);
    EXPECT_EQ(model.getError(), QString("Binance placeOco: private validation rejected the stop leg"));
    EXPECT_FALSE(model.getAcceptedOco().has_value());

    binanceService->setOcoHandler([](const PlaceOcoRequest &) { return createOcoInfo("NEW", "NEW", "OCO-2"); });
    binanceService->setOcoWaitHandler([](const OcoInfo &) -> OrderInfo
                                      { throw runtime_error("unfilled child cleanup failed after stream error"); });
    EXPECT_TRUE(model.placeOco(createOcoDraft(ExchangerType::BINANCE)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::WAIT_FAILED, 1000);
    EXPECT_EQ(model.getError(), QString("unfilled child cleanup failed after stream error"));
    ASSERT_TRUE(model.getAcceptedOco().has_value());
    EXPECT_EQ(model.getAcceptedOco()->orderListId, "OCO-2");
}

TEST(OrderPlacementModelTest, ShowsAcceptedWaitingAndFilledStatesAndRejectsASecondActiveOrder)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    promise<void> releaseWaitPromise;
    const shared_future<void> releaseWait = releaseWaitPromise.get_future().share();
    auto binanceService = make_shared<PlacementDealService>(ExchangerType::BINANCE);
    binanceService->setBuyHandler([](const string &, const string &, Decimal)
                                  { return createOrderInfo("NEW", "ACTIVE-1"); });
    binanceService->setWaitHandler(
        [releaseWait](const string &, const string &)
        {
            releaseWait.wait_for(2s);
            return createOrderInfo("FILLED", "ACTIVE-1");
        });
    auto bybitService = make_shared<PlacementDealService>(ExchangerType::BYBIT);
    OrderPlacementModel model(taskExecutor, binanceService, bybitService);
    QSignalSpy statusSpy(&model, &OrderPlacementModel::statusChanged);

    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::WAITING, 1000);
    ASSERT_TRUE(model.getAcceptedOrder().has_value());
    EXPECT_EQ(model.getAcceptedOrder()->orderId, "ACTIVE-1");
    EXPECT_FALSE(model.placeOrder(createDraft(ExchangerType::BYBIT, OperationType::SELL_CRYPTO)));
    EXPECT_EQ(bybitService->getSellCount(), 0u);

    vector<OrderPlacementModel::Status> observedStatuses;
    for (const QList<QVariant> &arguments : statusSpy)
    {
        observedStatuses.push_back(arguments[0].value<OrderPlacementModel::Status>());
    }
    EXPECT_NE(ranges::find(observedStatuses, OrderPlacementModel::Status::SUBMITTING), observedStatuses.end());
    EXPECT_NE(ranges::find(observedStatuses, OrderPlacementModel::Status::ACCEPTED), observedStatuses.end());
    EXPECT_NE(ranges::find(observedStatuses, OrderPlacementModel::Status::WAITING), observedStatuses.end());

    releaseWaitPromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::FILLED, 1000);
    ASSERT_TRUE(model.getTerminalOrder().has_value());
    EXPECT_EQ(model.getTerminalOrder()->status, "FILLED");
}

TEST(OrderPlacementModelTest, PreservesExactSubmissionAndWaitFailuresWithoutLosingAcceptedOrder)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService = make_shared<PlacementDealService>(ExchangerType::BINANCE);
    binanceService->setBuyHandler([](const string &, const string &, Decimal) -> OrderInfo
                                  { throw runtime_error("private validation rejected the quantity"); });
    auto bybitService = make_shared<PlacementDealService>(ExchangerType::BYBIT);
    OrderPlacementModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::SUBMISSION_FAILED, 1000);
    EXPECT_EQ(model.getError(), QString("private validation rejected the quantity"));
    EXPECT_FALSE(model.getAcceptedOrder().has_value());

    binanceService->setBuyHandler([](const string &, const string &, Decimal) -> OrderInfo
                                  { throw runtime_error("exchange placement request failed"); });
    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::SUBMISSION_FAILED, 1000);
    EXPECT_EQ(model.getError(), QString("exchange placement request failed"));
    EXPECT_FALSE(model.getAcceptedOrder().has_value());

    binanceService->setBuyHandler([](const string &, const string &, Decimal)
                                  { return createOrderInfo("NEW", "WAIT-FAIL"); });
    binanceService->setWaitHandler([](const string &, const string &) -> OrderInfo
                                   { throw runtime_error("user stream disconnected"); });
    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::WAIT_FAILED, 1000);
    EXPECT_EQ(model.getError(), QString("user stream disconnected"));
    ASSERT_TRUE(model.getAcceptedOrder().has_value());
    EXPECT_EQ(model.getAcceptedOrder()->orderId, "WAIT-FAIL");
}

TEST(OrderPlacementModelTest, StopsStreamsBeforeJoiningAPendingOrderWait)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    atomic<bool> stopRequested = false;
    auto binanceService = make_shared<PlacementDealService>(ExchangerType::BINANCE);
    binanceService->setBuyHandler([](const string &, const string &, Decimal)
                                  { return createOrderInfo("NEW", "SHUTDOWN-1"); });
    binanceService->setWaitHandler(
        [&stopRequested](const string &, const string &) -> OrderInfo
        {
            const auto deadline = chrono::steady_clock::now() + 2s;
            while (!stopRequested.load() && chrono::steady_clock::now() < deadline)
            {
                this_thread::yield();
            }
            throw runtime_error("stream stopped during shutdown");
        });
    auto bybitService = make_shared<PlacementDealService>(ExchangerType::BYBIT);
    OrderPlacementModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::WAITING, 1000);
    stopRequested = true;
    binanceService->stopUserStream();
    bybitService->stopUserStream();

    const auto startedAt = chrono::steady_clock::now();
    taskExecutor.stopAndWait();
    EXPECT_LT(chrono::steady_clock::now() - startedAt, 1s);
    EXPECT_EQ(binanceService->getStopCount(), 1u);
    EXPECT_EQ(bybitService->getStopCount(), 1u);
}

TEST(OrderPlacementModelTest, StopsStreamsBeforeJoiningAPendingOcoWait)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    atomic<bool> stopRequested = false;
    auto binanceService = make_shared<PlacementDealService>(ExchangerType::BINANCE);
    binanceService->setOcoHandler([](const PlaceOcoRequest &) { return createOcoInfo("NEW", "NEW"); });
    binanceService->setOcoWaitHandler(
        [&stopRequested](const OcoInfo &) -> OrderInfo
        {
            const auto deadline = chrono::steady_clock::now() + 2s;
            while (!stopRequested.load() && chrono::steady_clock::now() < deadline)
            {
                this_thread::yield();
            }
            throw runtime_error("OCO stream stopped during shutdown");
        });
    auto bybitService = make_shared<PlacementDealService>(ExchangerType::BYBIT);
    OrderPlacementModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOco(createOcoDraft(ExchangerType::BINANCE)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getStatus(), OrderPlacementModel::Status::WAITING, 1000);
    stopRequested = true;
    binanceService->stopUserStream();
    bybitService->stopUserStream();

    const auto startedAt = chrono::steady_clock::now();
    taskExecutor.stopAndWait();
    EXPECT_LT(chrono::steady_clock::now() - startedAt, 1s);
    EXPECT_EQ(binanceService->getStopCount(), 1u);
    EXPECT_EQ(bybitService->getStopCount(), 1u);
}

TEST(OrderPlacementModelTest, ConfirmsNormalizedValuesAndDoesNotSubmitWhenConfirmationIsCancelled)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    auto binanceService = make_shared<PlacementDealService>(ExchangerType::BINANCE, true);
    auto bybitService = make_shared<PlacementDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderPlacementModel orderPlacementModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderPlacementModel);
    auto *symbolStatus = window.findChild<QLabel *>("selectedSymbolInfoStatus");
    auto *amountInput = window.findChild<QLineEdit *>("orderAmountInput");
    auto *proceedButton = window.findChild<QPushButton *>("orderProceedButton");

    ASSERT_NE(symbolStatus, nullptr);
    ASSERT_NE(amountInput, nullptr);
    ASSERT_NE(proceedButton, nullptr);

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);
    balanceCatalog.loadBalances();
    QTRY_COMPARE_WITH_TIMEOUT(symbolStatus->text(), QString("Trading rules are ready for BTCUSDT."), 1000);
    amountInput->setText("0.010");
    ASSERT_TRUE(proceedButton->isEnabled());

    bool reviewedSummary = false;
    QTimer::singleShot(0,
                       [&reviewedSummary]()
                       {
                           auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                           if (dialog == nullptr)
                           {
                               return;
                           }
                           auto *summary = dialog->findChild<QLabel *>("orderConfirmationSummary");
                           auto *cancelButton = dialog->findChild<QPushButton *>("cancelOrderConfirmationButton");
                           if (summary != nullptr && cancelButton != nullptr)
                           {
                               reviewedSummary = summary->text().contains("Exchange: Binance") &&
                                                 summary->text().contains("Pair: BTC/USDT (BTCUSDT)") &&
                                                 summary->text().contains("Amount: 0.01 BTC");
                               QTest::mouseClick(cancelButton, Qt::LeftButton);
                           }
                       });
    QTest::mouseClick(proceedButton, Qt::LeftButton);

    EXPECT_TRUE(reviewedSummary);
    EXPECT_EQ(binanceService->getBuyCount(), 0u);
    EXPECT_EQ(orderPlacementModel.getStatus(), OrderPlacementModel::Status::IDLE);
}

TEST(OrderPlacementModelTest, ShowsExactPrivateValidationFailureAfterConfirmedSubmission)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    auto binanceService = make_shared<PlacementDealService>(ExchangerType::BINANCE, true);
    binanceService->setBuyHandler([](const string &, const string &, Decimal) -> OrderInfo
                                  { throw runtime_error("Binance placeOrder: amount is below the exchange minimum"); });
    auto bybitService = make_shared<PlacementDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderPlacementModel orderPlacementModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderPlacementModel);
    auto *symbolStatus = window.findChild<QLabel *>("selectedSymbolInfoStatus");
    auto *amountInput = window.findChild<QLineEdit *>("orderAmountInput");
    auto *proceedButton = window.findChild<QPushButton *>("orderProceedButton");
    auto *placementTitle = window.findChild<QLabel *>("orderPlacementStatusTitle");
    auto *placementError = window.findChild<QLabel *>("orderPlacementStatusError");

    ASSERT_NE(symbolStatus, nullptr);
    ASSERT_NE(amountInput, nullptr);
    ASSERT_NE(proceedButton, nullptr);
    ASSERT_NE(placementTitle, nullptr);
    ASSERT_NE(placementError, nullptr);

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);
    balanceCatalog.loadBalances();
    QTRY_COMPARE_WITH_TIMEOUT(symbolStatus->text(), QString("Trading rules are ready for BTCUSDT."), 1000);
    amountInput->setText("0.010");

    QTimer::singleShot(0,
                       []()
                       {
                           auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                           if (dialog != nullptr)
                           {
                               auto *placeButton = dialog->findChild<QPushButton *>("placeConfirmedOrderButton");
                               if (placeButton != nullptr)
                               {
                                   QTest::mouseClick(placeButton, Qt::LeftButton);
                               }
                           }
                       });
    QTest::mouseClick(proceedButton, Qt::LeftButton);

    QTRY_COMPARE_WITH_TIMEOUT(orderPlacementModel.getStatus(), OrderPlacementModel::Status::SUBMISSION_FAILED, 1000);
    EXPECT_EQ(placementTitle->text(), QString("Order placement failed"));
    EXPECT_EQ(placementError->text(), QString("Binance placeOrder: amount is below the exchange minimum"));
    EXPECT_TRUE(placementError->isVisible());
    EXPECT_TRUE(proceedButton->isEnabled());
}

TEST(OrderPlacementModelTest, PlacesOnlyAfterConfirmationAndShowsAcceptedThenFilledStatus)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    promise<void> releaseWaitPromise;
    const shared_future<void> releaseWait = releaseWaitPromise.get_future().share();
    auto binanceService = make_shared<PlacementDealService>(ExchangerType::BINANCE, true);
    binanceService->setBuyHandler([](const string &, const string &, Decimal)
                                  { return createOrderInfo("NEW", "UI-ORDER-1"); });
    binanceService->setWaitHandler(
        [releaseWait](const string &, const string &)
        {
            releaseWait.wait_for(2s);
            return createOrderInfo("FILLED", "UI-ORDER-1");
        });
    auto bybitService = make_shared<PlacementDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderPlacementModel orderPlacementModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderPlacementModel);
    auto *symbolStatus = window.findChild<QLabel *>("selectedSymbolInfoStatus");
    auto *amountInput = window.findChild<QLineEdit *>("orderAmountInput");
    auto *proceedButton = window.findChild<QPushButton *>("orderProceedButton");
    auto *placementTitle = window.findChild<QLabel *>("orderPlacementStatusTitle");
    auto *placementDetails = window.findChild<QLabel *>("orderPlacementStatusDetails");

    ASSERT_NE(symbolStatus, nullptr);
    ASSERT_NE(amountInput, nullptr);
    ASSERT_NE(proceedButton, nullptr);
    ASSERT_NE(placementTitle, nullptr);
    ASSERT_NE(placementDetails, nullptr);

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);
    balanceCatalog.loadBalances();
    QTRY_COMPARE_WITH_TIMEOUT(symbolStatus->text(), QString("Trading rules are ready for BTCUSDT."), 1000);
    amountInput->setText("0.010");
    ASSERT_TRUE(proceedButton->isEnabled());

    QTimer::singleShot(0,
                       []()
                       {
                           auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                           if (dialog != nullptr)
                           {
                               auto *placeButton = dialog->findChild<QPushButton *>("placeConfirmedOrderButton");
                               if (placeButton != nullptr)
                               {
                                   QTest::mouseClick(placeButton, Qt::LeftButton);
                               }
                           }
                       });
    QTest::mouseClick(proceedButton, Qt::LeftButton);

    QTRY_COMPARE_WITH_TIMEOUT(orderPlacementModel.getStatus(), OrderPlacementModel::Status::WAITING, 1000);
    EXPECT_EQ(placementTitle->text(), QString("Order accepted · waiting for a confirmed fill"));
    EXPECT_TRUE(placementDetails->text().contains("Order ID: UI-ORDER-1 · Exchange status: NEW"));
    EXPECT_FALSE(proceedButton->isEnabled());

    releaseWaitPromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(orderPlacementModel.getStatus(), OrderPlacementModel::Status::FILLED, 1000);
    EXPECT_EQ(placementTitle->text(), QString("Order filled"));
    EXPECT_TRUE(placementDetails->text().contains("Exchange status: FILLED"));
    EXPECT_TRUE(proceedButton->isEnabled());
}

TEST(OrderPlacementModelTest, ConfirmsOcoPlacementAndShowsGroupChildrenAndConfirmedFilledChild)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    promise<void> releaseWaitPromise;
    const shared_future<void> releaseWait = releaseWaitPromise.get_future().share();
    auto binanceService = make_shared<PlacementDealService>(ExchangerType::BINANCE, true);
    binanceService->setOcoHandler([](const PlaceOcoRequest &) { return createOcoInfo("NEW", "NEW", "UI-OCO-1"); });
    binanceService->setOcoWaitHandler(
        [releaseWait](const OcoInfo &)
        {
            releaseWait.wait_for(2s);
            return createOrderInfo("FILLED", "OCO-SL-1");
        });
    auto bybitService = make_shared<PlacementDealService>(ExchangerType::BYBIT);
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderPlacementModel orderPlacementModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderPlacementModel);
    auto *symbolStatus = window.findChild<QLabel *>("selectedSymbolInfoStatus");
    auto *operationSelector = window.findChild<QComboBox *>("orderOperationSelector");
    auto *ocoSideSelector = window.findChild<QComboBox *>("placeOcoSideSelector");
    auto *amountInput = window.findChild<QLineEdit *>("orderAmountInput");
    auto *limitPriceInput = window.findChild<QLineEdit *>("placeOcoLimitPriceInput");
    auto *stopPriceInput = window.findChild<QLineEdit *>("placeOcoStopPriceInput");
    auto *proceedButton = window.findChild<QPushButton *>("orderProceedButton");
    auto *placementTitle = window.findChild<QLabel *>("orderPlacementStatusTitle");
    auto *placementDetails = window.findChild<QLabel *>("orderPlacementStatusDetails");

    ASSERT_NE(symbolStatus, nullptr);
    ASSERT_NE(operationSelector, nullptr);
    ASSERT_NE(ocoSideSelector, nullptr);
    ASSERT_NE(amountInput, nullptr);
    ASSERT_NE(limitPriceInput, nullptr);
    ASSERT_NE(stopPriceInput, nullptr);
    ASSERT_NE(proceedButton, nullptr);
    ASSERT_NE(placementTitle, nullptr);
    ASSERT_NE(placementDetails, nullptr);

    window.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceService, bybitService);
    balanceCatalog.loadBalances();
    QTRY_COMPARE_WITH_TIMEOUT(symbolStatus->text(), QString("Trading rules are ready for BTCUSDT."), 1000);
    operationSelector->setCurrentText("Place OCO");
    ocoSideSelector->setCurrentText("Sell");
    amountInput->setText("0.010");
    limitPriceInput->setText("60000");
    stopPriceInput->setText("50000");
    ASSERT_TRUE(proceedButton->isEnabled());

    bool reviewedSummary = false;
    QTimer::singleShot(0,
                       [&reviewedSummary]()
                       {
                           auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                           if (dialog == nullptr)
                           {
                               return;
                           }
                           auto *summary = dialog->findChild<QLabel *>("orderConfirmationSummary");
                           auto *cancelButton = dialog->findChild<QPushButton *>("cancelOrderConfirmationButton");
                           if (summary != nullptr && cancelButton != nullptr)
                           {
                               reviewedSummary = summary->text().contains("Operation: Place OCO") &&
                                                 summary->text().contains("Limit leg: 60000 USDT per BTC") &&
                                                 summary->text().contains("Stop trigger: 50000 USDT per BTC");
                               QTest::mouseClick(cancelButton, Qt::LeftButton);
                           }
                       });
    QTest::mouseClick(proceedButton, Qt::LeftButton);

    EXPECT_TRUE(reviewedSummary);
    EXPECT_EQ(binanceService->getOcoCount(), 0u);
    EXPECT_EQ(orderPlacementModel.getStatus(), OrderPlacementModel::Status::IDLE);

    QTimer::singleShot(0,
                       []()
                       {
                           auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                           if (dialog != nullptr)
                           {
                               auto *placeButton = dialog->findChild<QPushButton *>("placeConfirmedOrderButton");
                               if (placeButton != nullptr)
                               {
                                   QTest::mouseClick(placeButton, Qt::LeftButton);
                               }
                           }
                       });
    QTest::mouseClick(proceedButton, Qt::LeftButton);

    QTRY_COMPARE_WITH_TIMEOUT(orderPlacementModel.getStatus(), OrderPlacementModel::Status::WAITING, 1000);
    EXPECT_EQ(placementTitle->text(), QString("OCO order accepted · waiting for a confirmed child fill"));
    EXPECT_TRUE(placementDetails->text().contains("OCO group ID: UI-OCO-1"));
    EXPECT_TRUE(placementDetails->text().contains("Take-profit child ID: OCO-TP-1"));
    EXPECT_TRUE(placementDetails->text().contains("Stop-loss child ID: OCO-SL-1"));
    const optional<PlaceOcoRequest> requestSnapshot = binanceService->getLastOcoRequest();
    ASSERT_TRUE(requestSnapshot.has_value());
    EXPECT_EQ(requestSnapshot->side, OrderOperation::SELL);
    EXPECT_FALSE(requestSnapshot->stopLimitPrice.has_value());
    EXPECT_FALSE(requestSnapshot->stopLimitTimeInForce.has_value());
    EXPECT_FALSE(proceedButton->isEnabled());

    releaseWaitPromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(orderPlacementModel.getStatus(), OrderPlacementModel::Status::FILLED, 1000);
    EXPECT_EQ(placementTitle->text(), QString("OCO child order filled"));
    EXPECT_TRUE(placementDetails->text().contains("Confirmed filled child ID: OCO-SL-1"));
    EXPECT_TRUE(proceedButton->isEnabled());
}
