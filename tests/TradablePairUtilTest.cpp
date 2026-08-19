#include "../src/DealService/common/TradablePairUtil.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(TradablePairUtilTest, FiltersIncompletePairsAndDuplicateSymbols)
{
    const std::vector<TradablePair> pairs = filterAndSortTradablePairs({
        {"BTCUSDT", "BTC", "USDT"},
        {"BTCUSDT", "BTC", "USDT"},
        {"", "ETH", "USDT"},
        {"ETHUSDT", "", "USDT"},
        {"ETHUSDT", "ETH", ""},
    });

    ASSERT_EQ(pairs.size(), 1u);
    EXPECT_EQ(pairs[0], (TradablePair{"BTCUSDT", "BTC", "USDT"}));
}

TEST(TradablePairUtilTest, SortsByBaseAssetThenQuoteAssetThenSymbol)
{
    const std::vector<TradablePair> pairs = filterAndSortTradablePairs({
        {"ETHUSDT", "ETH", "USDT"},
        {"BTCUSDT-B", "BTC", "USDT"},
        {"BTCUSDC", "BTC", "USDC"},
        {"BTCUSDT-A", "BTC", "USDT"},
    });

    const std::vector<TradablePair> expected = {
        {"BTCUSDC", "BTC", "USDC"},
        {"BTCUSDT-A", "BTC", "USDT"},
        {"BTCUSDT-B", "BTC", "USDT"},
        {"ETHUSDT", "ETH", "USDT"},
    };
    EXPECT_EQ(pairs, expected);
}
