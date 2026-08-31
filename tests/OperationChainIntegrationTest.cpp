#include "../src/OperationChain/OperationChain.hpp"
#include "../src/OperationChain/OperationFactory.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
    class OperationChainDealService : public DealService
    {
      public:
        OrderInfo placedOrder;
        OrderInfo filledOrder;
        std::string buyBaseAsset;
        std::string buyQuoteAsset;
        std::string sellBaseAsset;
        std::string sellQuoteAsset;
        Decimal buyQuantity{};
        Decimal sellQuantity{};
        int orderWaitCalls = 0;

        explicit OperationChainDealService(ExchangerType exchangerType)
            : DealService("host", "api", "secret", "ws", 5000, exchangerType)
        {}

        OrderInfo buyCrypto(const std::string &baseAsset, const std::string &quoteAsset, Decimal quantity) override
        {
            buyBaseAsset = baseAsset;
            buyQuoteAsset = quoteAsset;
            buyQuantity = quantity;
            return placedOrder;
        }

        OrderInfo sellCrypto(const std::string &baseAsset, const std::string &quoteAsset, Decimal quantity) override
        {
            sellBaseAsset = baseAsset;
            sellQuoteAsset = quoteAsset;
            sellQuantity = quantity;
            return placedOrder;
        }

        OrderInfo waitUntilOrderFilled(const std::string &, const std::string &) override
        {
            ++orderWaitCalls;
            return filledOrder;
        }

        OcoWaitResult waitUntilOcoOrderFilled(const OcoInfo &) override
        {
            return {};
        }

        flat_map<std::string, AssetBalance> getBalances() const override
        {
            return {};
        }

        std::optional<AssetBalance> getBalance(const std::string &) const override
        {
            return std::nullopt;
        }

        void startUserStream() override {}

        void stopUserStream() override {}

        StreamStatus getUserStreamStatus() const override
        {
            return StreamStatus::STOPPED;
        }

        std::string getUserStreamLastError() const override
        {
            return {};
        }

        OrderInfo placeOrder(const PlaceOrderRequest &) override
        {
            return {};
        }

        OrderInfo cancelOrder(const OrderQuery &) override
        {
            return {};
        }

        OrderInfo getOrder(const OrderQuery &) override
        {
            return {};
        }

        SymbolInfo getSymbolInfo(const std::string &, OrderCategory = OrderCategory::SPOT) override
        {
            return {};
        }

        std::vector<TradablePair> getTradablePairs() override
        {
            return {};
        }

        Decimal ceilQuantityToStep(const std::string &, Decimal quantity, OrderCategory = OrderCategory::SPOT) override
        {
            return quantity;
        }

        OcoInfo placeOco(const PlaceOcoRequest &) override
        {
            return {};
        }

        void cancelOco(const OrderListQuery &) override {}

        void cancelAllOpenOrders(const std::string &, OrderCategory) override {}

        flat_map<std::string, AssetBalance> getBalancesRest() override
        {
            return {};
        }
    };
}

TEST(OperationChainIntegrationTest, ExecutesFactoryOperationsAcrossExchangers)
{
    auto binanceService = std::make_shared<OperationChainDealService>(ExchangerType::BINANCE);
    binanceService->placedOrder.symbol = "BTCUSDT";
    binanceService->placedOrder.orderId = "binance-placed";
    binanceService->filledOrder = binanceService->placedOrder;
    binanceService->filledOrder.executedQty = Decimal{2};

    auto bybitService = std::make_shared<OperationChainDealService>(ExchangerType::BYBIT);
    bybitService->placedOrder.symbol = "BTCUSDT";
    bybitService->placedOrder.orderId = "bybit-placed";
    bybitService->filledOrder = bybitService->placedOrder;
    bybitService->filledOrder.executedQty = Decimal{2};
    bybitService->filledOrder.cumQuoteQty = Decimal{250};

    BaseConfig buyConfig;
    buyConfig.outAsset = "BTC";
    SendToConfig sendConfig;
    sendConfig.destinationExchanger = ExchangerType::BYBIT;
    sendConfig.chain = "test-chain";
    sendConfig.address = "test-address";
    BaseConfig sellConfig;
    sellConfig.outAsset = "USDT";

    const OperationFactory factory;
    bool finalContextObserved = false;
    const operation observeFinalContext = [&finalContextObserved](OperationContext &context)
    {
        EXPECT_EQ(context.exchangerType, ExchangerType::BYBIT);
        EXPECT_EQ(context.inAsset, "USDT");
        EXPECT_EQ(context.quantity, Decimal{250});
        finalContextObserved = true;
    };
    const std::vector<operation> operations{
        factory.create(OperationType::BUY_CRYPTO, buyConfig),
        factory.create(OperationType::SEND_TO, sendConfig),
        factory.create(OperationType::SELL_CRYPTO, sellConfig),
        observeFinalContext,
    };

    OperationChain chain(operations, {binanceService, bybitService}, ExchangerType::BINANCE, "USDT", Decimal{1});

    chain.execute();

    EXPECT_EQ(binanceService->buyBaseAsset, "BTC");
    EXPECT_EQ(binanceService->buyQuoteAsset, "USDT");
    EXPECT_EQ(binanceService->buyQuantity, Decimal{1});
    EXPECT_EQ(binanceService->orderWaitCalls, 1);
    EXPECT_EQ(bybitService->sellBaseAsset, "BTC");
    EXPECT_EQ(bybitService->sellQuoteAsset, "USDT");
    EXPECT_EQ(bybitService->sellQuantity, Decimal{2});
    EXPECT_EQ(bybitService->orderWaitCalls, 1);
    EXPECT_TRUE(finalContextObserved);
}
