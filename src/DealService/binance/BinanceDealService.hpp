#ifndef BINANCE_DEAL_SERVICE_H
#define BINANCE_DEAL_SERVICE_H

#include "DealService.hpp"
#include "ExchangerType.hpp"
#include "OrderOperation.hpp"
#include "OrderType.hpp"
#include "common/OcoInfo.hpp"
#include "common/OrderInfo.hpp"
#include "common/OrderListQuery.hpp"
#include "common/PlaceOcoRequest.hpp"
#include "common/SymbolInfo.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/json.hpp>

#include "common/OrderQuery.hpp"

#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace json = boost::json;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;

class BinanceDealService : public DealService
{
  private:
    mutable std::mutex balanceMutex;
    flat_map<std::string, AssetBalance> balances;

    std::map<std::string, SymbolInfo> symbolInfoCache;
    std::mutex symbolInfoMutex;

    using WebsocketStream = boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;
    std::mutex userWebsocketMutex;
    std::shared_ptr<WebsocketStream> userWebsocketStream;

    StreamStatus streamStatus = StreamStatus::STOPPED;
    std::string streamLastError;
    mutable std::mutex streamStatusMutex;

    long long serverTimeOffset = 0;
    std::atomic<long long> lastSyncMonoMs{0};
    std::mutex timeSyncMutex;

    std::string createQuery(const std::string &baseAsset,
                            const std::string &quoteAsset,
                            const binance::OrderOperation &operation,
                            const binance::OrderType &type,
                            Decimal quantity,
                            Decimal stepSize = Decimal{});

    Decimal getTickerPrice(const std::string &symbol);

    Decimal
    calculateSafeQty(const std::string &symbol, Decimal quantity, Decimal price, Decimal stepSize, Decimal minNotional);

    flat_map<std::string, std::string> createHeaders(const std::string &apiKey);

    std::string sendOrder(const std::string &query, const flat_map<std::string, std::string> &headers);

    std::optional<std::string> binanceResponseOk(const std::string &response);

    Decimal parseAmount(const json::object &o, const char *key);

    void updateBalanceCache(const std::string &asset, Decimal free, Decimal locked);

    void handleUserStreamMessage(const std::string &msg);

    std::string buildUserStreamSubscribeRequestJson();

    OrderInfo createOrderInfo(const json::object &obj);

    SymbolInfo createSymbolInfo(const json::object &symbolObject);

    std::string buildOcoQuery(const PlaceOcoRequest &request, long long timestamp);

    std::string buildOcoCancelQuery(const OrderListQuery &request, long long timestamp);

    OcoInfo createOcoInfo(const json::object &object);

    void setStreamStatus(StreamStatus status);
    void setStreamError(const std::string &error);

    long long getServerTime();
    void syncTime();
    long long getTimestamp();

  public:
    BinanceDealService(const std::string &host,
                       const std::string &apiKey,
                       const std::string &secretKey,
                       const std::string &websocketHost,
                       const int recvWindow = 5000)
        : DealService(host, apiKey, secretKey, websocketHost, recvWindow, ExchangerType::BINANCE)
    {}

    OrderInfo buyCrypto(const std::string &baseAsset, const std::string &quoteAsset, Decimal quantity) override;

    OrderInfo sellCrypto(const std::string &baseAsset, const std::string &quoteAsset, Decimal quantity) override;

    void waitUntilOrderFilled(const std::string &symbol, const std::string &orderId) override;

    flat_map<std::string, AssetBalance> getBalances() const override;

    std::optional<AssetBalance> getBalance(const std::string &asset) const override;

    void startUserStream() override;

    OrderInfo placeOrder(const PlaceOrderRequest &request) override;

    OrderInfo cancelOrder(const OrderQuery &request) override;

    OrderInfo getOrder(const OrderQuery &request) override;

    SymbolInfo getSymbolInfo(const std::string &symbol, const std::string &category = "spot") override;

    OcoInfo placeOco(const PlaceOcoRequest &request) override;

    OcoInfo cancelOco(const OrderListQuery &request) override;

    void stopUserStream() override;

    StreamStatus getUserStreamStatus() const override;
    std::string getUserStreamLastError() const override;

    bool cancelAllOpenOrders(const std::string &symbol, const std::string &category) override;

    flat_map<std::string, AssetBalance> getBalancesRest() override;
};

#endif