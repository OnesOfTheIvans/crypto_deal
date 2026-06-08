#include <gtest/gtest.h>

#include "DealService.hpp"
#include "binance/BinanceDealService.hpp"
#include "bybit/BybitDealService.hpp"
#include "common/DecimalConverter.hpp"

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#ifndef INSTANTIATE_TEST_SUITE_P
#define INSTANTIATE_TEST_SUITE_P INSTANTIATE_TEST_CASE_P
#endif

using namespace std;

namespace {
    struct Config
    {
        string binanceHost;
        string binanceApiKey;
        string binanceSecretKey;
        string binanceWebsocketHost;

        string bybitHost;
        string bybitApiKey;
        string bybitSecretKey;
        string bybitWebsocketHost;
    };

    static string getConfigPathOrEmpty()
    {
        if (const char *p = getenv("DEAL_CONFIG_INI"))
        {
            if (*p)
            {
                return string(p);
            }
        }

#ifdef CONFIG_FILE
        return string(CONFIG_FILE);
#else
        return {};
#endif
    }

    static optional<Config> tryLoadConfig()
    {
        const string path = getConfigPathOrEmpty();
        if (path.empty())
        {
            ADD_FAILURE()
                << "No config path. Define CONFIG_FILE at build time or set DEAL_CONFIG_INI=/path/to/config.ini";
            return nullopt;
        }

        boost::property_tree::ptree pt;
        try
        {
            boost::property_tree::ini_parser::read_ini(path, pt);
        }
        catch (const exception &e)
        {
            ADD_FAILURE() << "Failed to read ini config: " << e.what();
            return nullopt;
        }

        Config c;
        c.binanceHost = pt.get<string>("API.BINANCE_HOST", "");
        c.binanceApiKey = pt.get<string>("API.BINANCE_API_KEY", "");
        c.binanceSecretKey = pt.get<string>("API.BINANCE_SECRET_KEY", "");
        c.binanceWebsocketHost = pt.get<string>("API.BINANCE_WEBSOCKET_HOST", "");

        c.bybitHost = pt.get<string>("API.BYBIT_HOST", "");
        c.bybitApiKey = pt.get<string>("API.BYBIT_API_KEY", "");
        c.bybitSecretKey = pt.get<string>("API.BYBIT_SECRET_KEY", "");
        c.bybitWebsocketHost = pt.get<string>("API.BYBIT_WEBSOCKET_HOST", "");

        return c;
    }

    static string normalizeOrderStatus(const string &status)
    {
        string out;
        out.reserve(status.size());
        for (unsigned char ch : status)
        {
            if (isalnum(ch))
            {
                out.push_back(static_cast<char>(tolower(ch)));
            }
        }
        return out;
    }

    static Decimal remainingQty(const OrderInfo &o)
    {
        if (o.leavesQty > 0)
        {
            return o.leavesQty;
        }
        const Decimal r = o.origQty - o.executedQty;
        return (r > 0) ? r : Decimal{0};
    }

    static bool isOpenForCancel(const OrderInfo &o)
    {
        const string s = normalizeOrderStatus(o.status);
        const bool statusOk = (s == "new") || (s == "partiallyfilled") || (s == "untriggered");
        return statusOk && (remainingQty(o) > 0);
    }

    static bool waitForStreamConnected(DealService &svc, int tries = 50, int sleepMs = 100)
    {
        for (int i = 0; i < tries; ++i)
        {
            const auto st = svc.getUserStreamStatus();
            if (st == StreamStatus::CONNECTED)
            {
                return true;
            }
            if (st == StreamStatus::ERROR)
            {
                return false;
            }
            this_thread::sleep_for(chrono::milliseconds(sleepMs));
        }
        return false;
    }

    static bool waitForAnyBalances(DealService &svc, int tries = 300, int sleepMs = 100)
    {
        for (int i = 0; i < tries; ++i)
        {
            if (!svc.getBalances().empty())
            {
                return true;
            }
            if (svc.getUserStreamStatus() != StreamStatus::CONNECTED)
            {
                return false;
            }
            this_thread::sleep_for(chrono::milliseconds(sleepMs));
        }
        return false;
    }

    enum class Exchange
    {
        BINANCE,
        BYBIT
    };

    static const char *exchangeName(Exchange e)
    {
        return (e == Exchange::BINANCE) ? "BINANCE" : "BYBIT";
    }

    static unique_ptr<DealService> tryCreateService(Exchange e, const Config &c)
    {
        if (e == Exchange::BINANCE)
        {
            if (c.binanceHost.empty() || c.binanceApiKey.empty() || c.binanceSecretKey.empty() ||
                c.binanceWebsocketHost.empty())
            {
                ADD_FAILURE() << "Missing Binance config keys (API.BINANCE_*)";
                return {};
            }

            return make_unique<BinanceDealService>(c.binanceHost,
                                                   c.binanceApiKey,
                                                   c.binanceSecretKey,
                                                   c.binanceWebsocketHost);
        }

        if (c.bybitHost.empty() || c.bybitApiKey.empty() || c.bybitSecretKey.empty() || c.bybitWebsocketHost.empty())
        {
            ADD_FAILURE() << "Missing Bybit config keys (API.BYBIT_*)";
            return {};
        }

        return make_unique<BybitDealService>(c.bybitHost, c.bybitApiKey, c.bybitSecretKey, c.bybitWebsocketHost);
    }

    static void callGetBalancesRestOrFail(DealService &svc, Exchange ex)
    {
        if (ex == Exchange::BINANCE)
        {
            auto *b = dynamic_cast<BinanceDealService *>(&svc);
            ASSERT_NE(b, nullptr) << "Expected BinanceDealService instance for BINANCE param";
            ASSERT_NO_THROW(b->getBalancesRest());
            return;
        }

        auto *b = dynamic_cast<BybitDealService *>(&svc);
        ASSERT_NE(b, nullptr) << "Expected BybitDealService instance for BYBIT param";
        ASSERT_NO_THROW(b->getBalancesRest());
    }

    class DealServiceLiveIT : public ::testing::TestWithParam<Exchange>
    {
      protected:
        Config cfg{};
        unique_ptr<DealService> svc;

        const string symbol = "BTCUSDT";
        const string category = "spot";

        void SetUp() override
        {
            auto cfgOpt = tryLoadConfig();
            ASSERT_TRUE(cfgOpt.has_value()) << "Config load failed (see failures above).";
            cfg = cfgOpt.value();

            svc = tryCreateService(GetParam(), cfg);
            ASSERT_TRUE(svc != nullptr) << "Service create failed (see failures above).";

            try
            {
                svc->cancelAllOpenOrders(symbol, OrderCategory::SPOT);
            }
            catch (const exception &e)
            {
                cerr << "Pre-test cleanup cancelAllOpenOrders failed (continuing): " << e.what() << "\n";
            }
        }

        void TearDown() override
        {
            if (!svc)
            {
                return;
            }

            try
            {
                svc->stopUserStream();
            }
            catch (const exception &e)
            {
                cerr << "stopUserStream failed in TearDown: " << e.what() << "\n";
            }
        }
    };

    TEST_P(DealServiceLiveIT, RestBalancesSnapshot)
    {
        SCOPED_TRACE(string("Exchange=") + exchangeName(GetParam()));

        ASSERT_TRUE(svc != nullptr);

        SCOPED_TRACE("Step: getBalancesRest()");
        callGetBalancesRestOrFail(*svc, GetParam());

        const auto balances = svc->getBalances();
        ASSERT_FALSE(balances.empty()) << "REST balances snapshot produced empty cache.";

        const bool hasUSDT = svc->getBalance("USDT").has_value();
        const bool hasBTC = svc->getBalance("BTC").has_value();
        EXPECT_TRUE(hasUSDT || hasBTC) << "Neither USDT nor BTC found in balance cache after REST snapshot.";
    }

    TEST_P(DealServiceLiveIT, Smoke)
    {
        SCOPED_TRACE(string("Exchange=") + exchangeName(GetParam()));
        ASSERT_TRUE(svc != nullptr);

        {
            SCOPED_TRACE("Step: getSymbolInfo");
            SymbolInfo info;
            ASSERT_NO_THROW(info = svc->getSymbolInfo(symbol, OrderCategory::SPOT));
            EXPECT_EQ(info.symbol, symbol);
            EXPECT_GT(info.tickSize, Decimal{0});
            EXPECT_GT(info.stepSize, Decimal{0});
            EXPECT_GT(info.minQty, Decimal{0});
            EXPECT_FALSE(info.baseAsset.empty());
            EXPECT_FALSE(info.quoteAsset.empty());
        }

        {
            SCOPED_TRACE("Step: seed balances via REST (explicit)");
            callGetBalancesRestOrFail(*svc, GetParam());
            ASSERT_FALSE(svc->getBalances().empty()) << "Balance cache still empty after REST snapshot.";
        }

        {
            SCOPED_TRACE("Step: startUserStream + wait connected");
            ASSERT_NO_THROW(svc->startUserStream());
            const bool connected = waitForStreamConnected(*svc);
            ASSERT_TRUE(connected) << "Stream not connected. Status=" << static_cast<int>(svc->getUserStreamStatus())
                                   << " Error=" << svc->getUserStreamLastError();

            SCOPED_TRACE("Step: wait balances (should be immediate if REST already seeded)");
            const bool gotBalances = waitForAnyBalances(*svc, /*tries=*/600, /*sleepMs=*/100);
            ASSERT_TRUE(gotBalances) << "Balances cache empty / stream disconnected while waiting.";
        }

        {
            SCOPED_TRACE("Step: buyCrypto + sellCrypto");
            const string base = "BTC";
            const string quote = "USDT";
            const Decimal qty = DecimalConverter::parseDecimal("0.00009");

            OrderInfo buyOrder;
            ASSERT_NO_THROW(buyOrder = svc->buyCrypto(base, quote, qty));
            EXPECT_FALSE(buyOrder.orderId.empty());

            OrderInfo sellOrder;
            ASSERT_NO_THROW(sellOrder = svc->sellCrypto(base, quote, qty));
            EXPECT_FALSE(sellOrder.orderId.empty());
        }

        {
            SCOPED_TRACE("Step: place/get/cancel limit");
            PlaceOrderRequest req;
            req.symbol = symbol;
            req.category = OrderCategory::SPOT;
            req.side = OrderOperation::BUY;
            req.type = OrderType::LIMIT;
            req.quantity = DecimalConverter::parseDecimal("0.0002");
            req.price = DecimalConverter::parseDecimal("65000.0");
            req.timeInForce = string("GTC");
            req.clientOrderId = string("IT_ORDER_") + to_string(time(nullptr));

            OrderInfo placed;
            ASSERT_NO_THROW(placed = svc->placeOrder(req));
            EXPECT_FALSE(placed.orderId.empty());
            EXPECT_FALSE(placed.status.empty());

            OrderQuery q;
            q.symbol = symbol;
            q.category = OrderCategory::SPOT;
            if (!placed.orderId.empty())
            {
                q.orderId = placed.orderId;
            }
            else
            {
                q.clientOrderId = placed.clientOrderId;
            }

            OrderInfo got;
            ASSERT_NO_THROW(got = svc->getOrder(q));
            EXPECT_FALSE(got.status.empty());

            const bool shouldCancel = isOpenForCancel(got);
            if (shouldCancel)
            {
                OrderInfo cancelled;
                ASSERT_NO_THROW(cancelled = svc->cancelOrder(q));
                const string st = normalizeOrderStatus(cancelled.status);
                EXPECT_TRUE(st == "canceled" || st == "cancelled") << "Unexpected cancel status: " << cancelled.status;
            }
            else
            {
                SUCCEED() << "Order not open for cancel (status=" << got.status << ")";
            }
        }
    }

    TEST_P(DealServiceLiveIT, OcoPlaceCancel)
    {
        SCOPED_TRACE(string("Exchange=") + exchangeName(GetParam()));
        ASSERT_TRUE(svc != nullptr);

        {
            SCOPED_TRACE("Step: refresh balances before funding buy");
            callGetBalancesRestOrFail(*svc, GetParam());
        }

        {
            SCOPED_TRACE("Step: buy BTC to fund OCO SELL");
            const string base = "BTC";
            const string quote = "USDT";
            const Decimal qty = DecimalConverter::parseDecimal("0.00010");

            OrderInfo buyOrder;
            ASSERT_NO_THROW(buyOrder = svc->buyCrypto(base, quote, qty));
            EXPECT_FALSE(buyOrder.orderId.empty()) << "buyCrypto did not create an order (needed to fund OCO SELL)";
        }

        {
            SCOPED_TRACE("Step: refresh balances after funding buy");
            callGetBalancesRestOrFail(*svc, GetParam());
        }

        PlaceOcoRequest oco;
        oco.symbol = symbol;
        oco.side = OrderOperation::SELL;
        oco.quantity = DecimalConverter::parseDecimal("0.00010");

        oco.price = DecimalConverter::parseDecimal("90000.0");
        oco.stopPrice = DecimalConverter::parseDecimal("60000.0");

        oco.stopLimitPrice = DecimalConverter::parseDecimal("59000.0");
        oco.stopLimitTimeInForce = string("GTC");

        oco.listClientOrderId = string("IT_OCO_") + to_string(time(nullptr));

        OcoInfo placed;
        {
            SCOPED_TRACE("Step: placeOco");
            try
            {
                placed = svc->placeOco(oco);
            }
            catch (const std::exception &e)
            {
                FAIL() << "placeOco threw: " << e.what();
            }

            ASSERT_FALSE(placed.listClientOrderId.empty()) << "placeOco returned empty listClientOrderId";
        }

        {
            SCOPED_TRACE("Step: cancelOco");
            OrderListQuery q;
            q.symbol = symbol;
            q.category = OrderCategory::SPOT;
            q.listClientOrderId = placed.listClientOrderId;

            try
            {
                OcoInfo cancelled = svc->cancelOco(q);
                EXPECT_GE(cancelled.orders.size(), 1u);
            }
            catch (const std::exception &e)
            {
                FAIL() << "cancelOco threw: " << e.what();
            }
        }

        {
            SCOPED_TRACE("Step: cleanup sell BTC");
            const string base = "BTC";
            const string quote = "USDT";
            const Decimal qty = DecimalConverter::parseDecimal("0.00010");

            OrderInfo sellOrder;
            ASSERT_NO_THROW(sellOrder = svc->sellCrypto(base, quote, qty));
            EXPECT_FALSE(sellOrder.orderId.empty()) << "cleanup sellCrypto did not create an order";
        }
    }

    INSTANTIATE_TEST_SUITE_P(Live, DealServiceLiveIT, ::testing::Values(Exchange::BINANCE, Exchange::BYBIT));

}
