#include "../src/DealService/binance/BinanceDealService.hpp"
#include "../src/DealService/common/DecimalConverter.hpp"
#include "../src/DealService/common/OrderInfo.hpp"
#include "MockHttpRequest.hpp"
#include <gtest/gtest.h>

class BinanceDealServiceTest : public ::testing::Test
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
} // namespace

TEST_F(BinanceDealServiceTest, PlaceLimitOrder_Success)
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

    MockNetwork::instance().setResponse("/api/v3/order/test", "{}");
    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/api/v3/order", responseJson);

    PlaceOrderRequest req;
    req.symbol = "BTCUSDT";
    req.side = "BUY";
    req.type = "LIMIT";
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

TEST_F(BinanceDealServiceTest, MarketBuyOrder_Success)
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
        "origQty": "0.00010000",
        "executedQty": "0.00010000",
        "origQuoteOrderQty": "0.00000000",
        "cummulativeQuoteQty": "7.82535400",
        "status": "FILLED",
        "timeInForce": "GTC",
        "type": "MARKET",
        "side": "BUY",
        "workingTime": 1779052073333,
        "fills": [{
            "price": "78253.54000000",
            "qty": "0.00010000",
            "commission": "0.00000000",
            "commissionAsset": "BTC",
            "tradeId": 1515535
        }],
        "selfTradePreventionMode": "EXPIRE_MAKER"
    })";

    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", binanceSymbolInfoResponse());
    MockNetwork::instance().setResponse("/api/v3/ticker/price?symbol=BTCUSDT", tickerResponse);
    MockNetwork::instance().setResponse("/api/v3/order", responseJson);

    OrderInfo info = service.buyCrypto("BTC", "USDT", DecimalConverter::parseDecimal("0.00009"));

    EXPECT_EQ(info.symbol, "BTCUSDT");
    EXPECT_EQ(info.orderId, "4458118");
    EXPECT_EQ(info.status, "FILLED");
    EXPECT_EQ(info.side, "BUY");
    EXPECT_EQ(info.type, "MARKET");
    EXPECT_EQ(info.executedQty, DecimalConverter::parseDecimal("0.0001"));
}

TEST_F(BinanceDealServiceTest, CancelOrder_Success)
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

    MockNetwork::instance().setResponse("/api/v3/order", responseJson);

    OrderQuery query;
    query.symbol = "BTCUSDT";
    query.orderId = "12345";

    OrderInfo info = service.cancelOrder(query);

    EXPECT_EQ(info.symbol, "BTCUSDT");
    EXPECT_EQ(info.orderId, "12345");
    EXPECT_EQ(info.status, "CANCELED");
}

TEST_F(BinanceDealServiceTest, GetSymbolInfo_Success)
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

TEST_F(BinanceDealServiceTest, PlaceOrder_InvalidInput)
{
    auto service = createService();
    PlaceOrderRequest req;
    EXPECT_THROW(service.placeOrder(req), std::runtime_error); // Empty symbol

    req.symbol = "ETHUSDT";
    req.quantity = 0;
    EXPECT_THROW(service.placeOrder(req), std::runtime_error);
}

TEST_F(BinanceDealServiceTest, PlaceOrder_ApiError)
{
    auto service = createService();
    std::string errorJson = R"({
        "code": -1102,
        "msg": "Mandatory parameter 'timeInForce' was not sent, was empty/null, or malformed."
    })";

    MockNetwork::instance().setResponse("/api/v3/order", errorJson);

    PlaceOrderRequest req;
    req.symbol = "ETHUSDT";
    req.side = "BUY";
    req.type = "LIMIT";
    req.quantity = DecimalConverter::parseDecimal("1.0");
    req.price = DecimalConverter::parseDecimal("2000");
    req.timeInForce = "GTC";

    EXPECT_THROW(service.placeOrder(req), std::runtime_error);
}

TEST_F(BinanceDealServiceTest, GetSymbolInfo_NotFound)
{
    auto service = createService();
    std::string emptyResponse = "{}";
    MockNetwork::instance().setResponse("/api/v3/exchangeInfo", emptyResponse);

    EXPECT_THROW(service.getSymbolInfo("UNKNOWN"), std::runtime_error);
}
