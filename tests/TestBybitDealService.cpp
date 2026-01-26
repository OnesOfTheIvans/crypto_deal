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