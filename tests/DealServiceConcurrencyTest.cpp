#include "../src/DealService/binance/BinanceDealService.hpp"
#include "../src/DealService/bybit/BybitDealService.hpp"
#include "../src/DealService/common/DecimalConverter.hpp"
#include "../src/DealService/common/OrderWaitInterrupted.hpp"
#include "MockHttpRequest.hpp"
#include "PrivateAccess.hpp"

#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <latch>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {
    class TestableBinanceDealService : public BinanceDealService
    {
      public:
        TestableBinanceDealService() : BinanceDealService("test.binance.com", "api_key", "secret_key", "ws.binance.com")
        {}

        void startDormantRunner()
        {
            runner = std::thread([]() { std::this_thread::sleep_for(50ms); });
        }
    };

    class TestableBybitDealService : public BybitDealService
    {
      public:
        TestableBybitDealService() : BybitDealService("test.bybit.com", "api_key", "secret_key", "ws.bybit.com") {}

        void startDormantRunner()
        {
            runner = std::thread([]() { std::this_thread::sleep_for(50ms); });
        }
    };

    struct PlacementResult
    {
        bool succeeded;
        std::string error;
    };

    std::string createBinanceOrderResponse(long long orderId, const std::string &status)
    {
        return R"({"symbol":"BTCUSDT","orderId":)" + std::to_string(orderId) +
               R"(,"side":"BUY","type":"LIMIT","status":")" + status +
               R"(","price":"50000","origQty":"0.1","executedQty":"0","cumulativeQuoteQty":"0"})";
    }

    std::string createBinanceOrderMessage(long long orderId, const std::string &status)
    {
        return R"({"event":{"e":"executionReport","s":"BTCUSDT","S":"BUY","o":"LIMIT",)"
               R"("f":"GTC","q":"0.1","p":"50000","X":")" +
               status + R"(","i":)" + std::to_string(orderId) + R"(,"z":"0.1","Z":"5000"}})";
    }

    std::string createBybitOrderResponse(const std::string &orderId, const std::string &status)
    {
        return R"({"retCode":0,"retMsg":"OK","result":{"list":[{"symbol":"BTCUSDT","orderId":")" + orderId +
               R"(","orderStatus":")" + status +
               R"(","price":"50000","qty":"0.1","cumExecQty":"0","cumExecValue":"0"}]}})";
    }

    std::string
    createBybitOrderMessage(const std::string &orderId, const std::string &orderLinkId, const std::string &status)
    {
        return R"({"topic":"order","data":[{"category":"spot","symbol":"BTCUSDT","orderId":")" + orderId +
               R"(","orderLinkId":")" + orderLinkId + R"(","side":"Buy","orderType":"Limit","orderStatus":")" + status +
               R"(","price":"50000",)"
               R"("qty":"0.1","cumExecQty":"0.1","cumExecValue":"5000","leavesQty":"0",)"
               R"("avgPrice":"50000"}]})";
    }

    OcoInfo
    createOcoInfo(const std::string &groupId, const std::string &takeProfitOrderId, const std::string &stopLossOrderId)
    {
        OcoInfo ocoInfo;
        ocoInfo.orderListId = groupId;
        ocoInfo.listClientOrderId = groupId;
        ocoInfo.takeProfitOrder.symbol = "BTCUSDT";
        ocoInfo.takeProfitOrder.orderId = takeProfitOrderId;
        ocoInfo.takeProfitOrder.clientOrderId = groupId + "_TP";
        ocoInfo.stopLossOrder.symbol = "BTCUSDT";
        ocoInfo.stopLossOrder.orderId = stopLossOrderId;
        ocoInfo.stopLossOrder.clientOrderId = groupId + "_SL";
        return ocoInfo;
    }

    std::string getBinanceSymbolInfoResponse()
    {
        return R"({"symbols":[{"symbol":"BTCUSDT","status":"TRADING","baseAsset":"BTC",)"
               R"("quoteAsset":"USDT","filters":[)"
               R"({"filterType":"PRICE_FILTER","tickSize":"0.01"},)"
               R"({"filterType":"LOT_SIZE","minQty":"0.0001","stepSize":"0.0001"}]}]})";
    }

    std::string getBybitSymbolInfoResponse()
    {
        return R"({"retCode":0,"retMsg":"OK","result":{"list":[{"symbol":"BTCUSDT",)"
               R"("status":"Trading","baseCoin":"BTC","quoteCoin":"USDT",)"
               R"("priceFilter":{"tickSize":"0.01"},"lotSizeFilter":{"qtyStep":"0.0001",)"
               R"("minOrderQty":"0.0001","maxOrderQty":"1000","minOrderAmt":"10"}}]}})";
    }

    std::size_t countRequestsContaining(const std::string &targetSubstring)
    {
        std::size_t count = 0;
        for (const MockNetwork::RecordedRequest &request : MockNetwork::instance().getRequests())
        {
            if (request.target.find(targetSubstring) != std::string::npos)
            {
                ++count;
            }
        }
        return count;
    }

    template <typename Service> void stopConcurrently(Service &service)
    {
        std::latch ready(2);
        std::latch start(1);
        auto stop = [&service, &ready, &start]()
        {
            ready.count_down();
            start.wait();
            service.stopUserStream();
        };

        std::future<void> first = std::async(std::launch::async, stop);
        std::future<void> second = std::async(std::launch::async, stop);
        ready.wait();
        start.count_down();
        first.get();
        second.get();
    }
}

TEST(DealServiceConcurrencyTest, BinanceDistinctOrderWaitsReceiveIndependentUpdates)
{
    MockNetwork::instance().reset();
    MockNetwork::instance().setResponse("/api/v3/time", R"({"serverTime":1779052073334})");
    MockNetwork::instance().setResponse("orderId=101", createBinanceOrderResponse(101, "NEW"));
    MockNetwork::instance().setResponse("orderId=202", createBinanceOrderResponse(202, "NEW"));

    BinanceDealService service("test.binance.com", "api_key", "secret_key", "ws.binance.com");
    test_private_access::setBinanceStreamStatus(service, StreamStatus::CONNECTED);

    std::future<OrderInfo> first =
        std::async(std::launch::async, [&service]() { return service.waitUntilOrderFilled("BTCUSDT", "101"); });
    std::future<OrderInfo> second =
        std::async(std::launch::async, [&service]() { return service.waitUntilOrderFilled("BTCUSDT", "202"); });

    if (!MockNetwork::instance().waitForRequestCount("/api/v3/order?", 2, 1s))
    {
        test_private_access::setBinanceStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for concurrent Binance reconciliation requests";
    }

    test_private_access::dispatchBinanceUserStreamMessage(service, createBinanceOrderMessage(202, "FILLED"));
    test_private_access::dispatchBinanceUserStreamMessage(service, createBinanceOrderMessage(101, "FILLED"));

    EXPECT_EQ(first.get().orderId, "101");
    EXPECT_EQ(second.get().orderId, "202");
    EXPECT_EQ(countRequestsContaining("/api/v3/time"), 1u);
}

TEST(DealServiceConcurrencyTest, BybitDistinctOrderWaitsReceiveIndependentUpdates)
{
    MockNetwork::instance().reset();
    MockNetwork::instance().setResponse("/v5/market/time",
                                        R"({"retCode":0,"retMsg":"OK","result":{"timeSecond":"1779052073"}})");
    MockNetwork::instance().setResponse("orderId=FIRST", createBybitOrderResponse("FIRST", "New"));
    MockNetwork::instance().setResponse("orderId=SECOND", createBybitOrderResponse("SECOND", "New"));

    BybitDealService service("test.bybit.com", "api_key", "secret_key", "ws.bybit.com");
    test_private_access::setBybitStreamStatus(service, StreamStatus::CONNECTED);

    std::future<OrderInfo> first =
        std::async(std::launch::async, [&service]() { return service.waitUntilOrderFilled("BTCUSDT", "FIRST"); });
    std::future<OrderInfo> second =
        std::async(std::launch::async, [&service]() { return service.waitUntilOrderFilled("BTCUSDT", "SECOND"); });

    if (!MockNetwork::instance().waitForRequestCount("/v5/order/realtime", 2, 1s))
    {
        test_private_access::setBybitStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for concurrent Bybit reconciliation requests";
    }

    test_private_access::dispatchBybitUserStreamMessage(service,
                                                        createBybitOrderMessage("SECOND", "SECOND_CLIENT", "Filled"));
    test_private_access::dispatchBybitUserStreamMessage(service,
                                                        createBybitOrderMessage("FIRST", "FIRST_CLIENT", "Filled"));

    EXPECT_EQ(first.get().orderId, "FIRST");
    EXPECT_EQ(second.get().orderId, "SECOND");
    EXPECT_EQ(countRequestsContaining("/v5/market/time"), 1u);
}

TEST(DealServiceConcurrencyTest, BinanceFillWaitAndConfirmedCancellationShareOneOrderObserver)
{
    MockNetwork::instance().reset();
    MockNetwork::instance().setResponse("/api/v3/time", R"({"serverTime":1779052073334})");
    MockNetwork::instance().setResponse("orderId=250", createBinanceOrderResponse(250, "NEW"));
    MockNetwork::instance().setResponse("orderId=250", createBinanceOrderResponse(250, "NEW"));
    MockNetwork::instance().setResponse("orderId=250", createBinanceOrderResponse(250, "NEW"));

    BinanceDealService service("test.binance.com", "api_key", "secret_key", "ws.binance.com");
    test_private_access::setBinanceStreamStatus(service, StreamStatus::CONNECTED);

    std::future<OrderInfo> fillWait =
        std::async(std::launch::async, [&service]() { return service.waitUntilOrderFilled("BTCUSDT", "250"); });
    ASSERT_TRUE(MockNetwork::instance().waitForRequestCount("/api/v3/order?", 1, 1s));

    OrderQuery query;
    query.symbol = "BTCUSDT";
    query.orderId = "250";
    std::future<OrderInfo> cancellation =
        std::async(std::launch::async, [&service, &query]() { return service.cancelOrderAndWaitUntilTerminal(query); });
    ASSERT_TRUE(MockNetwork::instance().waitForRequestCount("/api/v3/order?", 3, 1s));
    EXPECT_EQ(cancellation.wait_for(25ms), std::future_status::timeout);

    test_private_access::dispatchBinanceUserStreamMessage(service, createBinanceOrderMessage(250, "FILLED"));

    ASSERT_EQ(fillWait.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(cancellation.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(fillWait.get().status, "FILLED");
    EXPECT_EQ(cancellation.get().status, "FILLED");
}

TEST(DealServiceConcurrencyTest, BybitFillWaitAndConfirmedCancellationShareOneOrderObserver)
{
    MockNetwork::instance().reset();
    MockNetwork::instance().setResponse("/v5/market/time",
                                        R"({"retCode":0,"retMsg":"OK","result":{"timeSecond":"1779052073"}})");
    MockNetwork::instance().setResponse("orderId=SHARED", createBybitOrderResponse("SHARED", "New"));
    MockNetwork::instance().setResponse("orderId=SHARED", createBybitOrderResponse("SHARED", "New"));
    MockNetwork::instance().setResponse(
        "/v5/order/cancel",
        R"({"retCode":0,"retMsg":"OK","result":{"orderId":"SHARED","orderLinkId":"SHARED_CLIENT"}})");

    BybitDealService service("test.bybit.com", "api_key", "secret_key", "ws.bybit.com");
    test_private_access::setBybitStreamStatus(service, StreamStatus::CONNECTED);

    std::future<OrderInfo> fillWait =
        std::async(std::launch::async, [&service]() { return service.waitUntilOrderFilled("BTCUSDT", "SHARED"); });
    ASSERT_TRUE(MockNetwork::instance().waitForRequestCount("/v5/order/realtime", 1, 1s));

    OrderQuery query;
    query.symbol = "BTCUSDT";
    query.orderId = "SHARED";
    std::future<OrderInfo> cancellation =
        std::async(std::launch::async, [&service, &query]() { return service.cancelOrderAndWaitUntilTerminal(query); });
    ASSERT_TRUE(MockNetwork::instance().waitForRequestCount("/v5/order/realtime", 2, 1s));
    ASSERT_TRUE(MockNetwork::instance().waitForRequestCount("/v5/order/cancel", 1, 1s));
    EXPECT_EQ(cancellation.wait_for(25ms), std::future_status::timeout);

    test_private_access::dispatchBybitUserStreamMessage(service,
                                                        createBybitOrderMessage("SHARED", "SHARED_CLIENT", "Filled"));

    ASSERT_EQ(fillWait.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(cancellation.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(fillWait.get().status, "Filled");
    EXPECT_EQ(cancellation.get().status, "Filled");
}

TEST(DealServiceConcurrencyTest, BinanceInterruptedConfirmationLeavesSameOrderFillWaitActive)
{
    MockNetwork::instance().reset();
    MockNetwork::instance().setResponse("/api/v3/time", R"({"serverTime":1779052073334})");
    MockNetwork::instance().setResponse("orderId=251", createBinanceOrderResponse(251, "NEW"));
    MockNetwork::instance().setResponse("orderId=251", createBinanceOrderResponse(251, "NEW"));
    MockNetwork::instance().setResponse("orderId=251", createBinanceOrderResponse(251, "NEW"));

    BinanceDealService service("test.binance.com", "api_key", "secret_key", "ws.binance.com");
    test_private_access::setBinanceStreamStatus(service, StreamStatus::CONNECTED);
    std::stop_source cancellationStopSource;

    std::future<OrderInfo> fillWait =
        std::async(std::launch::async, [&service]() { return service.waitUntilOrderFilled("BTCUSDT", "251"); });
    ASSERT_TRUE(MockNetwork::instance().waitForRequestCount("/api/v3/order?", 1, 1s));

    OrderQuery query;
    query.symbol = "BTCUSDT";
    query.orderId = "251";
    std::future<OrderInfo> cancellation =
        std::async(std::launch::async,
                   [&service, &query, stopToken = cancellationStopSource.get_token()]()
                   { return service.cancelOrderAndWaitUntilTerminal(query, stopToken); });
    ASSERT_TRUE(MockNetwork::instance().waitForRequestCount("/api/v3/order?", 3, 1s));

    cancellationStopSource.request_stop();
    ASSERT_EQ(cancellation.wait_for(1s), std::future_status::ready);
    EXPECT_THROW(cancellation.get(), OrderWaitInterrupted);
    EXPECT_EQ(fillWait.wait_for(25ms), std::future_status::timeout);
    EXPECT_EQ(service.getUserStreamStatus(), StreamStatus::CONNECTED);

    test_private_access::dispatchBinanceUserStreamMessage(service, createBinanceOrderMessage(251, "FILLED"));
    ASSERT_EQ(fillWait.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(fillWait.get().status, "FILLED");
}

TEST(DealServiceConcurrencyTest, BybitInterruptedConfirmationLeavesSameOrderFillWaitActive)
{
    MockNetwork::instance().reset();
    MockNetwork::instance().setResponse("/v5/market/time",
                                        R"({"retCode":0,"retMsg":"OK","result":{"timeSecond":"1779052073"}})");
    MockNetwork::instance().setResponse("orderId=INTERRUPTED_SHARED",
                                        createBybitOrderResponse("INTERRUPTED_SHARED", "New"));
    MockNetwork::instance().setResponse("orderId=INTERRUPTED_SHARED",
                                        createBybitOrderResponse("INTERRUPTED_SHARED", "New"));
    MockNetwork::instance().setResponse("/v5/order/cancel",
                                        R"({"retCode":0,"retMsg":"OK","result":{"orderId":"INTERRUPTED_SHARED"}})");

    BybitDealService service("test.bybit.com", "api_key", "secret_key", "ws.bybit.com");
    test_private_access::setBybitStreamStatus(service, StreamStatus::CONNECTED);
    std::stop_source cancellationStopSource;

    std::future<OrderInfo> fillWait =
        std::async(std::launch::async,
                   [&service]() { return service.waitUntilOrderFilled("BTCUSDT", "INTERRUPTED_SHARED"); });
    ASSERT_TRUE(MockNetwork::instance().waitForRequestCount("/v5/order/realtime", 1, 1s));

    OrderQuery query;
    query.symbol = "BTCUSDT";
    query.orderId = "INTERRUPTED_SHARED";
    std::future<OrderInfo> cancellation =
        std::async(std::launch::async,
                   [&service, &query, stopToken = cancellationStopSource.get_token()]()
                   { return service.cancelOrderAndWaitUntilTerminal(query, stopToken); });
    ASSERT_TRUE(MockNetwork::instance().waitForRequestCount("/v5/order/realtime", 2, 1s));
    ASSERT_TRUE(MockNetwork::instance().waitForRequestCount("/v5/order/cancel", 1, 1s));

    cancellationStopSource.request_stop();
    ASSERT_EQ(cancellation.wait_for(1s), std::future_status::ready);
    EXPECT_THROW(cancellation.get(), OrderWaitInterrupted);
    EXPECT_EQ(fillWait.wait_for(25ms), std::future_status::timeout);
    EXPECT_EQ(service.getUserStreamStatus(), StreamStatus::CONNECTED);

    test_private_access::dispatchBybitUserStreamMessage(
        service,
        createBybitOrderMessage("INTERRUPTED_SHARED", "INTERRUPTED_SHARED_CLIENT", "Filled"));
    ASSERT_EQ(fillWait.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(fillWait.get().status, "Filled");
}

TEST(DealServiceConcurrencyTest, ConfirmedCancellationHonorsPreRequestedStopTokensBeforeNetworkAccess)
{
    MockNetwork::instance().reset();
    BinanceDealService binance("test.binance.com", "api_key", "secret_key", "ws.binance.com");
    BybitDealService bybit("test.bybit.com", "api_key", "secret_key", "ws.bybit.com");
    std::stop_source stopSource;
    stopSource.request_stop();

    OrderQuery query;
    query.symbol = "BTCUSDT";
    query.orderId = "STOPPED";

    EXPECT_THROW(binance.cancelOrderAndWaitUntilTerminal(query, stopSource.get_token()), OrderWaitInterrupted);
    EXPECT_THROW(bybit.cancelOrderAndWaitUntilTerminal(query, stopSource.get_token()), OrderWaitInterrupted);
    EXPECT_TRUE(MockNetwork::instance().getRequests().empty());
}

TEST(DealServiceConcurrencyTest, BinanceInterruptedOrderWaitIsIsolatedAndCleansRegistration)
{
    MockNetwork::instance().reset();
    MockNetwork::instance().setResponse("/api/v3/time", R"({"serverTime":1779052073334})");
    MockNetwork::instance().setResponse("orderId=301", createBinanceOrderResponse(301, "NEW"));
    MockNetwork::instance().setResponse("orderId=301", createBinanceOrderResponse(301, "FILLED"));
    MockNetwork::instance().setResponse("orderId=302", createBinanceOrderResponse(302, "NEW"));

    BinanceDealService service("test.binance.com", "api_key", "secret_key", "ws.binance.com");
    test_private_access::setBinanceStreamStatus(service, StreamStatus::CONNECTED);
    std::stop_source interruptedSource;
    std::stop_source survivingSource;

    std::future<OrderInfo> interrupted =
        std::async(std::launch::async,
                   [&service, stopToken = interruptedSource.get_token()]()
                   { return service.waitUntilOrderFilled("BTCUSDT", "301", stopToken); });
    std::future<OrderInfo> surviving = std::async(std::launch::async,
                                                  [&service, stopToken = survivingSource.get_token()]() {
                                                      return service.waitUntilOrderFilled("BTCUSDT", "302", stopToken);
                                                  });

    if (!MockNetwork::instance().waitForRequestCount("/api/v3/order?", 2, 1s))
    {
        interruptedSource.request_stop();
        survivingSource.request_stop();
        test_private_access::setBinanceStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for Binance order reconciliation requests";
    }

    interruptedSource.request_stop();
    if (interrupted.wait_for(1s) != std::future_status::ready)
    {
        survivingSource.request_stop();
        test_private_access::setBinanceStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for the interrupted Binance order wait";
    }
    EXPECT_THROW(interrupted.get(), OrderWaitInterrupted);
    EXPECT_EQ(surviving.wait_for(25ms), std::future_status::timeout);

    test_private_access::dispatchBinanceUserStreamMessage(service, createBinanceOrderMessage(302, "FILLED"));
    survivingSource.request_stop();
    if (surviving.wait_for(1s) != std::future_status::ready)
    {
        test_private_access::setBinanceStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for the surviving Binance order wait";
    }
    EXPECT_EQ(surviving.get().orderId, "302");
    EXPECT_EQ(service.getUserStreamStatus(), StreamStatus::CONNECTED);

    const OrderInfo retriedOrder = service.waitUntilOrderFilled("BTCUSDT", "301");
    EXPECT_EQ(retriedOrder.status, "FILLED");
    EXPECT_EQ(countRequestsContaining("/api/v3/order?"), 3u);
}

TEST(DealServiceConcurrencyTest, BybitInterruptedOrderWaitIsIsolatedAndCleansRegistration)
{
    MockNetwork::instance().reset();
    MockNetwork::instance().setResponse("/v5/market/time",
                                        R"({"retCode":0,"retMsg":"OK","result":{"timeSecond":"1779052073"}})");
    MockNetwork::instance().setResponse("orderId=INTERRUPTED", createBybitOrderResponse("INTERRUPTED", "New"));
    MockNetwork::instance().setResponse("orderId=INTERRUPTED", createBybitOrderResponse("INTERRUPTED", "Filled"));
    MockNetwork::instance().setResponse("orderId=SURVIVING", createBybitOrderResponse("SURVIVING", "New"));

    BybitDealService service("test.bybit.com", "api_key", "secret_key", "ws.bybit.com");
    test_private_access::setBybitStreamStatus(service, StreamStatus::CONNECTED);
    std::stop_source interruptedSource;
    std::stop_source survivingSource;

    std::future<OrderInfo> interrupted =
        std::async(std::launch::async,
                   [&service, stopToken = interruptedSource.get_token()]()
                   { return service.waitUntilOrderFilled("BTCUSDT", "INTERRUPTED", stopToken); });
    std::future<OrderInfo> surviving =
        std::async(std::launch::async,
                   [&service, stopToken = survivingSource.get_token()]()
                   { return service.waitUntilOrderFilled("BTCUSDT", "SURVIVING", stopToken); });

    if (!MockNetwork::instance().waitForRequestCount("/v5/order/realtime", 2, 1s))
    {
        interruptedSource.request_stop();
        survivingSource.request_stop();
        test_private_access::setBybitStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for Bybit order reconciliation requests";
    }

    interruptedSource.request_stop();
    if (interrupted.wait_for(1s) != std::future_status::ready)
    {
        survivingSource.request_stop();
        test_private_access::setBybitStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for the interrupted Bybit order wait";
    }
    EXPECT_THROW(interrupted.get(), OrderWaitInterrupted);
    EXPECT_EQ(surviving.wait_for(25ms), std::future_status::timeout);

    test_private_access::dispatchBybitUserStreamMessage(
        service,
        createBybitOrderMessage("SURVIVING", "SURVIVING_CLIENT", "Filled"));
    survivingSource.request_stop();
    if (surviving.wait_for(1s) != std::future_status::ready)
    {
        test_private_access::setBybitStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for the surviving Bybit order wait";
    }
    EXPECT_EQ(surviving.get().orderId, "SURVIVING");
    EXPECT_EQ(service.getUserStreamStatus(), StreamStatus::CONNECTED);

    const OrderInfo retriedOrder = service.waitUntilOrderFilled("BTCUSDT", "INTERRUPTED");
    EXPECT_EQ(retriedOrder.status, "Filled");
    EXPECT_EQ(countRequestsContaining("/v5/order/realtime"), 3u);
}

TEST(DealServiceConcurrencyTest, BinanceInterruptedOcoWaitCleansRegistrationsAndTerminalStateWins)
{
    MockNetwork::instance().reset();
    MockNetwork::instance().setResponse("/api/v3/time", R"({"serverTime":1779052073334})");
    MockNetwork::instance().setResponse("orderId=401", createBinanceOrderResponse(401, "NEW"));
    MockNetwork::instance().setResponse("orderId=401", createBinanceOrderResponse(401, "NEW"));
    MockNetwork::instance().setResponse("orderId=402", createBinanceOrderResponse(402, "NEW"));
    MockNetwork::instance().setResponse("orderId=402", createBinanceOrderResponse(402, "NEW"));

    BinanceDealService service("test.binance.com", "api_key", "secret_key", "ws.binance.com");
    test_private_access::setBinanceStreamStatus(service, StreamStatus::CONNECTED);
    const OcoInfo ocoInfo = createOcoInfo("BINANCE_GROUP", "401", "402");
    std::stop_source interruptedSource;
    std::future<OcoWaitResult> interrupted =
        std::async(std::launch::async,
                   [&service, &ocoInfo, stopToken = interruptedSource.get_token()]()
                   { return service.waitUntilOcoOrderFilled(ocoInfo, stopToken); });

    if (!MockNetwork::instance().waitForRequestCount("/api/v3/order?", 2, 1s))
    {
        interruptedSource.request_stop();
        test_private_access::setBinanceStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for Binance OCO reconciliation requests";
    }
    interruptedSource.request_stop();
    if (interrupted.wait_for(1s) != std::future_status::ready)
    {
        test_private_access::setBinanceStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for the interrupted Binance OCO wait";
    }
    EXPECT_THROW(interrupted.get(), OrderWaitInterrupted);
    EXPECT_EQ(service.getUserStreamStatus(), StreamStatus::CONNECTED);
    EXPECT_EQ(countRequestsContaining("/api/v3/orderList"), 0u);

    std::stop_source completedSource;
    std::future<OcoWaitResult> completed = std::async(std::launch::async,
                                                      [&service, &ocoInfo, stopToken = completedSource.get_token()]()
                                                      { return service.waitUntilOcoOrderFilled(ocoInfo, stopToken); });
    if (!MockNetwork::instance().waitForRequestCount("/api/v3/order?", 4, 1s))
    {
        completedSource.request_stop();
        test_private_access::setBinanceStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for the retried Binance OCO reconciliation requests";
    }

    test_private_access::dispatchBinanceUserStreamMessage(service, createBinanceOrderMessage(401, "FILLED"));
    test_private_access::dispatchBinanceUserStreamMessage(service, createBinanceOrderMessage(402, "CANCELED"));
    completedSource.request_stop();
    if (completed.wait_for(1s) != std::future_status::ready)
    {
        test_private_access::setBinanceStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for the completed Binance OCO wait";
    }
    const OcoWaitResult result = completed.get();
    EXPECT_EQ(result.filledOrder.orderId, "401");
    EXPECT_EQ(result.siblingTerminalOrder.orderId, "402");
    EXPECT_EQ(service.getUserStreamStatus(), StreamStatus::CONNECTED);
    EXPECT_EQ(countRequestsContaining("/api/v3/orderList"), 0u);
}

TEST(DealServiceConcurrencyTest, BybitInterruptedOcoWaitCleansRegistrationsAndTerminalStateWins)
{
    MockNetwork::instance().reset();
    MockNetwork::instance().setResponse("/v5/market/time",
                                        R"({"retCode":0,"retMsg":"OK","result":{"timeSecond":"1779052073"}})");
    MockNetwork::instance().setResponse("orderId=TP_ID", createBybitOrderResponse("TP_ID", "New"));
    MockNetwork::instance().setResponse("orderId=TP_ID", createBybitOrderResponse("TP_ID", "New"));
    MockNetwork::instance().setResponse("orderId=SL_ID", createBybitOrderResponse("SL_ID", "New"));
    MockNetwork::instance().setResponse("orderId=SL_ID", createBybitOrderResponse("SL_ID", "New"));

    BybitDealService service("test.bybit.com", "api_key", "secret_key", "ws.bybit.com");
    test_private_access::setBybitStreamStatus(service, StreamStatus::CONNECTED);
    const OcoInfo ocoInfo = createOcoInfo("BYBIT_GROUP", "TP_ID", "SL_ID");
    std::stop_source interruptedSource;
    std::future<OcoWaitResult> interrupted =
        std::async(std::launch::async,
                   [&service, &ocoInfo, stopToken = interruptedSource.get_token()]()
                   { return service.waitUntilOcoOrderFilled(ocoInfo, stopToken); });

    if (!MockNetwork::instance().waitForRequestCount("/v5/order/realtime", 2, 1s))
    {
        interruptedSource.request_stop();
        test_private_access::setBybitStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for Bybit OCO reconciliation requests";
    }
    interruptedSource.request_stop();
    if (interrupted.wait_for(1s) != std::future_status::ready)
    {
        test_private_access::setBybitStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for the interrupted Bybit OCO wait";
    }
    EXPECT_THROW(interrupted.get(), OrderWaitInterrupted);
    EXPECT_EQ(service.getUserStreamStatus(), StreamStatus::CONNECTED);
    EXPECT_EQ(countRequestsContaining("/v5/order/cancel"), 0u);

    std::stop_source completedSource;
    std::future<OcoWaitResult> completed = std::async(std::launch::async,
                                                      [&service, &ocoInfo, stopToken = completedSource.get_token()]()
                                                      { return service.waitUntilOcoOrderFilled(ocoInfo, stopToken); });
    if (!MockNetwork::instance().waitForRequestCount("/v5/order/realtime", 4, 1s))
    {
        completedSource.request_stop();
        test_private_access::setBybitStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for the retried Bybit OCO reconciliation requests";
    }

    test_private_access::dispatchBybitUserStreamMessage(service,
                                                        createBybitOrderMessage("TP_ID", "BYBIT_GROUP_TP", "Filled"));
    test_private_access::dispatchBybitUserStreamMessage(
        service,
        createBybitOrderMessage("SL_ID", "BYBIT_GROUP_SL", "Cancelled"));
    completedSource.request_stop();
    if (completed.wait_for(1s) != std::future_status::ready)
    {
        test_private_access::setBybitStreamStatus(service, StreamStatus::ERROR);
        FAIL() << "Timed out waiting for the completed Bybit OCO wait";
    }
    const OcoWaitResult result = completed.get();
    EXPECT_EQ(result.filledOrder.orderId, "TP_ID");
    EXPECT_EQ(result.siblingTerminalOrder.orderId, "SL_ID");
    EXPECT_EQ(service.getUserStreamStatus(), StreamStatus::CONNECTED);
    EXPECT_EQ(countRequestsContaining("/v5/order/cancel"), 0u);
}

TEST(DealServiceConcurrencyTest, CachesAndBalancesSupportSimultaneousAccess)
{
    MockNetwork::instance().reset();
    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", getBinanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/v5/market/instruments-info", getBybitSymbolInfoResponse());

    BinanceDealService binance("test.binance.com", "api_key", "secret_key", "ws.binance.com");
    BybitDealService bybit("test.bybit.com", "api_key", "secret_key", "ws.bybit.com");
    std::vector<std::future<SymbolInfo>> symbols;
    std::vector<std::future<void>> balances;
    for (int index = 0; index < 8; ++index)
    {
        symbols.push_back(std::async(std::launch::async, [&binance]() { return binance.getSymbolInfo("BTCUSDT"); }));
        symbols.push_back(std::async(std::launch::async, [&bybit]() { return bybit.getSymbolInfo("BTCUSDT"); }));
        balances.push_back(
            std::async(std::launch::async,
                       [&binance, index]()
                       {
                           test_private_access::dispatchBinanceUserStreamMessage(
                               binance,
                               "{\"event\":{\"e\":\"outboundAccountPosition\",\"B\":[{\"a\":\"USDT\",\"f\":\"" +
                                   std::to_string(index + 1) + "\",\"l\":\"0\"}]}}");
                           static_cast<void>(binance.getBalances());
                       }));
        balances.push_back(
            std::async(std::launch::async,
                       [&bybit, index]()
                       {
                           test_private_access::dispatchBybitUserStreamMessage(
                               bybit,
                               "{\"topic\":\"wallet\",\"data\":[{\"coin\":[{\"coin\":\"USDT\",\"walletBalance\":\"" +
                                   std::to_string(index + 1) + "\",\"locked\":\"0\"}]}]}");
                           static_cast<void>(bybit.getBalances());
                       }));
    }

    for (std::future<SymbolInfo> &result : symbols)
    {
        EXPECT_EQ(result.get().symbol, "BTCUSDT");
    }
    for (std::future<void> &result : balances)
    {
        result.get();
    }

    const std::size_t binanceRequestCount = countRequestsContaining("/api/v3/exchangeInfo");
    const std::size_t bybitRequestCount = countRequestsContaining("/v5/market/instruments-info");
    EXPECT_EQ(binance.getSymbolInfo("BTCUSDT").symbol, "BTCUSDT");
    EXPECT_EQ(bybit.getSymbolInfo("BTCUSDT").symbol, "BTCUSDT");
    EXPECT_EQ(countRequestsContaining("/api/v3/exchangeInfo"), binanceRequestCount);
    EXPECT_EQ(countRequestsContaining("/v5/market/instruments-info"), bybitRequestCount);
    EXPECT_TRUE(binance.getBalance("USDT").has_value());
    EXPECT_TRUE(bybit.getBalance("USDT").has_value());
}

TEST(DealServiceConcurrencyTest, ConcurrentStreamStopsJoinEachRunnerOnce)
{
    TestableBinanceDealService binance;
    binance.startDormantRunner();
    EXPECT_NO_THROW(stopConcurrently(binance));

    TestableBybitDealService bybit;
    bybit.startDormantRunner();
    EXPECT_NO_THROW(stopConcurrently(bybit));
}

TEST(DealServiceConcurrencyTest, StreamStatusTransitionsNotifyEachServiceObserver)
{
    BinanceDealService binance("test.binance.com", "api_key", "secret_key", "ws.binance.com");
    BybitDealService bybit("test.bybit.com", "api_key", "secret_key", "ws.bybit.com");
    int binanceNotifications = 0;
    int bybitNotifications = 0;
    binance.setUserStreamEventHandlers({{}, [&binanceNotifications]() { ++binanceNotifications; }});
    bybit.setUserStreamEventHandlers({{}, [&bybitNotifications]() { ++bybitNotifications; }});

    test_private_access::setBinanceStreamStatus(binance, StreamStatus::CONNECTING);
    test_private_access::setBinanceStreamStatus(binance, StreamStatus::CONNECTED);
    test_private_access::setBybitStreamStatus(bybit, StreamStatus::CONNECTING);
    test_private_access::setBybitStreamStatus(bybit, StreamStatus::CONNECTED);

    EXPECT_EQ(binanceNotifications, 2);
    EXPECT_EQ(bybitNotifications, 2);
}

TEST(DealServiceConcurrencyTest, BybitRejectsConcurrentOcoGroupReuse)
{
    MockNetwork::instance().reset();
    MockNetwork::instance().setResponse("/v5/market/time",
                                        R"({"retCode":0,"retMsg":"OK","result":{"timeSecond":"1779052073"}})");
    MockNetwork::instance().setResponse("/v5/market/instruments-info", getBybitSymbolInfoResponse());
    MockNetwork::instance().setResponse(
        "/v5/market/price-limit",
        R"({"retCode":0,"retMsg":"OK","result":{"symbol":"BTCUSDT","buyLmt":"100000","sellLmt":"10000"}})");
    MockNetwork::instance().setResponse(
        "/v5/market/tickers",
        R"({"retCode":0,"retMsg":"OK","result":{"list":[{"symbol":"BTCUSDT","lastPrice":"55000"}]}})");
    MockNetwork::instance().setResponse(
        "/v5/order/create",
        R"({"retCode":0,"retMsg":"OK","result":{"orderId":"TP_ID","orderLinkId":"GROUP_TP"}})");
    MockNetwork::instance().setResponse(
        "/v5/order/create",
        R"({"retCode":0,"retMsg":"OK","result":{"orderId":"SL_ID","orderLinkId":"GROUP_SL"}})");

    BybitDealService service("test.bybit.com", "api_key", "secret_key", "ws.bybit.com");
    test_private_access::setBybitStreamStatus(service, StreamStatus::CONNECTED);
    test_private_access::dispatchBybitUserStreamMessage(
        service,
        R"({"topic":"wallet","data":[{"coin":[{"coin":"BTC","walletBalance":"1","locked":"0"}]}]})");

    PlaceOcoRequest request;
    request.symbol = "BTCUSDT";
    request.side = OrderOperation::SELL;
    request.quantity = DecimalConverter::parseDecimal("0.5");
    request.price = DecimalConverter::parseDecimal("60000");
    request.stopPrice = DecimalConverter::parseDecimal("55000");
    request.listClientOrderId = "GROUP";

    std::latch ready(2);
    std::latch start(1);
    auto place = [&service, &request, &ready, &start]()
    {
        ready.count_down();
        start.wait();
        try
        {
            static_cast<void>(service.placeOco(request));
            return PlacementResult{true, ""};
        }
        catch (const std::exception &exception)
        {
            return PlacementResult{false, exception.what()};
        }
    };

    std::future<PlacementResult> first = std::async(std::launch::async, place);
    std::future<PlacementResult> second = std::async(std::launch::async, place);
    ready.wait();
    start.count_down();

    const PlacementResult firstResult = first.get();
    const PlacementResult secondResult = second.get();
    EXPECT_NE(firstResult.succeeded, secondResult.succeeded);
    const std::string &error = firstResult.succeeded ? secondResult.error : firstResult.error;
    EXPECT_NE(error.find("already active"), std::string::npos);
    EXPECT_EQ(countRequestsContaining("/v5/order/create"), 2u);
}
