#include "../src/OperationChain/OperationChain.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

TEST(OperationChainTest, ExecutesOperationsInOrderWithSharedContext)
{
    std::vector<int> executionOrder;
    const operation firstOperation = [&executionOrder](OperationContext &context)
    {
        executionOrder.push_back(1);
        EXPECT_EQ(context.exchangerType, ExchangerType::BINANCE);
        EXPECT_EQ(context.inAsset, "USDT");
        EXPECT_EQ(context.quantity, Decimal{10});

        context.inAsset = "BTC";
        context.quantity = Decimal{2};
    };
    const operation secondOperation = [&executionOrder](OperationContext &context)
    {
        executionOrder.push_back(2);
        EXPECT_EQ(context.exchangerType, ExchangerType::BINANCE);
        EXPECT_EQ(context.inAsset, "BTC");
        EXPECT_EQ(context.quantity, Decimal{2});

        context.exchangerType = ExchangerType::BYBIT;
    };
    const operation thirdOperation = [&executionOrder](OperationContext &context)
    {
        executionOrder.push_back(3);
        EXPECT_EQ(context.exchangerType, ExchangerType::BYBIT);
    };

    OperationChain chain({firstOperation, secondOperation, thirdOperation},
                         {},
                         ExchangerType::BINANCE,
                         "USDT",
                         Decimal{10});

    chain.execute();

    EXPECT_EQ(executionOrder, (std::vector<int>{1, 2, 3}));
}

TEST(OperationChainTest, ExecutesEmptyChain)
{
    OperationChain chain({}, {}, ExchangerType::BINANCE, "USDT", Decimal{10});

    EXPECT_NO_THROW(chain.execute());
}

TEST(OperationChainTest, StopsAfterOperationThrows)
{
    std::vector<int> executionOrder;
    const operation firstOperation = [&executionOrder](OperationContext &) { executionOrder.push_back(1); };
    const operation failingOperation = [&executionOrder](OperationContext &)
    {
        executionOrder.push_back(2);
        throw std::runtime_error("Operation failed");
    };
    const operation skippedOperation = [&executionOrder](OperationContext &) { executionOrder.push_back(3); };

    OperationChain chain({firstOperation, failingOperation, skippedOperation},
                         {},
                         ExchangerType::BINANCE,
                         "USDT",
                         Decimal{10});

    EXPECT_THROW(chain.execute(), std::runtime_error);
    EXPECT_EQ(executionOrder, (std::vector<int>{1, 2}));
}
