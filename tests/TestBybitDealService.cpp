#include "../src/DealService/bybit/BybitDealService.hpp"
#include "../src/DealService/common/OrderInfo.hpp"
#include "MockHttpRequest.hpp"
#include <gtest/gtest.h>

class BybitDealServiceTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        MockNetwork::instance().reset();
    }

    BybitDealService createService()
    {
        return BybitDealService("test.bybit.com", "api_key", "secret_key", "ws.bybit.com");
    }
};

TEST_F(BybitDealServiceTest, PlaceLimitOrder_Success)
{
    auto service = createService();

    std::string responseJson = R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {
            "orderId": "132141",
            "orderLinkId": "client_id_1"
        }
    })";

    MockNetwork::instance().setResponse("/v5/order/create", responseJson);

    PlaceOrderRequest req;
    req.symbol = "BTCUSDT";
    req.side = "BUY";
    req.type = "LIMIT";
    req.quantity = 0.5;
    req.price = 45000.0;
    req.timeInForce = "GTC";

    OrderInfo info = service.placeOrder(req);

    EXPECT_EQ(info.symbol, "BTCUSDT");
    EXPECT_EQ(info.orderId, "132141");
    EXPECT_EQ(info.status, "New");
    EXPECT_EQ(info.side, "Buy");
    EXPECT_DOUBLE_EQ(info.origQty, 0.5);
    EXPECT_DOUBLE_EQ(info.price, 45000.0);
}

TEST_F(BybitDealServiceTest, CancelOrder_Success)
{
    auto service = createService();

    std::string responseJson = R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {
            "orderId": "132141",
            "orderLinkId": "client_id_1"
        }
    })";

    MockNetwork::instance().setResponse("/v5/order/cancel", responseJson);

    OrderQuery query;
    query.symbol = "BTCUSDT";
    query.orderId = "132141";

    OrderInfo info = service.cancelOrder(query);

    EXPECT_EQ(info.symbol, "BTCUSDT");
    EXPECT_EQ(info.orderId, "132141");
    EXPECT_EQ(info.status, "Cancelled");
}

TEST_F(BybitDealServiceTest, GetSymbolInfo_Success)
{
    auto service = createService();

    std::string responseJson = R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {
            "list": [{
                "symbol": "BTCUSDT",
                "status": "Trading",
                "baseCoin": "BTC",
                "quoteCoin": "USDT",
                "priceFilter": {
                    "tickSize": "0.01",
                    "minPrice": "0.1",
                    "maxPrice": "1000000"
                },
                "lotSizeFilter": {
                    "qtyStep": "0.0001",
                    "minOrderQty": "0.0001",
                    "maxOrderQty": "1000"
                }
            }]
        }
    })";

    MockNetwork::instance().setResponse("/v5/market/instruments-info", responseJson);

    SymbolInfo info = service.getSymbolInfo("BTCUSDT");

    EXPECT_EQ(info.symbol, "BTCUSDT");
    EXPECT_EQ(info.baseAsset, "BTC");
    EXPECT_EQ(info.quoteAsset, "USDT");
    EXPECT_DOUBLE_EQ(info.tickSize, 0.01);
    EXPECT_DOUBLE_EQ(info.stepSize, 0.0001);
}

TEST_F(BybitDealServiceTest, PlaceOco_Success)
{
    auto service = createService();

    std::string responseJson = R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {
            "orderId": "OCO_LEG_ID",
            "orderLinkId": "OCO_LEG_LINK_ID"
        }
    })";

    MockNetwork::instance().setResponse("/v5/order/create", responseJson);

    PlaceOcoRequest req;
    req.symbol = "BTCUSDT";
    req.side = "BUY";
    req.quantity = 0.5;
    req.price = 45000.0;
    req.stopPrice = 40000.0;
    req.stopLimitPrice = 39900.0;

    OcoInfo info = service.placeOco(req);

    EXPECT_EQ(info.orders.size(), 2);
}

TEST_F(BybitDealServiceTest, PlaceOrder_InvalidInput)
{
    auto service = createService();
    PlaceOrderRequest req;
    EXPECT_THROW(service.placeOrder(req), std::runtime_error);

    req.symbol = "BTCUSDT";
    req.quantity = -1.0;
    EXPECT_THROW(service.placeOrder(req), std::runtime_error);
}

TEST_F(BybitDealServiceTest, PlaceOrder_ApiError)
{
    auto service = createService();
    std::string errorJson = R"({
        "retCode": 10001,
        "retMsg": "Params Error"
    })";
    MockNetwork::instance().setResponse("/v5/order/create", errorJson);

    PlaceOrderRequest req;
    req.symbol = "BTCUSDT";
    req.side = "BUY";
    req.type = "LIMIT";
    req.quantity = 0.1;
    req.price = 50000;

    EXPECT_THROW(service.placeOrder(req), std::runtime_error);
}

TEST_F(BybitDealServiceTest, CancelOrder_ApiError)
{
    auto service = createService();
    std::string errorJson = R"({
        "retCode": 20002,
        "retMsg": "Order not found"
    })";
    MockNetwork::instance().setResponse("/v5/order/cancel", errorJson);

    OrderQuery q;
    q.symbol = "BTCUSDT";
    q.orderId = "999";
    EXPECT_THROW(service.cancelOrder(q), std::runtime_error);
}

TEST_F(BybitDealServiceTest, PlaceOco_ValidationError)
{
    auto service = createService();
    PlaceOcoRequest req;
    req.symbol = "BTCUSDT";
    req.side = "HOLD"; // Invalid
    req.quantity = 1;
    req.price = 100;
    req.stopPrice = 90;

    EXPECT_THROW(service.placeOco(req), std::runtime_error);

    req.side = "BUY";
    req.stopLimitPrice = -50.0;
    EXPECT_THROW(service.placeOco(req), std::runtime_error);
}

TEST_F(BybitDealServiceTest, PlaceOco_PartialFailure_Rollback)
{
    auto service = createService();

    std::string tpSuccess = R"({
        "retCode": 0,
        "result": { "orderId": "TP_ID", "orderLinkId": "TP_LINK" }
    })";

    std::string slFailure = R"({
        "retCode": 10002,
        "retMsg": "Invalid Price"
    })";

    std::string rollbackSuccess = R"({
        "retCode": 0,
        "result": { "orderId": "TP_ID" }
    })";

    MockNetwork::instance().setResponse("/v5/order/create", tpSuccess);       // TP
    MockNetwork::instance().setResponse("/v5/order/create", slFailure);       // SL
    MockNetwork::instance().setResponse("/v5/order/cancel", rollbackSuccess); // Rollback

    PlaceOcoRequest req;
    req.symbol = "BTCUSDT";
    req.side = "SELL";
    req.quantity = 0.5;
    req.price = 60000;
    req.stopPrice = 55000;

    try
    {
        service.placeOco(req);
        FAIL() << "Expected placeOco to throw";
    }
    catch (const std::runtime_error &e)
    {
        std::string msg = e.what();
        EXPECT_TRUE(msg.find("failed to place SL leg") != std::string::npos);
        EXPECT_TRUE(msg.find("Rollback failed") == std::string::npos);
    }
}