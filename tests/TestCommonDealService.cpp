#include "../src/DealService/DealService.hpp"
#include "../src/DealService/common/DecimalConverter.hpp"

#include <boost/json.hpp>
#include <gtest/gtest.h>

namespace {
    namespace json = boost::json;

    class DummyDealService : public DealService
    {
      public:
        DummyDealService() : DealService("host", "api", "secret", "ws", 5000, ExchangerType::BINANCE) {}

        std::string hmac(const std::string &key, const std::string &data) const
        {
            return hmac_sha256(key, data);
        }

        void parseString(std::string &out, const json::object &obj, json::string_view field, bool optional = false) const
        {
            parseAndSetParameter(out, obj, field, optional);
        }

        void parseDecimal(Decimal &out, const json::object &obj, json::string_view field, bool optional = false) const
        {
            parseAndSetParameter(out, obj, field, optional);
        }

        void parseLongLong(long long &out, const json::object &obj, json::string_view field, bool optional = false) const
        {
            parseAndSetParameter(out, obj, field, optional);
        }

        void parseInt(int &out, const json::object &obj, json::string_view field, bool optional = false) const
        {
            parseAndSetParameter(out, obj, field, optional);
        }

        OrderInfo buyCrypto(const std::string &, const std::string &, Decimal) override { return {}; }
        OrderInfo sellCrypto(const std::string &, const std::string &, Decimal) override { return {}; }
        void waitUntilOrderFilled(const std::string &, const std::string &) override {}
        flat_map<std::string, AssetBalance> getBalances() const override { return {}; }
        std::optional<AssetBalance> getBalance(const std::string &) const override { return std::nullopt; }
        void startUserStream() override {}
        void stopUserStream() override {}
        StreamStatus getUserStreamStatus() const override { return StreamStatus::STOPPED; }
        std::string getUserStreamLastError() const override { return {}; }
        OrderInfo placeOrder(const PlaceOrderRequest &) override { return {}; }
        OrderInfo cancelOrder(const OrderQuery &) override { return {}; }
        OrderInfo getOrder(const OrderQuery &) override { return {}; }
        SymbolInfo getSymbolInfo(const std::string &, const std::string & = "spot") override { return {}; }
        OcoInfo placeOco(const PlaceOcoRequest &) override { return {}; }
        OcoInfo cancelOco(const OrderListQuery &) override { return {}; }
        bool cancelAllOpenOrders(const std::string &, const std::string &) override { return true; }
        flat_map<std::string, AssetBalance> getBalancesRest() override { return {}; }
    };

    json::object parseObject(const std::string &text)
    {
        return json::parse(text).as_object();
    }
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

TEST(DealServiceHelpersTest, ParsesSupportedJsonFieldTypes)
{
    const DummyDealService service;
    const json::object obj = parseObject(R"({
        "stringValue": "abc",
        "integerAsString": 42,
        "decimalValue": "10.2500",
        "blankDecimal": "",
        "timestampText": "1776977716000",
        "timestampNumber": 1776977716001,
        "intValue": 123
    })");

    std::string text;
    service.parseString(text, obj, "stringValue");
    EXPECT_EQ(text, "abc");
    service.parseString(text, obj, "integerAsString");
    EXPECT_EQ(text, "42");

    Decimal decimal;
    service.parseDecimal(decimal, obj, "decimalValue");
    EXPECT_EQ(decimal, DecimalConverter::parseDecimal("10.25"));
    service.parseDecimal(decimal, obj, "blankDecimal");
    EXPECT_EQ(decimal, Decimal{});

    long long timestamp = 0;
    service.parseLongLong(timestamp, obj, "timestampText");
    EXPECT_EQ(timestamp, 1776977716000LL);
    service.parseLongLong(timestamp, obj, "timestampNumber");
    EXPECT_EQ(timestamp, 1776977716001LL);

    int integer = 0;
    service.parseInt(integer, obj, "intValue");
    EXPECT_EQ(integer, 123);
}

TEST(DealServiceHelpersTest, RejectsMissingInvalidAndOverflowingFields)
{
    const DummyDealService service;
    const json::object obj = parseObject(R"({
        "badDecimal": 10.5,
        "badTimestamp": "123x",
        "hugeInteger": 9223372036854775808,
        "hugeInt": 2147483648
    })");

    std::string text = "unchanged";
    service.parseString(text, obj, "missing", true);
    EXPECT_EQ(text, "unchanged");
    EXPECT_THROW(service.parseString(text, obj, "missing"), std::runtime_error);

    Decimal decimal;
    EXPECT_THROW(service.parseDecimal(decimal, obj, "badDecimal"), std::runtime_error);

    long long timestamp = 0;
    EXPECT_THROW(service.parseLongLong(timestamp, obj, "badTimestamp"), std::runtime_error);
    EXPECT_THROW(service.parseLongLong(timestamp, obj, "hugeInteger"), std::runtime_error);

    int integer = 0;
    EXPECT_THROW(service.parseInt(integer, obj, "hugeInt"), std::runtime_error);
}
