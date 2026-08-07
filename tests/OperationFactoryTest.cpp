#include "../src/OperationChain/OperationFactory.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    class OperationDealService : public DealService
    {
      public:
        OrderInfo placedOrder;
        OrderInfo filledOrder;
        OcoInfo placedOco;
        bool failOcoWait = false;
        int getOrderCalls = 0;
        int orderWaitCalls = 0;
        int ocoWaitCalls = 0;

        OperationDealService() : DealService("host", "api", "secret", "ws", 5000, ExchangerType::BINANCE) {}

        OrderInfo buyCrypto(const std::string &, const std::string &, Decimal) override
        {
            return placedOrder;
        }

        OrderInfo sellCrypto(const std::string &, const std::string &, Decimal) override
        {
            return placedOrder;
        }

        OrderInfo waitUntilOrderFilled(const std::string &, const std::string &) override
        {
            ++orderWaitCalls;
            return filledOrder;
        }

        OrderInfo waitUntilOcoOrderFilled(const OcoInfo &) override
        {
            ++ocoWaitCalls;
            if (failOcoWait)
            {
                throw std::runtime_error("OCO failed");
            }

            return filledOrder;
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
            return placedOrder;
        }

        OrderInfo cancelOrder(const OrderQuery &) override
        {
            return {};
        }

        OrderInfo getOrder(const OrderQuery &) override
        {
            ++getOrderCalls;
            return {};
        }

        SymbolInfo getSymbolInfo(const std::string &, OrderCategory = OrderCategory::SPOT) override
        {
            return {};
        }

        Decimal ceilQuantityToStep(const std::string &, Decimal quantity, OrderCategory = OrderCategory::SPOT) override
        {
            return quantity;
        }

        OcoInfo placeOco(const PlaceOcoRequest &) override
        {
            return placedOco;
        }

        void cancelOco(const OrderListQuery &) override {}

        void cancelAllOpenOrders(const std::string &, OrderCategory) override {}

        flat_map<std::string, AssetBalance> getBalancesRest() override
        {
            return {};
        }
    };

    OperationContext createContext(const std::shared_ptr<OperationDealService> &service)
    {
        OperationContext context({service});
        context.exchangerType = ExchangerType::BINANCE;
        context.inAsset = "USDT";
        context.quantity = Decimal{1};
        return context;
    }
}

TEST(OperationFactoryTest, PlaceOrderUsesReturnedWebsocketOrderWithoutRestQuery)
{
    auto service = std::make_shared<OperationDealService>();
    service->placedOrder.symbol = "BTCUSDT";
    service->placedOrder.orderId = "placed";
    service->filledOrder = service->placedOrder;
    service->filledOrder.orderId = "filled";
    service->filledOrder.clientOrderId = "client-filled";
    service->filledOrder.executedQty = Decimal{2};

    OperationContext context = createContext(service);
    PlaceOrderConfig config;
    config.outAsset = "BTC";
    config.side = OrderOperation::BUY;
    config.type = OrderType::MARKET;

    const OperationFactory factory;
    factory.create(OperationType::PLACE_ORDER, config)(context);

    EXPECT_EQ(service->orderWaitCalls, 1);
    EXPECT_EQ(service->getOrderCalls, 0);
    EXPECT_EQ(context.quantity, Decimal{2});
    EXPECT_EQ(context.inAsset, "BTC");
}

TEST(OperationFactoryTest, SellCryptoUsesCumulativeQuoteQuantity)
{
    auto service = std::make_shared<OperationDealService>();
    service->placedOrder.symbol = "BTCUSDT";
    service->placedOrder.orderId = "placed";
    service->filledOrder = service->placedOrder;
    service->filledOrder.executedQty = Decimal{2};
    service->filledOrder.cumQuoteQty = Decimal{250};

    OperationContext context = createContext(service);
    context.inAsset = "BTC";
    context.quantity = Decimal{2};
    BaseConfig config;
    config.outAsset = "USDT";

    const OperationFactory factory;
    factory.create(OperationType::SELL_CRYPTO, config)(context);

    EXPECT_EQ(context.quantity, Decimal{250});
    EXPECT_EQ(context.inAsset, "USDT");
}

TEST(OperationFactoryTest, PlaceSellOrderUsesCumulativeQuoteQuantity)
{
    auto service = std::make_shared<OperationDealService>();
    service->placedOrder.symbol = "BTCUSDT";
    service->placedOrder.orderId = "placed";
    service->filledOrder = service->placedOrder;
    service->filledOrder.executedQty = Decimal{2};
    service->filledOrder.cumQuoteQty = Decimal{250};

    OperationContext context = createContext(service);
    context.inAsset = "BTC";
    context.quantity = Decimal{2};
    PlaceOrderConfig config;
    config.outAsset = "USDT";
    config.side = OrderOperation::SELL;
    config.type = OrderType::MARKET;

    const OperationFactory factory;
    factory.create(OperationType::PLACE_ORDER, config)(context);

    EXPECT_EQ(context.quantity, Decimal{250});
    EXPECT_EQ(context.inAsset, "USDT");
}

TEST(OperationFactoryTest, PlaceOcoDelegatesToOcoWaitAndUsesFilledChild)
{
    auto service = std::make_shared<OperationDealService>();
    service->placedOco.orderListId = "list";
    service->filledOrder.symbol = "BTCUSDT";
    service->filledOrder.orderId = "filled-child";
    service->filledOrder.executedQty = Decimal{3};

    OperationContext context = createContext(service);
    PlaceOcoConfig config;
    config.outAsset = "BTC";
    config.side = OrderOperation::BUY;
    config.price = Decimal{10};
    config.stopPrice = Decimal{9};

    const OperationFactory factory;
    factory.create(OperationType::PLACE_OCO, config)(context);

    EXPECT_EQ(service->ocoWaitCalls, 1);
    EXPECT_EQ(service->getOrderCalls, 0);
    EXPECT_EQ(context.quantity, Decimal{3});
    EXPECT_EQ(context.inAsset, "BTC");
}

TEST(OperationFactoryTest, PlaceSellOcoUsesCumulativeQuoteQuantity)
{
    auto service = std::make_shared<OperationDealService>();
    service->placedOco.orderListId = "list";
    service->filledOrder.symbol = "BTCUSDT";
    service->filledOrder.orderId = "filled-child";
    service->filledOrder.executedQty = Decimal{2};
    service->filledOrder.cumQuoteQty = Decimal{250};

    OperationContext context = createContext(service);
    context.inAsset = "BTC";
    context.quantity = Decimal{2};
    PlaceOcoConfig config;
    config.outAsset = "USDT";
    config.side = OrderOperation::SELL;
    config.price = Decimal{10};
    config.stopPrice = Decimal{9};

    const OperationFactory factory;
    factory.create(OperationType::PLACE_OCO, config)(context);

    EXPECT_EQ(context.quantity, Decimal{250});
    EXPECT_EQ(context.inAsset, "USDT");
}

TEST(OperationFactoryTest, PlaceOcoWaitFailurePropagatesWithoutMutatingContext)
{
    auto service = std::make_shared<OperationDealService>();
    service->failOcoWait = true;

    OperationContext context = createContext(service);
    PlaceOcoConfig config;
    config.outAsset = "BTC";
    config.side = OrderOperation::BUY;
    config.price = Decimal{10};
    config.stopPrice = Decimal{9};

    const OperationFactory factory;
    const operation operation = factory.create(OperationType::PLACE_OCO, config);

    EXPECT_THROW(operation(context), std::runtime_error);
    EXPECT_EQ(context.inAsset, "USDT");
    EXPECT_EQ(context.quantity, Decimal{1});
}
