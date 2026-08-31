#ifndef TEST_DEAL_SERVICE_H
#define TEST_DEAL_SERVICE_H

#include "DealService.hpp"
#include "common/DecimalConverter.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class TestDealService : public DealService
{
  public:
    using TradablePairLoader = std::function<std::vector<TradablePair>()>;
    using SymbolInfoLoader = std::function<SymbolInfo(const std::string &)>;
    using BalanceLoader = std::function<flat_map<std::string, AssetBalance>()>;
    using UserStreamStarter = std::function<void(TestDealService &)>;

  private:
    TradablePairLoader tradablePairLoader;
    SymbolInfoLoader symbolInfoLoader;
    BalanceLoader balanceLoader;
    UserStreamStarter userStreamStarter;
    mutable std::mutex balanceMutex;
    flat_map<std::string, AssetBalance> balances;
    mutable std::mutex streamMutex;
    StreamStatus streamStatus = StreamStatus::STOPPED;
    std::string streamError;
    std::atomic<std::size_t> tradablePairRequestCount;
    std::atomic<std::size_t> symbolInfoRequestCount;
    std::atomic<std::size_t> balanceRequestCount;
    std::atomic<std::size_t> userStreamStartCount;
    std::atomic<std::size_t> userStreamStopCount;

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
                             SymbolInfoLoader symbolInfoLoader = {},
                             BalanceLoader balanceLoader = {},
                             UserStreamStarter userStreamStarter = {})
        : DealService("host", "api", "secret", "ws", 5000, exchangerType),
          tradablePairLoader(std::move(tradablePairLoader)), symbolInfoLoader(std::move(symbolInfoLoader)),
          balanceLoader(std::move(balanceLoader)), userStreamStarter(std::move(userStreamStarter)),
          tradablePairRequestCount(0), symbolInfoRequestCount(0), balanceRequestCount(0), userStreamStartCount(0),
          userStreamStopCount(0)
    {}

    std::size_t getTradablePairRequestCount() const
    {
        return tradablePairRequestCount.load();
    }

    std::size_t getSymbolInfoRequestCount() const
    {
        return symbolInfoRequestCount.load();
    }

    std::size_t getBalanceRequestCount() const
    {
        return balanceRequestCount.load();
    }

    std::size_t getUserStreamStartCount() const
    {
        return userStreamStartCount.load();
    }

    std::size_t getUserStreamStopCount() const
    {
        return userStreamStopCount.load();
    }

    void publishBalanceUpdate(const AssetBalance &balance)
    {
        {
            std::lock_guard<std::mutex> lock(balanceMutex);
            balances[balance.asset] = balance;
        }
        notifyBalanceCacheChanged();
    }

    void publishUserStreamStatus(StreamStatus status, std::string error = {})
    {
        {
            std::lock_guard<std::mutex> lock(streamMutex);
            streamStatus = status;
            streamError = std::move(error);
        }
        notifyUserStreamStatusChanged();
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
        std::lock_guard<std::mutex> lock(balanceMutex);
        return balances;
    }

    std::optional<AssetBalance> getBalance(const std::string &asset) const override
    {
        std::lock_guard<std::mutex> lock(balanceMutex);
        auto balance = balances.find(asset);
        return balance == balances.end() ? std::nullopt : std::optional<AssetBalance>(balance->second);
    }

    void startUserStream() override
    {
        ++userStreamStartCount;
        if (userStreamStarter)
        {
            userStreamStarter(*this);
            return;
        }
        publishUserStreamStatus(StreamStatus::CONNECTED);
    }

    void stopUserStream() override
    {
        ++userStreamStopCount;
        publishUserStreamStatus(StreamStatus::STOPPED);
    }

    StreamStatus getUserStreamStatus() const override
    {
        std::lock_guard<std::mutex> lock(streamMutex);
        return streamStatus;
    }

    std::string getUserStreamLastError() const override
    {
        std::lock_guard<std::mutex> lock(streamMutex);
        return streamError;
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
        ++balanceRequestCount;
        flat_map<std::string, AssetBalance> loadedBalances =
            balanceLoader ? balanceLoader() : flat_map<std::string, AssetBalance>{};
        {
            std::lock_guard<std::mutex> lock(balanceMutex);
            balances = loadedBalances;
        }
        return loadedBalances;
    }
};

#endif
