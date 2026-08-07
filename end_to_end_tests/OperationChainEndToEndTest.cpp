#include <gtest/gtest.h>

#include "OperationChain.hpp"
#include "OperationFactory.hpp"
#include "binance/BinanceDealService.hpp"
#include "bybit/BybitDealService.hpp"
#include "common/DecimalConverter.hpp"

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

    string getConfigPathOrEmpty()
    {
        if (const char *path = getenv("DEAL_CONFIG_INI"))
        {
            if (*path)
            {
                return string(path);
            }
        }

#ifdef CONFIG_FILE
        return string(CONFIG_FILE);
#else
        return {};
#endif
    }

    optional<Config> tryLoadConfig()
    {
        const string path = getConfigPathOrEmpty();
        if (path.empty())
        {
            ADD_FAILURE()
                << "No config path. Define CONFIG_FILE at build time or set DEAL_CONFIG_INI=/path/to/config.ini";
            return nullopt;
        }

        boost::property_tree::ptree propertyTree;
        try
        {
            boost::property_tree::ini_parser::read_ini(path, propertyTree);
        }
        catch (const exception &e)
        {
            ADD_FAILURE() << "Failed to read ini config: " << e.what();
            return nullopt;
        }

        Config config;
        config.binanceHost = propertyTree.get<string>("API.BINANCE_HOST", "");
        config.binanceApiKey = propertyTree.get<string>("API.BINANCE_API_KEY", "");
        config.binanceSecretKey = propertyTree.get<string>("API.BINANCE_SECRET_KEY", "");
        config.binanceWebsocketHost = propertyTree.get<string>("API.BINANCE_WEBSOCKET_HOST", "");
        config.bybitHost = propertyTree.get<string>("API.BYBIT_HOST", "");
        config.bybitApiKey = propertyTree.get<string>("API.BYBIT_API_KEY", "");
        config.bybitSecretKey = propertyTree.get<string>("API.BYBIT_SECRET_KEY", "");
        config.bybitWebsocketHost = propertyTree.get<string>("API.BYBIT_WEBSOCKET_HOST", "");
        return config;
    }

    shared_ptr<DealService> tryCreateService(ExchangerType exchangerType, const Config &config)
    {
        if (exchangerType == ExchangerType::BINANCE)
        {
            if (config.binanceHost.empty() || config.binanceApiKey.empty() || config.binanceSecretKey.empty() ||
                config.binanceWebsocketHost.empty())
            {
                ADD_FAILURE() << "Missing Binance config keys (API.BINANCE_*)";
                return {};
            }

            return make_shared<BinanceDealService>(config.binanceHost,
                                                   config.binanceApiKey,
                                                   config.binanceSecretKey,
                                                   config.binanceWebsocketHost);
        }

        if (config.bybitHost.empty() || config.bybitApiKey.empty() || config.bybitSecretKey.empty() ||
            config.bybitWebsocketHost.empty())
        {
            ADD_FAILURE() << "Missing Bybit config keys (API.BYBIT_*)";
            return {};
        }

        return make_shared<BybitDealService>(config.bybitHost,
                                             config.bybitApiKey,
                                             config.bybitSecretKey,
                                             config.bybitWebsocketHost);
    }

    string getExchangerName(ExchangerType exchangerType)
    {
        return exchangerType == ExchangerType::BINANCE ? "BINANCE" : "BYBIT";
    }

    bool waitForStreamConnected(DealService &service, int tries = 50, int sleepMs = 100)
    {
        for (int attempt = 0; attempt < tries; ++attempt)
        {
            const StreamStatus status = service.getUserStreamStatus();
            if (status == StreamStatus::CONNECTED)
            {
                return true;
            }
            if (status == StreamStatus::ERROR)
            {
                return false;
            }
            this_thread::sleep_for(chrono::milliseconds(sleepMs));
        }
        return false;
    }

    void executeOperationChainInNewThread(OperationChain &chain, DealService &service)
    {
        packaged_task<void()> task([&chain]() { chain.execute(); });
        future<void> result = task.get_future();
        thread operationChainThread(move(task));

        if (result.wait_for(chrono::seconds(60)) != future_status::ready)
        {
            try
            {
                service.stopUserStream();
            }
            catch (const exception &e)
            {
                cerr << "stopUserStream failed after operation chain timed out: " << e.what() << "\n";
            }

            operationChainThread.join();
            throw runtime_error("Operation chain did not finish within 60 seconds");
        }

        operationChainThread.join();
        result.get();
    }

    class OperationChainEndToEndTest : public ::testing::TestWithParam<ExchangerType>
    {
      protected:
        shared_ptr<DealService> service;
        const string symbol = "BTCUSDT";

        void SetUp() override
        {
            const optional<Config> config = tryLoadConfig();
            ASSERT_TRUE(config.has_value()) << "Config load failed (see failures above).";

            service = tryCreateService(GetParam(), config.value());
            ASSERT_TRUE(service != nullptr) << "Service create failed (see failures above).";

            try
            {
                service->cancelAllOpenOrders(symbol, OrderCategory::SPOT);
            }
            catch (const exception &e)
            {
                cerr << "Pre-test cleanup cancelAllOpenOrders failed (continuing): " << e.what() << "\n";
            }
        }

        void TearDown() override
        {
            if (!service)
            {
                return;
            }

            try
            {
                service->stopUserStream();
            }
            catch (const exception &e)
            {
                cerr << "stopUserStream failed in TearDown: " << e.what() << "\n";
            }
        }
    };

    TEST_P(OperationChainEndToEndTest, ExecutesBuyAndSellInNewThread)
    {
        SCOPED_TRACE(string("Exchange=") + getExchangerName(GetParam()));
        ASSERT_TRUE(service != nullptr);

        ASSERT_NO_THROW(service->getBalancesRest());
        ASSERT_NO_THROW(service->startUserStream());
        ASSERT_TRUE(waitForStreamConnected(*service))
            << "Stream not connected. Status=" << static_cast<int>(service->getUserStreamStatus())
            << " Error=" << service->getUserStreamLastError();

        BaseConfig buyConfig;
        buyConfig.outAsset = "BTC";
        BaseConfig sellConfig;
        sellConfig.outAsset = "USDT";

        bool finalContextObserved = false;
        ExchangerType finalExchangerType = GetParam();
        string finalAsset;
        Decimal finalQuantity{};

        const OperationFactory factory;
        const operation refreshBalances = [](OperationContext &context)
        { context.exchangersPull.getExchanger(context.exchangerType)->getBalancesRest(); };
        const operation observeFinalContext =
            [&finalContextObserved, &finalExchangerType, &finalAsset, &finalQuantity](OperationContext &context)
        {
            finalExchangerType = context.exchangerType;
            finalAsset = context.inAsset;
            finalQuantity = context.quantity;
            finalContextObserved = true;
        };
        const vector<operation> operations{
            factory.create(OperationType::BUY_CRYPTO, buyConfig),
            refreshBalances,
            factory.create(OperationType::SELL_CRYPTO, sellConfig),
            observeFinalContext,
        };

        OperationChain chain(operations, {service}, GetParam(), "USDT", DecimalConverter::parseDecimal("0.00010"));

        ASSERT_NO_THROW(executeOperationChainInNewThread(chain, *service));

        EXPECT_TRUE(finalContextObserved);
        EXPECT_EQ(finalExchangerType, GetParam());
        EXPECT_EQ(finalAsset, "USDT");
        EXPECT_GT(finalQuantity, Decimal{0});
    }

    INSTANTIATE_TEST_SUITE_P(EndToEnd,
                             OperationChainEndToEndTest,
                             ::testing::Values(ExchangerType::BINANCE, ExchangerType::BYBIT));
}
