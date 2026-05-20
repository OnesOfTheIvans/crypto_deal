#include "../src/DealService/bybit/BybitDealService.hpp"
#include "../src/DealService/common/DecimalConverter.hpp"
#include "../src/DealService/common/OrderInfo.hpp"
#include "MockHttpRequest.hpp"
#include "PrivateAccess.hpp"
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

namespace {
    std::string bybitSymbolInfoResponse()
    {
        return R"({
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
                    "maxOrderQty": "1000",
                    "minOrderAmt": "10"
                }
            }]
        }
    })";
    }

    std::string bybitWalletBalanceResponse()
    {
        return R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {
            "list": [{
                "coin": [
                    {
                        "coin": "USDT",
                        "walletBalance": "100000",
                        "availableToWithdraw": "100000",
                        "locked": "0"
                    },
                    {
                        "coin": "BTC",
                        "walletBalance": "1",
                        "availableToWithdraw": "1",
                        "locked": "0"
                    }
                ]
            }]
        }
    })";
    }

    std::string bybitWalletStreamMessage(const std::string &usdtWallet = "100000",
                                         const std::string &usdtLocked = "0",
                                         const std::string &btcWallet = "1",
                                         const std::string &btcLocked = "0")
    {
        return "{\"topic\":\"wallet\",\"data\":[{\"coin\":[{\"coin\":\"USDT\",\"walletBalance\":\"" + usdtWallet +
               "\",\"locked\":\"" + usdtLocked + "\"},{\"coin\":\"BTC\",\"walletBalance\":\"" + btcWallet +
               "\",\"locked\":\"" + btcLocked + "\"}]}]}";
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
} // namespace

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
    MockNetwork::instance().setResponse("/v5/market/instruments-info", bybitSymbolInfoResponse());
    test_private_access::dispatchBybitUserStreamMessage(service, bybitWalletStreamMessage());

    PlaceOrderRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::BUY;
    req.type = OrderType::LIMIT;
    req.quantity = DecimalConverter::parseDecimal("0.5");
    req.price = DecimalConverter::parseDecimal("45000.0");
    req.timeInForce = "GTC";

    OrderInfo info = service.placeOrder(req);

    EXPECT_EQ(info.symbol, "BTCUSDT");
    EXPECT_EQ(info.orderId, "132141");
    EXPECT_EQ(info.status, "New");
    EXPECT_EQ(info.side, "Buy");
    EXPECT_EQ(info.origQty, DecimalConverter::parseDecimal("0.5"));
    EXPECT_EQ(info.price, DecimalConverter::parseDecimal("45000.0"));
    EXPECT_EQ(countRequestsContaining("/v5/account/wallet-balance"), 0u);
}

TEST_F(BybitDealServiceTest, PlaceLimitOrder_RejectsBelowMinNotional)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/v5/market/instruments-info", bybitSymbolInfoResponse());

    PlaceOrderRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::BUY;
    req.type = OrderType::LIMIT;
    req.quantity = DecimalConverter::parseDecimal("0.0001");
    req.price = DecimalConverter::parseDecimal("50000.0");
    req.timeInForce = "GTC";

    EXPECT_THROW(service.placeOrder(req), std::runtime_error);
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
    EXPECT_EQ(info.tickSize, DecimalConverter::parseDecimal("0.01"));
    EXPECT_EQ(info.stepSize, DecimalConverter::parseDecimal("0.0001"));
}

TEST_F(BybitDealServiceTest, CeilQuantityToStep)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/v5/market/instruments-info", bybitSymbolInfoResponse());

    EXPECT_EQ(service.ceilQuantityToStep("BTCUSDT", DecimalConverter::parseDecimal("0.00019")),
              DecimalConverter::parseDecimal("0.0002"));
}

TEST_F(BybitDealServiceTest, WalletStreamMessageUpdatesBalanceCache)
{
    auto service = createService();

    test_private_access::dispatchBybitUserStreamMessage(service, bybitWalletStreamMessage("1000", "25", "2", "0.5"));

    const auto usdt = service.getBalance("USDT");
    ASSERT_TRUE(usdt.has_value());
    EXPECT_EQ(usdt->free, DecimalConverter::parseDecimal("975"));
    EXPECT_EQ(usdt->locked, DecimalConverter::parseDecimal("25"));

    const auto btc = service.getBalance("BTC");
    ASSERT_TRUE(btc.has_value());
    EXPECT_EQ(btc->free, DecimalConverter::parseDecimal("1.5"));
    EXPECT_EQ(btc->locked, DecimalConverter::parseDecimal("0.5"));
}

TEST_F(BybitDealServiceTest, WalletStreamMessageOverwritesCachedBalance)
{
    auto service = createService();

    test_private_access::dispatchBybitUserStreamMessage(service, bybitWalletStreamMessage("1000", "0", "1", "0"));
    test_private_access::dispatchBybitUserStreamMessage(service,
                                                        bybitWalletStreamMessage("500", "100", "0.25", "0.05"));

    const auto usdt = service.getBalance("USDT");
    ASSERT_TRUE(usdt.has_value());
    EXPECT_EQ(usdt->free, DecimalConverter::parseDecimal("400"));
    EXPECT_EQ(usdt->locked, DecimalConverter::parseDecimal("100"));

    const auto btc = service.getBalance("BTC");
    ASSERT_TRUE(btc.has_value());
    EXPECT_EQ(btc->free, DecimalConverter::parseDecimal("0.20"));
    EXPECT_EQ(btc->locked, DecimalConverter::parseDecimal("0.05"));
}

TEST_F(BybitDealServiceTest, GetBalancesRest_BlankOptionalFields)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/v5/market/time", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {
            "timeSecond": "1776977716"
        }
    })");

    MockNetwork::instance().setResponse("/v5/account/wallet-balance", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {
            "list": [{
                "coin": [{
                    "coin": "USDT",
                    "walletBalance": "10.5",
                    "availableToWithdraw": "",
                    "availableToTrade": "",
                    "locked": "0.5"
                }]
            }]
        }
    })");

    const auto balances = service.getBalancesRest();

    ASSERT_FALSE(balances.empty());
    const auto balance = service.getBalance("USDT");
    ASSERT_TRUE(balance.has_value());
    EXPECT_EQ(balance->free, DecimalConverter::parseDecimal("10.0"));
    EXPECT_EQ(balance->locked, DecimalConverter::parseDecimal("0.5"));
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
    MockNetwork::instance().setResponse("/v5/market/instruments-info", bybitSymbolInfoResponse());
    test_private_access::dispatchBybitUserStreamMessage(service, bybitWalletStreamMessage());

    PlaceOcoRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::BUY;
    req.quantity = DecimalConverter::parseDecimal("0.5");
    req.price = DecimalConverter::parseDecimal("45000.0");
    req.stopPrice = DecimalConverter::parseDecimal("40000.0");
    req.stopLimitPrice = DecimalConverter::parseDecimal("39900.0");

    OcoInfo info = service.placeOco(req);

    EXPECT_EQ(info.orders.size(), 2);
}

TEST_F(BybitDealServiceTest, PlaceOrder_InvalidInput)
{
    auto service = createService();
    PlaceOrderRequest req;
    req.side = OrderOperation::BUY;
    req.type = OrderType::LIMIT;
    EXPECT_THROW(service.placeOrder(req), std::runtime_error);

    req.symbol = "BTCUSDT";
    req.quantity = DecimalConverter::parseDecimal("-1.0");
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
    req.side = OrderOperation::BUY;
    req.type = OrderType::LIMIT;
    req.quantity = DecimalConverter::parseDecimal("0.1");
    req.price = DecimalConverter::parseDecimal("50000");

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
    req.side = OrderOperation::BUY;
    req.quantity = 0;
    req.price = DecimalConverter::parseDecimal("100");
    req.stopPrice = DecimalConverter::parseDecimal("90");

    EXPECT_THROW(service.placeOco(req), std::runtime_error);

    req.quantity = DecimalConverter::parseDecimal("1");
    req.stopLimitPrice = DecimalConverter::parseDecimal("-50.0");
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
    MockNetwork::instance().setResponse("/v5/market/instruments-info", bybitSymbolInfoResponse());
    test_private_access::dispatchBybitUserStreamMessage(service, bybitWalletStreamMessage());

    PlaceOcoRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::SELL;
    req.quantity = DecimalConverter::parseDecimal("0.5");
    req.price = DecimalConverter::parseDecimal("60000");
    req.stopPrice = DecimalConverter::parseDecimal("55000");

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

TEST_F(BybitDealServiceTest, MarketBuyOrder_RejectsInsufficientQuoteBalance)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/v5/market/instruments-info", bybitSymbolInfoResponse());
    MockNetwork::instance().setResponse("/v5/market/tickers", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": { "list": [{ "symbol": "BTCUSDT", "lastPrice": "50000" }] }
    })");

    EXPECT_THROW(service.buyCrypto("BTC", "USDT", DecimalConverter::parseDecimal("2")), std::runtime_error);
    EXPECT_EQ(countRequestsContaining("/v5/account/wallet-balance"), 0u);
}

TEST_F(BybitDealServiceTest, MarketSellOrder_RejectsInsufficientBaseBalance)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/v5/market/instruments-info", bybitSymbolInfoResponse());
    MockNetwork::instance().setResponse("/v5/market/tickers", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": { "list": [{ "symbol": "BTCUSDT", "lastPrice": "50000" }] }
    })");

    EXPECT_THROW(service.sellCrypto("BTC", "USDT", DecimalConverter::parseDecimal("0.01")), std::runtime_error);
    EXPECT_EQ(countRequestsContaining("/v5/account/wallet-balance"), 0u);
}

TEST_F(BybitDealServiceTest, PlaceOrder_InsufficientBuyBalance)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/v5/market/instruments-info", bybitSymbolInfoResponse());
    PlaceOrderRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::BUY;
    req.type = OrderType::LIMIT;
    req.quantity = DecimalConverter::parseDecimal("1");
    req.price = DecimalConverter::parseDecimal("50000");
    req.timeInForce = "GTC";

    EXPECT_THROW(service.placeOrder(req), std::runtime_error);
    EXPECT_EQ(countRequestsContaining("/v5/account/wallet-balance"), 0u);
}

TEST_F(BybitDealServiceTest, GetOrder_ParsesDetailedResponse)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/v5/order/realtime", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {
            "list": [{
                "symbol": "BTCUSDT",
                "orderId": "ORDER_ID",
                "orderLinkId": "CLIENT_ID",
                "side": "Buy",
                "orderType": "Limit",
                "timeInForce": "GTC",
                "orderStatus": "PartiallyFilled",
                "price": "50000",
                "qty": "0.3",
                "cumExecQty": "0.1",
                "cumExecValue": "5000",
                "leavesQty": "0.2",
                "avgPrice": "50000",
                "createdTime": "1779052073334",
                "updatedTime": "1779052073335"
            }]
        }
    })");

    OrderQuery query;
    query.symbol = "BTCUSDT";
    query.orderId = "ORDER_ID";

    OrderInfo info = service.getOrder(query);

    EXPECT_EQ(info.symbol, "BTCUSDT");
    EXPECT_EQ(info.orderId, "ORDER_ID");
    EXPECT_EQ(info.clientOrderId, "CLIENT_ID");
    EXPECT_EQ(info.status, "PartiallyFilled");
    EXPECT_EQ(info.executedQty, DecimalConverter::parseDecimal("0.1"));
    EXPECT_EQ(info.leavesQty, DecimalConverter::parseDecimal("0.2"));
    EXPECT_EQ(info.updatedTimeMs, 1779052073335LL);
}

TEST_F(BybitDealServiceTest, GetOrder_EmptyListThrows)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/v5/order/realtime", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": { "list": [] }
    })");

    OrderQuery query;
    query.symbol = "BTCUSDT";
    query.orderId = "ORDER_ID";

    EXPECT_THROW(service.getOrder(query), std::runtime_error);
}

TEST_F(BybitDealServiceTest, GetBalancesRest_FallsBackFromUnifiedToSpot)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/v5/account/wallet-balance?accountType=UNIFIED", R"({
        "retCode": 10001,
        "retMsg": "Account type invalid"
    })");
    MockNetwork::instance().setResponse("/v5/account/wallet-balance?accountType=SPOT", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {
            "list": [{
                "coin": [{
                    "coin": "USDT",
                    "walletBalance": "20",
                    "availableToWithdraw": "15",
                    "locked": "5"
                }]
            }]
        }
    })");

    const auto balances = service.getBalancesRest();

    ASSERT_TRUE(balances.contains("USDT"));
    EXPECT_EQ(balances.at("USDT").free, DecimalConverter::parseDecimal("15"));
    EXPECT_EQ(balances.at("USDT").locked, DecimalConverter::parseDecimal("5"));
}

TEST_F(BybitDealServiceTest, GetBalancesRest_RefreshesWhenCacheAlreadyPopulated)
{
    auto service = createService();

    test_private_access::dispatchBybitUserStreamMessage(service, bybitWalletStreamMessage("100", "0", "1", "0"));
    MockNetwork::instance().setResponse("/v5/account/wallet-balance?accountType=UNIFIED", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {
            "list": [{
                "coin": [{
                    "coin": "USDT",
                    "walletBalance": "20",
                    "availableToWithdraw": "18",
                    "locked": "2"
                }]
            }]
        }
    })");
    MockNetwork::instance().setResponse("/v5/account/wallet-balance?accountType=SPOT", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": { "list": [] }
    })");

    const auto balances = service.getBalancesRest();

    ASSERT_TRUE(balances.contains("USDT"));
    EXPECT_EQ(balances.at("USDT").free, DecimalConverter::parseDecimal("18"));
    EXPECT_EQ(balances.at("USDT").locked, DecimalConverter::parseDecimal("2"));
    EXPECT_EQ(countRequestsContaining("/v5/account/wallet-balance"), 2u);
}

TEST_F(BybitDealServiceTest, GetSymbolInfo_UsesBasePrecisionFallbackForStep)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/v5/market/instruments-info", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {
            "list": [{
                "symbol": "ETHUSDT",
                "status": "Trading",
                "baseCoin": "ETH",
                "quoteCoin": "USDT",
                "priceFilter": {
                    "tickSize": "0.01",
                    "minPrice": "0.1",
                    "maxPrice": "1000000"
                },
                "lotSizeFilter": {
                    "qtyStep": "",
                    "basePrecision": "0.000001",
                    "minOrderQty": "0.000001",
                    "maxOrderQty": "1000",
                    "minOrderAmt": "5"
                }
            }]
        }
    })");

    SymbolInfo info = service.getSymbolInfo("ETHUSDT");

    EXPECT_EQ(info.symbol, "ETHUSDT");
    EXPECT_EQ(info.stepSize, DecimalConverter::parseDecimal("0.000001"));
    EXPECT_EQ(info.minNotional, DecimalConverter::parseDecimal("5"));
}

TEST_F(BybitDealServiceTest, CancelAllOpenOrders_SuccessAndApiError)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/v5/order/cancel-all", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": {}
    })");
    MockNetwork::instance().setResponse("/v5/order/cancel-all", R"({
        "retCode": 10001,
        "retMsg": "Params Error"
    })");

    EXPECT_TRUE(service.cancelAllOpenOrders("BTCUSDT", "spot"));
    EXPECT_EQ(MockNetwork::instance().lastRequest().method, "POST");
    EXPECT_NE(MockNetwork::instance().lastRequest().body.find(R"("symbol":"BTCUSDT")"), std::string::npos);

    EXPECT_THROW(service.cancelAllOpenOrders("BTCUSDT", "spot"), std::runtime_error);
}

TEST_F(BybitDealServiceTest, CancelOco_Success)
{
    auto service = createService();

    MockNetwork::instance().setResponse("/v5/order/create", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": { "orderId": "TP_ID", "orderLinkId": "GROUP_TP" }
    })");
    MockNetwork::instance().setResponse("/v5/order/create", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": { "orderId": "SL_ID", "orderLinkId": "GROUP_SL" }
    })");
    MockNetwork::instance().setResponse("/v5/order/cancel", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": { "orderId": "TP_ID", "orderLinkId": "GROUP_TP" }
    })");
    MockNetwork::instance().setResponse("/v5/order/cancel", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": { "orderId": "SL_ID", "orderLinkId": "GROUP_SL" }
    })");
    MockNetwork::instance().setResponse("/v5/market/instruments-info", bybitSymbolInfoResponse());
    MockNetwork::instance().setResponse("/v5/market/tickers", R"({
        "retCode": 0,
        "retMsg": "OK",
        "result": { "list": [{ "symbol": "BTCUSDT", "lastPrice": "55000" }] }
    })");
    test_private_access::dispatchBybitUserStreamMessage(service, bybitWalletStreamMessage());

    PlaceOcoRequest req;
    req.symbol = "BTCUSDT";
    req.side = OrderOperation::SELL;
    req.quantity = DecimalConverter::parseDecimal("0.5");
    req.price = DecimalConverter::parseDecimal("60000");
    req.stopPrice = DecimalConverter::parseDecimal("55000");
    req.listClientOrderId = "GROUP";

    OcoInfo placed = service.placeOco(req);

    OrderListQuery cancelQuery;
    cancelQuery.symbol = "BTCUSDT";
    cancelQuery.listClientOrderId = placed.listClientOrderId;

    OcoInfo cancelled = service.cancelOco(cancelQuery);

    EXPECT_EQ(cancelled.listClientOrderId, "GROUP");
    ASSERT_EQ(cancelled.orders.size(), 2u);
    EXPECT_EQ(cancelled.orders[0].status, "Cancelled");
    EXPECT_EQ(cancelled.orders[1].status, "Cancelled");
}

TEST_F(BybitDealServiceTest, CancelOco_UnknownGroupThrows)
{
    auto service = createService();

    OrderListQuery query;
    query.symbol = "BTCUSDT";
    query.listClientOrderId = "missing";

    EXPECT_THROW(service.cancelOco(query), std::runtime_error);
}
