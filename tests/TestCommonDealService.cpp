#include "../src/DealService/DealService.hpp"
#include "../src/DealService/common/DecimalConverter.hpp"

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
        void waitUntilOrderFilled(const std::string &, const std::string &) override {}
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
        SymbolInfo getSymbolInfo(const std::string &, const std::string & = "spot") override
        {
            return {};
        }
        Decimal ceilQuantityToStep(const std::string &, Decimal quantity, const std::string & = "spot") override
        {
            return quantity;
        }
        OcoInfo placeOco(const PlaceOcoRequest &) override
        {
            return {};
        }
        OcoInfo cancelOco(const OrderListQuery &) override
        {
            return {};
        }
        bool cancelAllOpenOrders(const std::string &, const std::string &) override
        {
            return true;
        }
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
