#ifndef BYBIT_DEAL_SERVICE_H
#define BYBIT_DEAL_SERVICE_H

#include "DealService.hpp"
#include "ExchangerType.hpp"
#include "OrderCategory.hpp"
#include "OrderOperation.hpp"
#include "OrderType.hpp"
#include "common/OcoInfo.hpp"
#include "common/OrderListQuery.hpp"
#include "common/PlaceOcoRequest.hpp"
#include "common/SymbolInfo.hpp"

#include "common/OrderQuery.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/json.hpp>

#include <chrono>
#include <map>
#include <optional>
#include <string>

using msec = std::chrono::milliseconds::rep;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;
namespace json = boost::json;

class BybitDealService : public DealService
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
    bool timeSynced = false;

    struct BybitOcoGroup
    {
        std::string groupId;
        OrderQuery takeProfit;
        OrderQuery stopLeg;
        std::string tpOrderLinkId;
        std::string slOrderLinkId;
        bool closing = false;
        std::string lastError;
    };

    mutable std::mutex ocoMutex;
    flat_map<std::string, BybitOcoGroup> ocoGroups;
    flat_map<std::string, std::string> ocoLegToGroup;

    Decimal parseAmount(const boost::json::object &jsonObject, const char *key);

    AssetBalance parseBalance(const boost::json::object &coinObject);

    void updateBalanceCache(const std::string &asset, Decimal free, Decimal locked);

    std::string createBody(const std::string &baseAsset,
                           const std::string &quoteAsset,
                           const bybit::OrderCategory &category,
                           const bybit::OrderOperation &operation,
                           const bybit::OrderType &type,
                           Decimal quantity,
                           Decimal stepSize = Decimal{});

    OrderInfo createOrderInfo(const json::object &result,
                              const PlaceOrderRequest &request,
                              const std::string &side,
                              const std::string &type,
                              msec timestamp);

    OrderInfo createOrderInfo(const json::object &result, const OrderQuery &request, msec timestamp);

    OrderInfo createDetailedOrderInfo(const json::object &orderObj,
                                      const OrderQuery &request,
                                      const std::string &category,
                                      msec timestamp);

    SymbolInfo createSymbolInfo(const json::object &instrument, const std::string &symbol);

    flat_map<std::string, std::string>
    createHeaders(const std::string &apiKey, const std::string &signature, const msec &timestamp);

    std::string getSignature(const std::string &query, const msec &timestamp);

    std::string sendOrder(const std::string &query, const flat_map<std::string, std::string> &headers);

    bool bybitResponseOk(const std::string &response, std::string *errOut);

    void handleUserStreamMessage(const std::string &msg);

    void handleWalletUpdate(const boost::json::object &root);
    void handleOrderUpdate(const boost::json::object &root);
    void processOcoUpdate(const std::string &orderLinkId);

    void setStreamStatus(StreamStatus status);
    void setStreamError(const std::string &error);

    long long getServerTime();
    void syncTime();
    long long getTimestamp();

    void refreshBalancesFromRest(const std::string &accountType,
                                 const std::optional<std::string> &coinFilter = std::nullopt);

    void ensureBalancesSeeded(const std::optional<std::string> &coinFilter = std::nullopt);
    void ensureBalancesSeeded(const std::string &coinFilter);

    bool capMarketQtyByBalance(bool isBuy,
                               const std::string &baseAsset,
                               const std::string &quoteAsset,
                               const SymbolInfo &symbolInfo,
                               Decimal lastPrice,
                               Decimal &qtyInBase,
                               std::string &reason);

  public:
    BybitDealService(const std::string &host,
                     const std::string &apiKey,
                     const std::string &secretKey,
                     const std::string &websocketHost,
                     const int recvWindow = 5000)
        : DealService(host, apiKey, secretKey, websocketHost, recvWindow, ExchangerType::BYBIT)
    {}

    OrderInfo buyCrypto(const std::string &baseAsset, const std::string &quoteAsset, Decimal quantity) override;

    OrderInfo sellCrypto(const std::string &baseAsset, const std::string &quoteAsset, Decimal quantity) override;

    void waitUntilOrderFilled(const std::string &symbol, const std::string &orderId) override;

    Decimal getTickerPrice(const std::string &symbol);

    OrderInfo placeOrder(const PlaceOrderRequest &request) override;

    OrderInfo cancelOrder(const OrderQuery &request) override;

    OrderInfo getOrder(const OrderQuery &request) override;

    SymbolInfo getSymbolInfo(const std::string &symbol, const std::string &category = "spot") override;

    OcoInfo placeOco(const PlaceOcoRequest &request) override;

    OcoInfo cancelOco(const OrderListQuery &request) override;

    flat_map<std::string, AssetBalance> getBalances() const override;

    std::optional<AssetBalance> getBalance(const std::string &asset) const override;

    void startUserStream() override;

    void stopUserStream() override;

    StreamStatus getUserStreamStatus() const override;
    std::string getUserStreamLastError() const override;

    bool cancelAllOpenOrders(const std::string &symbol, const std::string &category) override;

    flat_map<std::string, AssetBalance> getBalancesRest() override;
};

#endif