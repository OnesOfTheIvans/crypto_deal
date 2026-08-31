#include "../src/DealService/DealService.hpp"
#include "../src/DealService/common/DecimalConverter.hpp"

#include <algorithm>
#include <cctype>

#include <gtest/gtest.h>

namespace {
    class DummyDealService : public DealService
    {
      public:
        DummyDealService() : DealService("host", "api", "secret", "ws", 5000, ExchangerType::BINANCE) {}

        std::string hmac(const std::string &key, const std::string &data) const
        {
            return hmac_sha256(key, data);
        }

        OrderInfo buyCrypto(const std::string &, const std::string &, Decimal) override
        {
            return {};
        }
        OrderInfo sellCrypto(const std::string &, const std::string &, Decimal) override
        {
            return {};
        }

        OrderInfo waitUntilOrderFilled(const std::string &, const std::string &) override
        {
            return {};
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
        OrderInfo getOrder(const OrderQuery &query) override
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
} // namespace

TEST(DecimalConverterTest, ParsesFormatsAndRoundsByStep)
{
    const Decimal value = DecimalConverter::parseDecimal("1.23456");

    EXPECT_EQ(DecimalConverter::formatDecimal(value, 4), "1.2346");
    EXPECT_EQ(DecimalConverter::formatDecimal(DecimalConverter::parseDecimal("-0.0000")), "0");
    EXPECT_EQ(DecimalConverter::formatByStep(value, DecimalConverter::parseDecimal("0.0100")), "1.23");
    EXPECT_EQ(DecimalConverter::ceilToStep(value, DecimalConverter::parseDecimal("0.01")),
              DecimalConverter::parseDecimal("1.24"));
    EXPECT_EQ(DecimalConverter::floorToStep(value, DecimalConverter::parseDecimal("0.01")),
              DecimalConverter::parseDecimal("1.23"));
    EXPECT_EQ(DecimalConverter::ceilToStep(value, Decimal{}), value);
    EXPECT_EQ(DecimalConverter::floorToStep(value, Decimal{}), value);
    EXPECT_THROW(DecimalConverter::parseDecimal("12abc"), std::runtime_error);
}

TEST(DealServiceHelpersTest, ComputesKnownHmacSha256)
{
    const DummyDealService service;

    EXPECT_EQ(service.hmac("key", "The quick brown fox jumps over the lazy dog"),
              "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
}

TEST(DealServiceHelpersTest, GeneratesUniqueOcoId)
{
    const DummyDealService service;

    const std::string firstId = service.generateUniqueOcoId();
    const std::string secondId = service.generateUniqueOcoId();

    EXPECT_EQ(firstId.size(), 33u);
    EXPECT_EQ(firstId[0], 'O');
    EXPECT_TRUE(std::all_of(firstId.begin() + 1,
                            firstId.end(),
                            [](unsigned char c) { return std::islower(c) || std::isdigit(c); }));
    EXPECT_NE(firstId, secondId);
}
