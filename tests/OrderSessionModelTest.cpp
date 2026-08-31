#include "graphical/models/OrderSessionModel.hpp"
#include "TestDealService.hpp"
#include "graphical/CryptoDealWindow.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"
#include "graphical/models/BalanceCatalog.hpp"
#include "graphical/models/PairCatalog.hpp"
#include "graphical/models/SymbolInfoCatalog.hpp"

#include <QApplication>
#include <QTreeWidget>
#include <QtTest/QTest>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
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
        static char applicationName[] = "OrderSessionModelTests";
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
        orderInfo.price = DecimalConverter::parseDecimal("50000");
        orderInfo.origQty = DecimalConverter::parseDecimal("0.01");
        orderInfo.executedQty =
            status == "FILLED" || status == "Filled" ? DecimalConverter::parseDecimal("0.01") : Decimal{};
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

    OcoOrderDraft createOcoDraft()
    {
        OcoOrderDraft draft;
        draft.exchangerType = ExchangerType::BINANCE;
        draft.pair = {"BTCUSDT", "BTC", "USDT"};
        draft.side = OrderOperation::SELL;
        draft.quantity = DecimalConverter::parseDecimal("0.01");
        draft.limitPrice = DecimalConverter::parseDecimal("60000");
        draft.stopPrice = DecimalConverter::parseDecimal("50000");
        draft.quantityText = "0.010";
        draft.limitPriceText = "60000";
        draft.stopPriceText = "50000";
        return draft;
    }

    OcoInfo createOcoInfo(const string &takeProfitStatus = "NEW", const string &stopLossStatus = "NEW")
    {
        OcoInfo ocoInfo;
        ocoInfo.orderListId = "OCO-GROUP-1";
        ocoInfo.takeProfitOrder = createOrderInfo(takeProfitStatus, "OCO-TP-1");
        ocoInfo.stopLossOrder = createOrderInfo(stopLossStatus, "OCO-SL-1");
        return ocoInfo;
    }

    OcoWaitResult createOcoWaitResult(const string &filledOrderId = "OCO-TP-1",
                                      const string &filledStatus = "FILLED",
                                      const string &siblingStatus = "CANCELED")
    {
        const bool isTakeProfitFilled = filledOrderId == "OCO-TP-1";
        return {createOrderInfo(filledStatus, filledOrderId),
                createOrderInfo(siblingStatus, isTakeProfitFilled ? "OCO-SL-1" : "OCO-TP-1")};
    }

    class SessionDealService final : public TestDealService
    {
      public:
        using BasicHandler = function<OrderInfo(const string &, const string &, Decimal)>;
        using OrderHandler = function<OrderInfo(const PlaceOrderRequest &)>;
        using WaitHandler = function<OrderInfo(const string &, const string &)>;
        using OcoHandler = function<OcoInfo(const PlaceOcoRequest &)>;
        using OcoWaitHandler = function<OcoWaitResult(const OcoInfo &)>;

      private:
        BasicHandler buyHandler;
        BasicHandler sellHandler;
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
        optional<PlaceOrderRequest> lastOrderRequest;

      public:
        explicit SessionDealService(ExchangerType exchangerType)
            : TestDealService(exchangerType), buyCount(0), sellCount(0), orderCount(0), waitCount(0), ocoCount(0),
              ocoWaitCount(0), stopCount(0)
        {}

        void setBuyHandler(BasicHandler handler)
        {
            buyHandler = move(handler);
        }

        void setSellHandler(BasicHandler handler)
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

        size_t getOcoCount() const
        {
            return ocoCount.load();
        }

        size_t getOcoWaitCount() const
        {
            return ocoWaitCount.load();
        }

        size_t getStopCount() const
        {
            return stopCount.load();
        }

        optional<PlaceOrderRequest> getLastOrderRequest() const
        {
            return lastOrderRequest;
        }

        OrderInfo buyCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity) override
        {
            ++buyCount;
            return buyHandler ? buyHandler(baseAsset, quoteAsset, quantity) : createOrderInfo("FILLED", "BUY-1");
        }

        OrderInfo sellCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity) override
        {
            ++sellCount;
            return sellHandler ? sellHandler(baseAsset, quoteAsset, quantity) : createOrderInfo("FILLED", "SELL-1");
        }

        OrderInfo placeOrder(const PlaceOrderRequest &request) override
        {
            ++orderCount;
            lastOrderRequest = request;
            return orderHandler ? orderHandler(request) : createOrderInfo("Filled", "CUSTOM-1");
        }

        OrderInfo waitUntilOrderFilled(const string &symbol, const string &orderId) override
        {
            ++waitCount;
            return waitHandler ? waitHandler(symbol, orderId) : createOrderInfo("FILLED", orderId);
        }

        OcoInfo placeOco(const PlaceOcoRequest &request) override
        {
            ++ocoCount;
            return ocoHandler ? ocoHandler(request) : createOcoInfo();
        }

        OcoWaitResult waitUntilOcoOrderFilled(const OcoInfo &ocoInfo) override
        {
            ++ocoWaitCount;
            return ocoWaitHandler ? ocoWaitHandler(ocoInfo) : createOcoWaitResult();
        }

        void stopUserStream() override
        {
            ++stopCount;
        }
    };

    const OrderSessionModel::Entry &getEntryById(const OrderSessionModel &model, OrderSessionModel::EntryId entryId)
    {
        const OrderSessionModel::Entry *entry = model.findEntryById(entryId);
        EXPECT_NE(entry, nullptr);
        return *entry;
    }

    const OrderSessionModel::Entry &getEntryByExchangeOrderId(const OrderSessionModel &model, const string &orderId)
    {
        const vector<OrderSessionModel::Entry> &entries = model.getEntries();
        const auto entry = ranges::find_if(entries,
                                           [&orderId](const OrderSessionModel::Entry &candidate) {
                                               return candidate.acceptedOrder.has_value() &&
                                                      candidate.acceptedOrder->orderId == orderId;
                                           });
        EXPECT_NE(entry, entries.end());
        return *entry;
    }

    bool isSortedByRecentUpdate(const vector<OrderSessionModel::Entry> &entries)
    {
        return ranges::is_sorted(entries,
                                 [](const OrderSessionModel::Entry &left, const OrderSessionModel::Entry &right)
                                 {
                                     if (left.updatedAtMs != right.updatedAtMs)
                                     {
                                         return left.updatedAtMs > right.updatedAtMs;
                                     }
                                     return left.id > right.id;
                                 });
    }
}

TEST(OrderSessionModelTest, MapsBasicOperationsAndRetainsIndependentTerminalEntries)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService = make_shared<SessionDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SessionDealService>(ExchangerType::BYBIT);
    OrderSessionModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::SELL_CRYPTO)));
    BasicOrderDraft customDraft = createDraft(ExchangerType::BYBIT, OperationType::PLACE_ORDER);
    customDraft.type = OrderType::MARKET;
    EXPECT_TRUE(model.placeOrder(customDraft));

    QTRY_COMPARE_WITH_TIMEOUT(model.getEntries().size(), 3u, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(ranges::all_of(model.getEntries(),
                                            [](const OrderSessionModel::Entry &entry)
                                            { return entry.status == OrderSessionModel::Status::FILLED; }),
                             1000);
    EXPECT_EQ(binanceService->getBuyCount(), 1u);
    EXPECT_EQ(binanceService->getSellCount(), 1u);
    EXPECT_EQ(bybitService->getOrderCount(), 1u);
    ASSERT_TRUE(bybitService->getLastOrderRequest().has_value());
    EXPECT_EQ(bybitService->getLastOrderRequest()->marketUnit, "baseCoin");
}

TEST(OrderSessionModelTest, UpdatesConcurrentEntriesIndependentlyAndKeepsRecentOrder)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    promise<void> releaseBinanceWaitPromise;
    promise<void> releaseBybitWaitPromise;
    const shared_future<void> releaseBinanceWait = releaseBinanceWaitPromise.get_future().share();
    const shared_future<void> releaseBybitWait = releaseBybitWaitPromise.get_future().share();
    auto binanceService = make_shared<SessionDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SessionDealService>(ExchangerType::BYBIT);
    binanceService->setBuyHandler([](const string &, const string &, Decimal)
                                  { return createOrderInfo("NEW", "BIN-1"); });
    binanceService->setWaitHandler(
        [releaseBinanceWait](const string &, const string &)
        {
            releaseBinanceWait.wait_for(2s);
            return createOrderInfo("FILLED", "BIN-1");
        });
    bybitService->setBuyHandler([](const string &, const string &, Decimal)
                                { return createOrderInfo("New", "BYB-1"); });
    bybitService->setWaitHandler(
        [releaseBybitWait](const string &, const string &)
        {
            releaseBybitWait.wait_for(2s);
            return createOrderInfo("Filled", "BYB-1");
        });
    OrderSessionModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BYBIT, OperationType::BUY_CRYPTO)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getEntries().size(), 2u, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(ranges::all_of(model.getEntries(),
                                            [](const OrderSessionModel::Entry &entry)
                                            { return entry.status == OrderSessionModel::Status::WAITING; }),
                             1000);

    releaseBybitWaitPromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(getEntryByExchangeOrderId(model, "BYB-1").status,
                              OrderSessionModel::Status::FILLED,
                              1000);
    EXPECT_EQ(getEntryByExchangeOrderId(model, "BIN-1").status, OrderSessionModel::Status::WAITING);
    EXPECT_TRUE(isSortedByRecentUpdate(model.getEntries()));

    QTest::qWait(5);
    releaseBinanceWaitPromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(getEntryByExchangeOrderId(model, "BIN-1").status,
                              OrderSessionModel::Status::FILLED,
                              1000);
    EXPECT_TRUE(isSortedByRecentUpdate(model.getEntries()));
}

TEST(OrderSessionModelTest, RetainsExactSubmissionAndMonitoringFailuresInSessionHistory)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService = make_shared<SessionDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SessionDealService>(ExchangerType::BYBIT);
    binanceService->setBuyHandler([](const string &, const string &, Decimal) -> OrderInfo
                                  { throw runtime_error("Binance rejected quantity below its minimum"); });
    OrderSessionModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getEntries().front().status, OrderSessionModel::Status::SUBMISSION_FAILED, 1000);
    const OrderSessionModel::EntryId failedEntryId = model.getEntries().front().id;
    const OrderSessionModel::Entry &failedEntry = getEntryById(model, failedEntryId);
    EXPECT_EQ(failedEntry.error, QString("Binance rejected quantity below its minimum"));
    EXPECT_FALSE(failedEntry.acceptedOrder.has_value());
    EXPECT_FALSE(OrderSessionModel::isActiveStatus(failedEntry.status));

    binanceService->setBuyHandler([](const string &, const string &, Decimal)
                                  { return createOrderInfo("NEW", "WAIT-1"); });
    binanceService->setWaitHandler([](const string &, const string &) -> OrderInfo
                                   { throw runtime_error("Order stream disconnected"); });
    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    QTRY_VERIFY_WITH_TIMEOUT(ranges::any_of(model.getEntries(),
                                            [](const OrderSessionModel::Entry &entry) {
                                                return entry.acceptedOrder.has_value() &&
                                                       entry.acceptedOrder->orderId == "WAIT-1";
                                            }),
                             1000);
    const OrderSessionModel::EntryId monitoringFailureEntryId = getEntryByExchangeOrderId(model, "WAIT-1").id;
    QTRY_COMPARE_WITH_TIMEOUT(model.findEntryById(monitoringFailureEntryId)->status,
                              OrderSessionModel::Status::MONITORING_FAILED,
                              1000);
    const OrderSessionModel::Entry &monitoringFailure = getEntryById(model, monitoringFailureEntryId);
    EXPECT_EQ(monitoringFailure.error, QString("Order stream disconnected"));
    EXPECT_TRUE(monitoringFailure.acceptedOrder.has_value());
    EXPECT_TRUE(OrderSessionModel::isActiveStatus(monitoringFailure.status));
}

TEST(OrderSessionModelTest, RepresentsOcoGroupAndChildOrdersUntilConfirmedChildFills)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    promise<void> releaseWaitPromise;
    const shared_future<void> releaseWait = releaseWaitPromise.get_future().share();
    auto binanceService = make_shared<SessionDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SessionDealService>(ExchangerType::BYBIT);
    binanceService->setOcoHandler([](const PlaceOcoRequest &) { return createOcoInfo(); });
    binanceService->setOcoWaitHandler(
        [releaseWait](const OcoInfo &)
        {
            releaseWait.wait_for(2s);
            return createOcoWaitResult("OCO-SL-1");
        });
    OrderSessionModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOco(createOcoDraft()));
    QTRY_COMPARE_WITH_TIMEOUT(model.getEntries().size(), 1u, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(model.getEntries().front().status, OrderSessionModel::Status::WAITING, 1000);
    const OrderSessionModel::EntryId entryId = model.getEntries().front().id;
    const OrderSessionModel::Entry &waitingEntry = getEntryById(model, entryId);
    ASSERT_TRUE(waitingEntry.acceptedOco.has_value());
    EXPECT_EQ(waitingEntry.acceptedOco->orderListId, "OCO-GROUP-1");
    EXPECT_EQ(waitingEntry.acceptedOco->takeProfitOrder.orderId, "OCO-TP-1");
    EXPECT_EQ(waitingEntry.acceptedOco->stopLossOrder.orderId, "OCO-SL-1");

    releaseWaitPromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(getEntryById(model, entryId).status, OrderSessionModel::Status::FILLED, 1000);
    const OrderSessionModel::Entry &filledEntry = getEntryById(model, entryId);
    ASSERT_TRUE(filledEntry.terminalOrder.has_value());
    EXPECT_EQ(filledEntry.terminalOrder->orderId, "OCO-SL-1");
    ASSERT_TRUE(filledEntry.acceptedOco.has_value());
    EXPECT_EQ(filledEntry.acceptedOco->stopLossOrder.status, "FILLED");
    EXPECT_EQ(filledEntry.acceptedOco->takeProfitOrder.status, "CANCELED");
}

TEST(OrderSessionModelTest, MarksExchangeConfirmedBasicFillTerminalWithoutStartingMonitoring)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService = make_shared<SessionDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SessionDealService>(ExchangerType::BYBIT);
    OrderSessionModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getEntries().front().status, OrderSessionModel::Status::FILLED, 1000);
    EXPECT_EQ(binanceService->getWaitCount(), 0u);
    ASSERT_TRUE(model.getEntries().front().terminalOrder.has_value());
    EXPECT_EQ(model.getEntries().front().terminalOrder->status, "FILLED");
}

TEST(OrderSessionModelTest, WaitsForTheSiblingAfterAnAcceptedOcoChildIsFilled)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService = make_shared<SessionDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SessionDealService>(ExchangerType::BYBIT);
    binanceService->setOcoHandler([](const PlaceOcoRequest &) { return createOcoInfo("FILLED", "NEW"); });
    binanceService->setOcoWaitHandler([](const OcoInfo &) { return createOcoWaitResult(); });
    OrderSessionModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOco(createOcoDraft()));
    QTRY_COMPARE_WITH_TIMEOUT(model.getEntries().front().status, OrderSessionModel::Status::FILLED, 1000);
    EXPECT_EQ(binanceService->getOcoWaitCount(), 1u);
    ASSERT_TRUE(model.getEntries().front().terminalOrder.has_value());
    EXPECT_EQ(model.getEntries().front().terminalOrder->orderId, "OCO-TP-1");
    ASSERT_TRUE(model.getEntries().front().acceptedOco.has_value());
    EXPECT_EQ(model.getEntries().front().acceptedOco->stopLossOrder.status, "CANCELED");
}

TEST(OrderSessionModelTest, DoesNotMarkOcoFilledBeforeTheSiblingTerminalStateIsConfirmed)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    promise<void> releaseWaitPromise;
    const shared_future<void> releaseWait = releaseWaitPromise.get_future().share();
    auto binanceService = make_shared<SessionDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SessionDealService>(ExchangerType::BYBIT);
    binanceService->setOcoHandler([](const PlaceOcoRequest &) { return createOcoInfo("FILLED", "NEW"); });
    binanceService->setOcoWaitHandler(
        [releaseWait](const OcoInfo &)
        {
            releaseWait.wait_for(2s);
            return createOcoWaitResult();
        });
    OrderSessionModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOco(createOcoDraft()));
    QTRY_COMPARE_WITH_TIMEOUT(model.getEntries().front().status, OrderSessionModel::Status::WAITING, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(binanceService->getOcoWaitCount(), 1u, 1000);
    ASSERT_TRUE(model.getEntries().front().acceptedOco.has_value());
    EXPECT_EQ(model.getEntries().front().acceptedOco->takeProfitOrder.status, "FILLED");
    EXPECT_EQ(model.getEntries().front().acceptedOco->stopLossOrder.status, "NEW");

    releaseWaitPromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(model.getEntries().front().status, OrderSessionModel::Status::FILLED, 1000);
    ASSERT_TRUE(model.getEntries().front().acceptedOco.has_value());
    EXPECT_EQ(model.getEntries().front().acceptedOco->stopLossOrder.status, "CANCELED");
}

TEST(OrderSessionModelTest, RetainsDistinctEntriesWhenTheSameDraftIsSubmittedMoreThanOnce)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService = make_shared<SessionDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SessionDealService>(ExchangerType::BYBIT);
    OrderSessionModel model(taskExecutor, binanceService, bybitService);
    const BasicOrderDraft draft = createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO);

    EXPECT_TRUE(model.placeOrder(draft));
    EXPECT_TRUE(model.placeOrder(draft));
    QTRY_COMPARE_WITH_TIMEOUT(model.getEntries().size(), 2u, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(ranges::all_of(model.getEntries(),
                                            [](const OrderSessionModel::Entry &entry)
                                            { return entry.status == OrderSessionModel::Status::FILLED; }),
                             1000);
    EXPECT_NE(model.getEntries().at(0).id, model.getEntries().at(1).id);
    EXPECT_EQ(binanceService->getBuyCount(), 2u);
}

TEST(OrderSessionModelTest, KeepsOcoMonitoringFailureActiveWithAcceptedGroupDetails)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    auto binanceService = make_shared<SessionDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SessionDealService>(ExchangerType::BYBIT);
    binanceService->setOcoHandler([](const PlaceOcoRequest &) { return createOcoInfo(); });
    binanceService->setOcoWaitHandler([](const OcoInfo &) -> OcoWaitResult
                                      { throw runtime_error("OCO cleanup could not reconcile the cancelled child"); });
    OrderSessionModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOco(createOcoDraft()));
    QTRY_COMPARE_WITH_TIMEOUT(model.getEntries().front().status, OrderSessionModel::Status::MONITORING_FAILED, 1000);
    const OrderSessionModel::Entry &entry = model.getEntries().front();
    EXPECT_TRUE(OrderSessionModel::isActiveStatus(entry.status));
    ASSERT_TRUE(entry.acceptedOco.has_value());
    EXPECT_EQ(entry.acceptedOco->orderListId, "OCO-GROUP-1");
    EXPECT_EQ(entry.error, QString("OCO cleanup could not reconcile the cancelled child"));
}

TEST(OrderSessionModelTest, DisplaysActiveAndAllSessionTablesWithVisibleOcoChildren)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    promise<void> releaseWaitPromise;
    const shared_future<void> releaseWait = releaseWaitPromise.get_future().share();
    PairCatalog pairCatalog;
    auto binanceService = make_shared<SessionDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SessionDealService>(ExchangerType::BYBIT);
    binanceService->setOcoHandler([](const PlaceOcoRequest &) { return createOcoInfo(); });
    binanceService->setOcoWaitHandler(
        [releaseWait](const OcoInfo &)
        {
            releaseWait.wait_for(2s);
            return createOcoWaitResult();
        });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderSessionModel orderSessionModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderSessionModel);
    auto *activeTable = window.findChild<QTreeWidget *>("activeOrdersTable");
    auto *allSessionTable = window.findChild<QTreeWidget *>("allSessionOrdersTable");

    ASSERT_NE(activeTable, nullptr);
    ASSERT_NE(allSessionTable, nullptr);
    window.show();

    EXPECT_TRUE(orderSessionModel.placeOco(createOcoDraft()));
    QTRY_COMPARE_WITH_TIMEOUT(activeTable->topLevelItemCount(), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(allSessionTable->topLevelItemCount(), 1, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(activeTable->topLevelItem(0)->text(3).contains("OCO group · OCO-GROUP-1"), 1000);
    QTreeWidgetItem *groupItem = activeTable->topLevelItem(0);
    ASSERT_NE(groupItem, nullptr);
    EXPECT_TRUE(groupItem->text(3).contains("OCO group · OCO-GROUP-1"));
    ASSERT_EQ(groupItem->childCount(), 2);
    EXPECT_TRUE(groupItem->child(0)->text(3).contains("OCO-TP-1"));
    EXPECT_TRUE(groupItem->child(1)->text(3).contains("OCO-SL-1"));

    releaseWaitPromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(activeTable->topLevelItemCount(), 0, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(allSessionTable->topLevelItemCount(), 1, 1000);
    EXPECT_TRUE(allSessionTable->topLevelItem(0)->text(7).contains("Confirmed child filled"));
}

TEST(OrderSessionModelTest, ShowsFailedSubmissionOnlyInAllSessionOrdersTable)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    auto binanceService = make_shared<SessionDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SessionDealService>(ExchangerType::BYBIT);
    binanceService->setBuyHandler([](const string &, const string &, Decimal) -> OrderInfo
                                  { throw runtime_error("Binance rejected the submitted amount"); });
    BalanceCatalog balanceCatalog(taskExecutor, binanceService, bybitService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceService, bybitService);
    OrderSessionModel orderSessionModel(taskExecutor, binanceService, bybitService);
    CryptoDealWindow window(pairCatalog, balanceCatalog, symbolInfoCatalog, orderSessionModel);
    auto *activeTable = window.findChild<QTreeWidget *>("activeOrdersTable");
    auto *allSessionTable = window.findChild<QTreeWidget *>("allSessionOrdersTable");

    ASSERT_NE(activeTable, nullptr);
    ASSERT_NE(allSessionTable, nullptr);
    window.show();

    EXPECT_TRUE(orderSessionModel.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    QTRY_COMPARE_WITH_TIMEOUT(allSessionTable->topLevelItemCount(), 1, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(allSessionTable->topLevelItem(0)->text(7).contains("Submission failed"), 1000);
    EXPECT_EQ(activeTable->topLevelItemCount(), 0);
    EXPECT_TRUE(allSessionTable->topLevelItem(0)->text(7).contains("Binance rejected the submitted amount"));
}

TEST(OrderSessionModelTest, StopsStreamsBeforeJoiningIndependentPendingWaits)
{
    getApplication();
    AsyncTaskExecutor taskExecutor;
    atomic<bool> stopRequested = false;
    auto binanceService = make_shared<SessionDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SessionDealService>(ExchangerType::BYBIT);
    binanceService->setBuyHandler([](const string &, const string &, Decimal)
                                  { return createOrderInfo("NEW", "STOP-1"); });
    binanceService->setWaitHandler(
        [&stopRequested](const string &, const string &) -> OrderInfo
        {
            const auto deadline = chrono::steady_clock::now() + 2s;
            while (!stopRequested.load() && chrono::steady_clock::now() < deadline)
            {
                this_thread::yield();
            }
            throw runtime_error("Order stream stopped during shutdown");
        });
    OrderSessionModel model(taskExecutor, binanceService, bybitService);

    EXPECT_TRUE(model.placeOrder(createDraft(ExchangerType::BINANCE, OperationType::BUY_CRYPTO)));
    QTRY_COMPARE_WITH_TIMEOUT(model.getEntries().front().status, OrderSessionModel::Status::WAITING, 1000);

    stopRequested = true;
    binanceService->stopUserStream();
    bybitService->stopUserStream();
    const auto startedAt = chrono::steady_clock::now();
    taskExecutor.stopAndWait();
    EXPECT_LT(chrono::steady_clock::now() - startedAt, 1s);
    EXPECT_EQ(binanceService->getStopCount(), 1u);
    EXPECT_EQ(bybitService->getStopCount(), 1u);
}
