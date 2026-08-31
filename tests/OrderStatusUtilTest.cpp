#include "common/OrderStatusUtil.hpp"

#include <gtest/gtest.h>

TEST(OrderStatusUtilTest, ClassifiesBinanceStatuses)
{
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BINANCE, ""), OrderStatusState::UNAVAILABLE);
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BINANCE, "NEW"), OrderStatusState::ACTIVE);
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BINANCE, "PARTIALLY_FILLED"),
              OrderStatusState::ACTIVE);
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BINANCE, "FILLED"), OrderStatusState::FILLED);
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BINANCE, "CANCELED"), OrderStatusState::CANCELLED);
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BINANCE, "EXPIRED"),
              OrderStatusState::OTHER_TERMINAL);
}

TEST(OrderStatusUtilTest, ClassifiesBybitStatuses)
{
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BYBIT, ""), OrderStatusState::UNAVAILABLE);
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BYBIT, "New"), OrderStatusState::ACTIVE);
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BYBIT, "Untriggered"), OrderStatusState::ACTIVE);
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BYBIT, "Filled"), OrderStatusState::FILLED);
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BYBIT, "Cancelled"), OrderStatusState::CANCELLED);
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BYBIT, "PartiallyFilledCanceled"),
              OrderStatusState::CANCELLED);
    EXPECT_EQ(OrderStatusUtil::classifyOrderStatus(ExchangerType::BYBIT, "Rejected"), OrderStatusState::OTHER_TERMINAL);
}

TEST(OrderStatusUtilTest, ReportsFilledCancelledAndTerminalQuestions)
{
    EXPECT_TRUE(OrderStatusUtil::isOrderFilled(ExchangerType::BINANCE, "FILLED"));
    EXPECT_FALSE(OrderStatusUtil::isOrderFilled(ExchangerType::BYBIT, "Cancelled"));
    EXPECT_TRUE(OrderStatusUtil::isOrderCancelled(ExchangerType::BYBIT, "Cancelled"));
    EXPECT_TRUE(OrderStatusUtil::isOrderTerminal(ExchangerType::BINANCE, "REJECTED"));
    EXPECT_FALSE(OrderStatusUtil::isOrderTerminal(ExchangerType::BYBIT, "PartiallyFilled"));
}
