#ifndef TEST_DEAL_SERVICE_H
#define TEST_DEAL_SERVICE_H

#include "DealService.hpp"
#include "common/DecimalConverter.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class TestDealService : public DealService
{
  public:
    using TradablePairLoader = std::function<std::vector<TradablePair>()>;
    using SymbolInfoLoader = std::function<SymbolInfo(const std::string &)>;

  private:
    TradablePairLoader tradablePairLoader;
    SymbolInfoLoader symbolInfoLoader;
    std::atomic<std::size_t> tradablePairRequestCount;
    std::atomic<std::size_t> symbolInfoRequestCount;

    static SymbolInfo createDefaultSymbolInfo(const std::string &symbol)
    {
        SymbolInfo symbolInfo;
        symbolInfo.symbol = symbol;
        symbolInfo.status = "Trading";
        symbolInfo.baseAsset = "BTC";
        symbolInfo.quoteAsset = "USDT";
        symbolInfo.tickSize = DecimalConverter::parseDecimal("0.01");
        symbolInfo.stepSize = DecimalConverter::parseDecimal("0.001");
        symbolInfo.minQty = DecimalConverter::parseDecimal("0.001");
        symbolInfo.maxQty = DecimalConverter::parseDecimal("100");
        symbolInfo.minPrice = DecimalConverter::parseDecimal("0.01");
        symbolInfo.maxPrice = DecimalConverter::parseDecimal("1000000");
        symbolInfo.minNotional = DecimalConverter::parseDecimal("10");
        symbolInfo.qtyPrecision = 3;
        symbolInfo.pricePrecision = 2;
        return symbolInfo;
    }

  public:
    explicit TestDealService(ExchangerType exchangerType,
                             TradablePairLoader tradablePairLoader = {},
                             SymbolInfoLoader symbolInfoLoader = {})
        : DealService("host", "api", "secret", "ws", 5000, exchangerType),
          tradablePairLoader(std::move(tradablePairLoader)), symbolInfoLoader(std::move(symbolInfoLoader)),
          tradablePairRequestCount(0), symbolInfoRequestCount(0)
    {}

    std::size_t getTradablePairRequestCount() const
    {
        return tradablePairRequestCount.load();
    }

    std::size_t getSymbolInfoRequestCount() const
    {
        return symbolInfoRequestCount.load();
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

    OrderInfo waitUntilOcoOrderFilled(const OcoInfo &) override
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

    SymbolInfo getSymbolInfo(const std::string &symbol, OrderCategory = OrderCategory::SPOT) override
    {
        ++symbolInfoRequestCount;
        return symbolInfoLoader ? symbolInfoLoader(symbol) : createDefaultSymbolInfo(symbol);
    }

    std::vector<TradablePair> getTradablePairs() override
    {
        ++tradablePairRequestCount;
        return tradablePairLoader ? tradablePairLoader() : std::vector<TradablePair>{};
    }

    Decimal ceilQuantityToStep(const std::string &symbol,
                               Decimal quantity,
                               OrderCategory category = OrderCategory::SPOT) override
    {
        return DecimalConverter::ceilToStep(quantity, getSymbolInfo(symbol, category).stepSize);
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

#endif
