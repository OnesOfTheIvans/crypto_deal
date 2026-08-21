#include "../src/DealService/binance/BinanceDealService.hpp"
#include "../src/DealService/common/DecimalConverter.hpp"
#include "../src/DealService/common/domain/OrderInfo.hpp"
#include "MockHttpRequest.hpp"
#include "PrivateAccess.hpp"
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <thread>

class BinanceDealServiceIntegrationTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        MockNetwork::instance().reset();
    }

    BinanceDealService createService()
    {
        return BinanceDealService("test.binance.com", "api_key", "secret_key", "ws.binance.com");
    }
};

namespace {
    std::string binanceSymbolInfoResponse()
    {
        return R"({
        "symbols": [{
            "symbol": "BTCUSDT",
            "status": "TRADING",
            "baseAsset": "BTC",
            "baseAssetPrecision": 8,
            "quoteAsset": "USDT",
            "quotePrecision": 8,
            "filters": [
                {
                    "filterType": "PRICE_FILTER",
                    "minPrice": "0.01000000",
                    "maxPrice": "1000000.00000000",
                    "tickSize": "0.01000000"
                },
                {
                    "filterType": "LOT_SIZE",
                    "minQty": "0.00010000",
                    "maxQty": "9000.00000000",
                    "stepSize": "0.00010000"
                },
                {
                    "filterType": "MIN_NOTIONAL",
                    "minNotional": "10.00000000"
                }
            ]
        }]
    })";
    }

    void setBinanceServerTimeResponse()
    {
        MockNetwork::instance().setResponse("/api/v3/time", R"({"serverTime": 1779052073334})");
    }

    std::string createBinanceOrderResponse(long long orderId, const std::string &status)
    {
        return R"({
            "symbol": "BTCUSDT",
            "orderId": )" +
               std::to_string(orderId) +
               R"(,
            "side": "SELL",
            "type": "LIMIT_MAKER",
            "status": ")" +
               status +
               R"(",
            "price": "50000.00000000",
            "origQty": "0.50000000",
            "executedQty": "0.50000000",
            "cumulativeQuoteQty": "25000.00000000"
        })";
    }

    std::string binanceBalanceStreamMessage(const std::string &usdtFree = "100000",
                                            const std::string &usdtLocked = "0",
                                            const std::string &btcFree = "1",
                                            const std::string &btcLocked = "0")
    {
        return "{\"event\":{\"e\":\"outboundAccountPosition\",\"B\":[{\"a\":\"USDT\",\"f\":\"" + usdtFree +
               "\",\"l\":\"" + usdtLocked + "\"},{\"a\":\"BTC\",\"f\":\"" + btcFree + "\",\"l\":\"" + btcLocked +
               "\"}]}}";
    }

    void seedBinanceBalances(BinanceDealService &service,
                             const std::string &usdtFree = "100000",
                             const std::string &usdtLocked = "0",
                             const std::string &btcFree = "1",
                             const std::string &btcLocked = "0")
    {
        test_private_access::dispatchBinanceUserStreamMessage(
            service,
            binanceBalanceStreamMessage(usdtFree, usdtLocked, btcFree, btcLocked));
    }

    std::size_t countRequestsContaining(const std::string &targetPart)
    {
        std::size_t count = 0;
        for (const auto &request : MockNetwork::instance().getRequests())
        {
            if (request.target.find(targetPart) != std::string::npos)
            {
                ++count;
            }
        }
        return count;
    }

    void expectMarketOrderRequest(const MockNetwork::RecordedRequest &request, const std::string &side)
    {
        EXPECT_EQ(request.method, "POST");
        EXPECT_NE(request.target.find("/api/v3/order?"), std::string::npos);
        EXPECT_NE(request.target.find("symbol=BTCUSDT"), std::string::npos);
        EXPECT_NE(request.target.find("side=" + side), std::string::npos);
        EXPECT_NE(request.target.find("type=MARKET"), std::string::npos);
        EXPECT_NE(request.target.find("quantity=0.0002"), std::string::npos);
        EXPECT_NE(request.target.find("newOrderRespType=RESULT"), std::string::npos);
        EXPECT_NE(request.headers.find("X-MBX-APIKEY"), request.headers.end());
    }
} // namespace

TEST_F(BinanceDealServiceIntegrationTest, PlaceLimitOrder_Success)
{
    auto service = createService();

    std::string responseJson = R"({
        "symbol": "BTCUSDT",
        "orderId": 2831923,
        "orderListId": -1,
        "clientOrderId": "iso_cancel_test",
        "transactTime": 1507725176595,
        "price": "50000.00000000",
        "origQty": "1.00000000",
        "executedQty": "0.00000000",
        "cummulativeQuoteQty": "0.00000000",
        "status": "NEW",
        "timeInForce": "GTC",
        "type": "LIMIT",
        "side": "BUY",
        "workingTime": 1507725176595,
        "selfTradePreventionMode": "NONE"
    })";

    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/order/test", "{}");
    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/api/v3/order", responseJson);
    seedBinanceBalances(service);

    PlaceOrderRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::BUY;
    req.type = OrderType::LIMIT;
    req.quantity = DecimalConverter::parseDecimal("1.0");
    req.price = DecimalConverter::parseDecimal("50000.0");
    req.timeInForce = "GTC";

    OrderInfo info = service.placeOrder(req);

    EXPECT_EQ(info.symbol, "BTCUSDT");
    EXPECT_EQ(info.orderId, "2831923");
    EXPECT_EQ(info.status, "NEW");
    EXPECT_EQ(info.side, "BUY");
    EXPECT_EQ(info.type, "LIMIT");
    EXPECT_EQ(info.price, DecimalConverter::parseDecimal("50000.0"));
    EXPECT_EQ(info.origQty, DecimalConverter::parseDecimal("1.0"));
}

TEST_F(BinanceDealServiceIntegrationTest, MarketBuyOrder_Success)
{
    auto service = createService();

    std::string tickerResponse = R"({
        "symbol": "BTCUSDT",
        "price": "78253.54000000"
    })";

    std::string responseJson = R"({
        "symbol": "BTCUSDT",
        "orderId": 4458118,
        "orderListId": -1,
        "clientOrderId": "4VaNZmqo4CpVWQNBx9TBWT",
        "transactTime": 1779052073333,
        "price": "0.00000000",
        "origQty": "0.00020000",
        "executedQty": "0.00020000",
        "origQuoteOrderQty": "0.00000000",
        "cummulativeQuoteQty": "15.65070800",
        "status": "FILLED",
        "timeInForce": "GTC",
        "type": "MARKET",
        "side": "BUY",
        "workingTime": 1779052073333,
        "fills": [{
            "price": "78253.54000000",
            "qty": "0.00020000",
            "commission": "0.00000000",
            "commissionAsset": "BTC",
            "tradeId": 1515535
        }],
        "selfTradePreventionMode": "EXPIRE_MAKER"
    })";

    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/api/v3/ticker/price?symbol=BTCUSDT", tickerResponse);
    MockNetwork::instance().setResponse("/api/v3/order", responseJson);
    seedBinanceBalances(service);

    OrderInfo info = service.buyCrypto("BTC", "USDT", DecimalConverter::parseDecimal("0.0002"));

    EXPECT_EQ(info.symbol, "BTCUSDT");
    EXPECT_EQ(info.orderId, "4458118");
    EXPECT_EQ(info.status, "FILLED");
    EXPECT_EQ(info.side, "BUY");
    EXPECT_EQ(info.type, "MARKET");
    EXPECT_EQ(info.executedQty, DecimalConverter::parseDecimal("0.0002"));

    expectMarketOrderRequest(MockNetwork::instance().lastRequest(), "BUY");
}

TEST_F(BinanceDealServiceIntegrationTest, MarketSellOrder_Success)
{
    auto service = createService();

    std::string tickerResponse = R"({
        "symbol": "BTCUSDT",
        "price": "78253.54000000"
    })";

    std::string responseJson = R"({
        "symbol": "BTCUSDT",
        "orderId": 4458119,
        "clientOrderId": "sell-client-id",
        "transactTime": 1779052073333,
        "price": "0.00000000",
        "origQty": "0.00020000",
        "executedQty": "0.00020000",
        "cummulativeQuoteQty": "15.65070800",
        "status": "FILLED",
        "type": "MARKET",
        "side": "SELL"
    })";

    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/api/v3/ticker/price?symbol=BTCUSDT", tickerResponse);
    MockNetwork::instance().setResponse("/api/v3/order", responseJson);
    seedBinanceBalances(service);

    OrderInfo info = service.sellCrypto("BTC", "USDT", DecimalConverter::parseDecimal("0.0002"));

    EXPECT_EQ(info.symbol, "BTCUSDT");
    EXPECT_EQ(info.orderId, "4458119");
    EXPECT_EQ(info.status, "FILLED");
    EXPECT_EQ(info.side, "SELL");
    EXPECT_EQ(info.type, "MARKET");
    EXPECT_EQ(info.executedQty, DecimalConverter::parseDecimal("0.0002"));

    expectMarketOrderRequest(MockNetwork::instance().lastRequest(), "SELL");
}

TEST_F(BinanceDealServiceIntegrationTest, MarketBuyOrder_RejectsBelowMinNotional)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/api/v3/ticker/price?symbol=BTCUSDT", R"({
        "symbol": "BTCUSDT",
        "price": "50000.00000000"
    })");

    EXPECT_THROW(service.buyCrypto("BTC", "USDT", DecimalConverter::parseDecimal("0.0001")), std::runtime_error);
}

TEST_F(BinanceDealServiceIntegrationTest, PlaceLimitOrder_RejectsInsufficientQuoteBalance)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    seedBinanceBalances(service, "10", "0", "1", "0");

    PlaceOrderRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::BUY;
    req.type = OrderType::LIMIT;
    req.quantity = DecimalConverter::parseDecimal("0.5");
    req.price = DecimalConverter::parseDecimal("45000");
    req.timeInForce = "GTC";

    EXPECT_THROW(service.placeOrder(req), std::runtime_error);
    EXPECT_EQ(countRequestsContaining("/api/v3/order?"), 0u);
}

TEST_F(BinanceDealServiceIntegrationTest, MarketBuyOrder_RejectsInsufficientQuoteBalance)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/api/v3/ticker/price?symbol=BTCUSDT", R"({
        "symbol": "BTCUSDT",
        "price": "50000.00000000"
    })");
    seedBinanceBalances(service, "10", "0", "1", "0");

    EXPECT_THROW(service.buyCrypto("BTC", "USDT", DecimalConverter::parseDecimal("0.001")), std::runtime_error);
    EXPECT_EQ(countRequestsContaining("/api/v3/order?"), 0u);
}

TEST_F(BinanceDealServiceIntegrationTest, MarketSellOrder_RejectsInsufficientBaseBalance)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/api/v3/ticker/price?symbol=BTCUSDT", R"({
        "symbol": "BTCUSDT",
        "price": "50000.00000000"
    })");
    seedBinanceBalances(service, "100000", "0", "0.0001", "0");

    EXPECT_THROW(service.sellCrypto("BTC", "USDT", DecimalConverter::parseDecimal("0.0002")), std::runtime_error);
    EXPECT_EQ(countRequestsContaining("/api/v3/order?"), 0u);
}

TEST_F(BinanceDealServiceIntegrationTest, CancelOrder_Success)
{
    auto service = createService();

    std::string responseJson = R"({
        "symbol": "BTCUSDT",
        "origClientOrderId": "my_order_1",
        "orderId": 12345,
        "orderListId": -1,
        "clientOrderId": "cancel_req_1",
        "price": "55000.00000000",
        "origQty": "0.50000000",
        "executedQty": "0.00000000",
        "cummulativeQuoteQty": "0.00000000",
        "status": "CANCELED",
        "timeInForce": "GTC",
        "type": "LIMIT",
        "side": "SELL"
    })";

    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/order", responseJson);

    OrderQuery query;
    query.symbol = "BTCUSDT";
    query.orderId = "12345";

    OrderInfo info = service.cancelOrder(query);

    EXPECT_EQ(info.symbol, "BTCUSDT");
    EXPECT_EQ(info.orderId, "12345");
    EXPECT_EQ(info.status, "CANCELED");
}

TEST_F(BinanceDealServiceIntegrationTest, GetSymbolInfo_Success)
{
    auto service = createService();

    std::string responseJson = R"({
        "symbols": [{
            "symbol": "ETHUSDT",
            "status": "TRADING",
            "baseAsset": "ETH",
            "baseAssetPrecision": 8,
            "quoteAsset": "USDT",
            "quotePrecision": 8,
            "filters": [
                {
                    "filterType": "PRICE_FILTER",
                    "minPrice": "0.01000000",
                    "maxPrice": "1000000.00000000",
                    "tickSize": "0.01000000"
                },
                {
                    "filterType": "LOT_SIZE",
                    "minQty": "0.00010000",
                    "maxQty": "9000.00000000",
                    "stepSize": "0.00010000"
                },
                {
                    "filterType": "MIN_NOTIONAL",
                    "minNotional": "10.00000000"
                }
            ]
        }]
    })";

    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", responseJson);

    SymbolInfo info = service.getSymbolInfo("ETHUSDT");

    EXPECT_EQ(info.symbol, "ETHUSDT");
    EXPECT_EQ(info.baseAsset, "ETH");
    EXPECT_EQ(info.quoteAsset, "USDT");
    EXPECT_EQ(info.minPrice, DecimalConverter::parseDecimal("0.01"));
    EXPECT_EQ(info.minQty, DecimalConverter::parseDecimal("0.0001"));
    EXPECT_EQ(info.minQty, DecimalConverter::parseDecimal("0.0001"));
    EXPECT_EQ(info.minNotional, DecimalConverter::parseDecimal("10.0"));
}

TEST_F(BinanceDealServiceIntegrationTest, CeilQuantityToStep)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());

    EXPECT_EQ(service.ceilQuantityToStep("BTCUSDT", DecimalConverter::parseDecimal("0.00019")),
              DecimalConverter::parseDecimal("0.0002"));
}

TEST_F(BinanceDealServiceIntegrationTest, PlaceOrder_InvalidInput)
{
    auto service = createService();
    PlaceOrderRequest req;
    req.side = OrderOperation::BUY;
    req.type = OrderType::LIMIT;
    EXPECT_THROW(service.placeOrder(req), std::runtime_error); // Empty symbol

    req.symbol = "ETHUSDT";
    req.quantity = 0;
    EXPECT_THROW(service.placeOrder(req), std::runtime_error);
}

TEST_F(BinanceDealServiceIntegrationTest, PlaceOrder_ApiError)
{
    auto service = createService();
    std::string errorJson = R"({
        "code": -1102,
        "msg": "Mandatory parameter 'timeInForce' was not sent, was empty/null, or malformed."
    })";

    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/api/v3/order", errorJson);
    seedBinanceBalances(service);

    PlaceOrderRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::BUY;
    req.type = OrderType::LIMIT;
    req.quantity = DecimalConverter::parseDecimal("1.0");
    req.price = DecimalConverter::parseDecimal("2000");
    req.timeInForce = "GTC";

    EXPECT_THROW(service.placeOrder(req), std::runtime_error);
}

TEST_F(BinanceDealServiceIntegrationTest, GetSymbolInfo_NotFound)
{
    auto service = createService();
    std::string emptyResponse = "{}";
    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", emptyResponse);

    EXPECT_THROW(service.getSymbolInfo("UNKNOWN"), std::runtime_error);
}

TEST_F(BinanceDealServiceIntegrationTest, MarketSellOrder_RejectsInvalidStep)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/api/v3/ticker/price?symbol=BTCUSDT", R"({
        "symbol": "BTCUSDT",
        "price": "78253.54000000"
    })");

    try
    {
        service.sellCrypto("BTC", "USDT", DecimalConverter::parseDecimal("0.00019"));
        FAIL() << "Expected sellCrypto to reject invalid step";
    }
    catch (const std::runtime_error &e)
    {
        std::string msg = e.what();
        EXPECT_NE(msg.find("quantity is not valid for step size"), std::string::npos);
        EXPECT_EQ(msg.find("0.0002"), std::string::npos);
    }
}

TEST_F(BinanceDealServiceIntegrationTest, GetOrder_ParsesDetailedResponse)
{
    auto service = createService();

    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/order", R"({
        "symbol": "BTCUSDT",
        "orderId": 12345,
        "clientOrderId": "client-123",
        "price": "50000.00000000",
        "origQty": "0.30000000",
        "executedQty": "0.10000000",
        "cumulativeQuoteQty": "5000.00000000",
        "status": "PARTIALLY_FILLED",
        "timeInForce": "GTC",
        "type": "LIMIT",
        "side": "BUY",
        "transactTime": 1779052073334
    })");

    OrderQuery query;
    query.symbol = "BTCUSDT";
    query.orderId = "12345";

    OrderInfo info = service.getOrder(query);

    EXPECT_EQ(info.symbol, "BTCUSDT");
    EXPECT_EQ(info.orderId, "12345");
    EXPECT_EQ(info.status, "PARTIALLY_FILLED");
    EXPECT_EQ(info.leavesQty, DecimalConverter::parseDecimal("0.2"));
    EXPECT_EQ(info.avgPrice, DecimalConverter::parseDecimal("50000"));
}

TEST_F(BinanceDealServiceIntegrationTest, GetBalancesRest_SeedsBalanceCache)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/api/v3/time", R"({"serverTime": 1779052073334})");
    MockNetwork::instance().setResponse("/api/v3/account", R"({
        "balances": [
            { "asset": "USDT", "free": "10.50000000", "locked": "0.25000000" },
            { "asset": "BTC", "free": "0.12500000", "locked": "0.00000000" }
        ]
    })");

    const auto balances = service.getBalancesRest();

    ASSERT_TRUE(balances.contains("USDT"));
    ASSERT_TRUE(balances.contains("BTC"));
    EXPECT_EQ(balances.at("USDT").free, DecimalConverter::parseDecimal("10.5"));
    EXPECT_EQ(balances.at("USDT").locked, DecimalConverter::parseDecimal("0.25"));
    EXPECT_EQ(service.getBalance("BTC")->free, DecimalConverter::parseDecimal("0.125"));
}

TEST_F(BinanceDealServiceIntegrationTest, StopUserStreamDoesNotLogWhenAlreadyStopped)
{
    auto service = createService();

    testing::internal::CaptureStdout();
    service.stopUserStream();
    const std::string output = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(output.empty());
}

TEST_F(BinanceDealServiceIntegrationTest, UserStreamAccountPositionUpdatesBalanceCache)
{
    auto service = createService();

    test_private_access::dispatchBinanceUserStreamMessage(service, R"({
        "event": {
            "e": "outboundAccountPosition",
            "B": [
                { "a": "USDT", "f": "123.45", "l": "6.78" },
                { "a": "BTC", "f": "0.25", "l": "0.05" }
            ]
        }
    })");

    const auto usdt = service.getBalance("USDT");
    ASSERT_TRUE(usdt.has_value());
    EXPECT_EQ(usdt->free, DecimalConverter::parseDecimal("123.45"));
    EXPECT_EQ(usdt->locked, DecimalConverter::parseDecimal("6.78"));

    const auto btc = service.getBalance("BTC");
    ASSERT_TRUE(btc.has_value());
    EXPECT_EQ(btc->free, DecimalConverter::parseDecimal("0.25"));
    EXPECT_EQ(btc->locked, DecimalConverter::parseDecimal("0.05"));
}

TEST_F(BinanceDealServiceIntegrationTest, UserStreamIgnoresNonBalanceEvent)
{
    auto service = createService();

    test_private_access::dispatchBinanceUserStreamMessage(service, R"({
        "event": {
            "e": "executionReport",
            "B": [
                { "a": "USDT", "f": "123.45", "l": "6.78" }
            ]
        }
    })");

    EXPECT_FALSE(service.getBalance("USDT").has_value());
}

TEST_F(BinanceDealServiceIntegrationTest, UserStreamExecutionReportPublishesCompleteOrderUpdate)
{
    auto service = createService();
    test_private_access::setBinanceStreamStatus(service, StreamStatus::CONNECTED);
    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/order", R"({
        "symbol": "BTCUSDT",
        "orderId": 123,
        "side": "BUY",
        "type": "LIMIT",
        "status": "NEW",
        "price": "50000.00000000",
        "origQty": "0.50000000",
        "executedQty": "0.00000000",
        "cumulativeQuoteQty": "0.00000000"
    })");

    std::future<OrderInfo> result =
        std::async(std::launch::async, [&service]() { return service.waitUntilOrderFilled("BTCUSDT", "123"); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    test_private_access::dispatchBinanceUserStreamMessage(service, R"({
        "event": {
            "e": "executionReport",
            "E": 1779052073335,
            "s": "BTCUSDT",
            "c": "client-order",
            "S": "BUY",
            "o": "LIMIT",
            "f": "GTC",
            "q": "0.50000000",
            "p": "50000.00000000",
            "x": "TRADE",
            "X": "FILLED",
            "r": "NONE",
            "i": 123,
            "z": "0.50000000",
            "Z": "25000.00000000",
            "T": 1779052073335,
            "O": 1779052073300
        }
    })");

    ASSERT_EQ(result.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    const OrderInfo update = result.get();
    EXPECT_EQ(update.status, "FILLED");
    EXPECT_EQ(update.clientOrderId, "client-order");
    EXPECT_EQ(update.executedQty, DecimalConverter::parseDecimal("0.5"));
    EXPECT_EQ(update.cumQuoteQty, DecimalConverter::parseDecimal("25000"));
    EXPECT_EQ(update.avgPrice, DecimalConverter::parseDecimal("50000"));
    EXPECT_EQ(countRequestsContaining("/api/v3/order?"), 1u);
}

TEST_F(BinanceDealServiceIntegrationTest, RejectedListStatusPublishesRejectedChildOrders)
{
    auto service = createService();
    test_private_access::setBinanceStreamStatus(service, StreamStatus::CONNECTED);
    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/order", R"({
        "symbol": "BTCUSDT",
        "orderId": 456,
        "side": "SELL",
        "type": "LIMIT_MAKER",
        "status": "NEW",
        "price": "50000.00000000",
        "origQty": "0.50000000",
        "executedQty": "0.00000000",
        "cumulativeQuoteQty": "0.00000000"
    })");

    std::future<OrderInfo> result =
        std::async(std::launch::async, [&service]() { return service.waitUntilOrderFilled("BTCUSDT", "456"); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    test_private_access::dispatchBinanceUserStreamMessage(service, R"({
        "event": {
            "e": "listStatus",
            "s": "BTCUSDT",
            "L": "REJECT",
            "r": "OCO_BAD_PRICES",
            "O": [
                { "s": "BTCUSDT", "i": 456, "c": "limit-leg" },
                { "s": "BTCUSDT", "i": 457, "c": "stop-leg" }
            ]
        }
    })");

    ASSERT_EQ(result.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    try
    {
        result.get();
        FAIL() << "Expected rejected Binance order wait to throw";
    }
    catch (const std::runtime_error &exception)
    {
        EXPECT_NE(std::string(exception.what()).find("REJECTED"), std::string::npos);
        EXPECT_NE(std::string(exception.what()).find("OCO_BAD_PRICES"), std::string::npos);
    }
}

TEST_F(BinanceDealServiceIntegrationTest, FailedWaitRemovesOrderRegistration)
{
    auto service = createService();
    test_private_access::setBinanceStreamStatus(service, StreamStatus::CONNECTED);
    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/order", createBinanceOrderResponse(800, "CANCELED"));
    MockNetwork::instance().setResponse("/api/v3/order", createBinanceOrderResponse(800, "FILLED"));

    EXPECT_THROW(service.waitUntilOrderFilled("BTCUSDT", "800"), std::runtime_error);

    const OrderInfo filledOrder = service.waitUntilOrderFilled("BTCUSDT", "800");
    EXPECT_EQ(filledOrder.status, "FILLED");
    EXPECT_EQ(countRequestsContaining("/api/v3/order?"), 2u);
}

TEST_F(BinanceDealServiceIntegrationTest, OcoWaitUsesNamedLegsAndReturnsFilledSibling)
{
    auto service = createService();
    test_private_access::setBinanceStreamStatus(service, StreamStatus::CONNECTED);
    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/order", createBinanceOrderResponse(810, "CANCELED"));
    MockNetwork::instance().setResponse("/api/v3/order", createBinanceOrderResponse(811, "FILLED"));

    OcoInfo ocoInfo;
    ocoInfo.orderListId = "81";
    ocoInfo.takeProfitOrder.symbol = "BTCUSDT";
    ocoInfo.takeProfitOrder.orderId = "810";
    ocoInfo.stopLossOrder.symbol = "BTCUSDT";
    ocoInfo.stopLossOrder.orderId = "811";

    const OrderInfo filledOrder = service.waitUntilOcoOrderFilled(ocoInfo);

    EXPECT_EQ(filledOrder.orderId, "811");
    EXPECT_EQ(filledOrder.status, "FILLED");
    EXPECT_EQ(countRequestsContaining("/api/v3/order?"), 2u);
}

TEST_F(BinanceDealServiceIntegrationTest, GetBalancesRest_ApiError)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/api/v3/time", R"({"serverTime": 1779052073334})");
    MockNetwork::instance().setResponse("/api/v3/account", R"({
        "code": -2015,
        "msg": "Invalid API-key"
    })");

    EXPECT_THROW(service.getBalancesRest(), std::runtime_error);
}

TEST_F(BinanceDealServiceIntegrationTest, CancelAllOpenOrders_IgnoresAlreadyGoneErrors)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/api/v3/time", R"({"serverTime": 1779052073334})");
    MockNetwork::instance().setResponse("/api/v3/openOrders", R"({
        "code": -2011,
        "msg": "Unknown order sent."
    })");

    EXPECT_NO_THROW(service.cancelAllOpenOrders("BTCUSDT", OrderCategory::SPOT));
    EXPECT_EQ(MockNetwork::instance().lastRequest().method, "DELETE");
}

TEST_F(BinanceDealServiceIntegrationTest, PlaceOco_Success)
{
    auto service = createService();

    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/api/v3/orderList/oco", R"({
        "orderListId": 777,
        "listClientOrderId": "oco-list-id",
        "transactionTime": 1779052073334,
        "orderReports": [
            {
                "symbol": "BTCUSDT",
                "orderId": 1,
                "clientOrderId": "limit-leg",
                "price": "90000.00000000",
                "origQty": "0.00020000",
                "executedQty": "0.00000000",
                "cummulativeQuoteQty": "0.00000000",
                "status": "NEW",
                "timeInForce": "GTC",
                "type": "LIMIT_MAKER",
                "side": "SELL",
                "transactTime": 1779052073334
            },
            {
                "symbol": "BTCUSDT",
                "orderId": 2,
                "clientOrderId": "stop-leg",
                "price": "59000.00000000",
                "origQty": "0.00020000",
                "executedQty": "0.00000000",
                "cummulativeQuoteQty": "0.00000000",
                "status": "NEW",
                "timeInForce": "GTC",
                "type": "STOP_LOSS_LIMIT",
                "side": "SELL",
                "transactTime": 1779052073334
            }
        ]
    })");
    seedBinanceBalances(service);

    PlaceOcoRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::SELL;
    req.quantity = DecimalConverter::parseDecimal("0.0002");
    req.price = DecimalConverter::parseDecimal("90000");
    req.stopPrice = DecimalConverter::parseDecimal("60000");
    req.stopLimitPrice = DecimalConverter::parseDecimal("59000");
    req.stopLimitTimeInForce = "GTC";
    req.listClientOrderId = "oco-list-id";

    OcoInfo info = service.placeOco(req);

    EXPECT_EQ(info.orderListId, "777");
    EXPECT_EQ(info.listClientOrderId, "oco-list-id");
    EXPECT_EQ(info.takeProfitOrder.orderId, "1");
    EXPECT_EQ(info.stopLossOrder.type, "STOP_LOSS_LIMIT");

    const auto &lastRequest = MockNetwork::instance().lastRequest();
    EXPECT_EQ(lastRequest.method, "POST");
    EXPECT_NE(lastRequest.target.find("quantity=0.0002"), std::string::npos);
    EXPECT_NE(lastRequest.target.find("belowTimeInForce=GTC"), std::string::npos);
}

TEST_F(BinanceDealServiceIntegrationTest, PlaceOco_RequiresOrderReportCoreFields)
{
    auto service = createService();

    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/api/v3/orderList/oco", R"({
        "orderListId": 777,
        "listClientOrderId": "oco-list-id",
        "transactionTime": 1779052073334,
        "orderReports": [
            {
                "orderId": 1,
                "clientOrderId": "limit-leg",
                "price": "90000.00000000",
                "origQty": "0.00020000",
                "executedQty": "0.00000000",
                "cummulativeQuoteQty": "0.00000000",
                "status": "NEW",
                "timeInForce": "GTC",
                "type": "LIMIT_MAKER",
                "side": "SELL",
                "transactTime": 1779052073334
            }
        ]
    })");
    seedBinanceBalances(service);

    PlaceOcoRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::SELL;
    req.quantity = DecimalConverter::parseDecimal("0.0002");
    req.price = DecimalConverter::parseDecimal("90000");
    req.stopPrice = DecimalConverter::parseDecimal("60000");
    req.stopLimitPrice = DecimalConverter::parseDecimal("59000");
    req.stopLimitTimeInForce = "GTC";

    EXPECT_THROW(service.placeOco(req), std::runtime_error);
}

TEST_F(BinanceDealServiceIntegrationTest, PlaceOco_RejectsInvalidStep)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());

    PlaceOcoRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::SELL;
    req.quantity = DecimalConverter::parseDecimal("0.00019");
    req.price = DecimalConverter::parseDecimal("90000");
    req.stopPrice = DecimalConverter::parseDecimal("60000");
    req.stopLimitPrice = DecimalConverter::parseDecimal("59000");
    req.stopLimitTimeInForce = "GTC";

    EXPECT_THROW(service.placeOco(req), std::runtime_error);
}

TEST_F(BinanceDealServiceIntegrationTest, PlaceOco_RejectsInsufficientBaseBalance)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    seedBinanceBalances(service, "100000", "0", "0.0001", "0");

    PlaceOcoRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::SELL;
    req.quantity = DecimalConverter::parseDecimal("0.0002");
    req.price = DecimalConverter::parseDecimal("90000");
    req.stopPrice = DecimalConverter::parseDecimal("60000");
    req.stopLimitPrice = DecimalConverter::parseDecimal("59000");
    req.stopLimitTimeInForce = "GTC";

    EXPECT_THROW(service.placeOco(req), std::runtime_error);
    EXPECT_EQ(countRequestsContaining("/api/v3/orderList/oco"), 0u);
}

TEST_F(BinanceDealServiceIntegrationTest, PlaceOco_RejectsInsufficientQuoteBalance)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    seedBinanceBalances(service, "10", "0", "1", "0");

    PlaceOcoRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::BUY;
    req.quantity = DecimalConverter::parseDecimal("0.0002");
    req.price = DecimalConverter::parseDecimal("90000");
    req.stopPrice = DecimalConverter::parseDecimal("60000");
    req.stopLimitPrice = DecimalConverter::parseDecimal("59000");
    req.stopLimitTimeInForce = "GTC";

    EXPECT_THROW(service.placeOco(req), std::runtime_error);
    EXPECT_EQ(countRequestsContaining("/api/v3/orderList/oco"), 0u);
}

TEST_F(BinanceDealServiceIntegrationTest, PlaceOco_RequiresStopLimitTimeInForce)
{
    auto service = createService();

    PlaceOcoRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::SELL;
    req.quantity = DecimalConverter::parseDecimal("0.0002");
    req.price = DecimalConverter::parseDecimal("90000");
    req.stopPrice = DecimalConverter::parseDecimal("60000");
    req.stopLimitPrice = DecimalConverter::parseDecimal("59000");

    EXPECT_THROW(service.placeOco(req), std::runtime_error);
}

TEST_F(BinanceDealServiceIntegrationTest, CancelOco_Success)
{
    auto service = createService();

    setBinanceServerTimeResponse();
    MockNetwork::instance().setResponse("/api/v3/orderList", R"({
        "orderListId": 777,
        "listClientOrderId": "oco-list-id",
        "transactionTime": 1779052073334,
        "orderReports": [
            {
                "symbol": "BTCUSDT",
                "orderId": 1,
                "clientOrderId": "limit-leg",
                "price": "90000.00000000",
                "origQty": "0.00020000",
                "executedQty": "0.00000000",
                "cummulativeQuoteQty": "0.00000000",
                "status": "CANCELED",
                "type": "LIMIT_MAKER",
                "side": "SELL",
                "transactTime": 1779052073334
            }
        ]
    })");

    OrderListQuery query;
    query.symbol = "BTCUSDT";
    query.listClientOrderId = "oco-list-id";

    EXPECT_NO_THROW(service.cancelOco(query));
    EXPECT_EQ(MockNetwork::instance().lastRequest().method, "DELETE");
}
