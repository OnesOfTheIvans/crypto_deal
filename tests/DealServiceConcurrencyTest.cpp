#include "../src/DealService/binance/BinanceDealService.hpp"
#include "../src/DealService/bybit/BybitDealService.hpp"
#include "../src/DealService/common/DecimalConverter.hpp"
#include "MockHttpRequest.hpp"
#include "PrivateAccess.hpp"

#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <latch>
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

    std::string createBinanceFilledMessage(long long orderId)
    {
        return R"({"event":{"e":"executionReport","s":"BTCUSDT","S":"BUY","o":"LIMIT",)"
               R"("f":"GTC","q":"0.1","p":"50000","X":"FILLED","i":)" +
               std::to_string(orderId) + R"(,"z":"0.1","Z":"5000"}})";
    }

    std::string createBybitOrderResponse(const std::string &orderId, const std::string &status)
    {
        return R"({"retCode":0,"retMsg":"OK","result":{"list":[{"symbol":"BTCUSDT","orderId":")" + orderId +
               R"(","orderStatus":")" + status +
               R"(","price":"50000","qty":"0.1","cumExecQty":"0","cumExecValue":"0"}]}})";
    }

    std::string createBybitFilledMessage(const std::string &orderId)
    {
        return R"({"topic":"order","data":[{"category":"spot","symbol":"BTCUSDT","orderId":")" + orderId +
               R"(","side":"Buy","orderType":"Limit","orderStatus":"Filled","price":"50000",)"
               R"("qty":"0.1","cumExecQty":"0.1","cumExecValue":"5000","leavesQty":"0",)"
               R"("avgPrice":"50000"}]})";
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

    test_private_access::dispatchBinanceUserStreamMessage(service, createBinanceFilledMessage(202));
    test_private_access::dispatchBinanceUserStreamMessage(service, createBinanceFilledMessage(101));

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

    test_private_access::dispatchBybitUserStreamMessage(service, createBybitFilledMessage("SECOND"));
    test_private_access::dispatchBybitUserStreamMessage(service, createBybitFilledMessage("FIRST"));

    EXPECT_EQ(first.get().orderId, "FIRST");
    EXPECT_EQ(second.get().orderId, "SECOND");
    EXPECT_EQ(countRequestsContaining("/v5/market/time"), 1u);
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
