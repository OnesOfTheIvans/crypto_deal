#ifndef BINANCE_DEAL_SERVICE_H
#define BINANCE_DEAL_SERVICE_H

#include "DealService.hpp"
#include "ExchangerType.hpp"
#include "common/domain/OcoInfo.hpp"
#include "common/domain/OrderInfo.hpp"
#include "common/domain/OrderListQuery.hpp"
#include "common/domain/OrderOperation.hpp"
#include "common/domain/OrderType.hpp"
#include "common/domain/PlaceOcoRequest.hpp"
#include "common/domain/SymbolInfo.hpp"
#include "domain/AccountBalanceDto.hpp"
#include "domain/OcoDto.hpp"
#include "domain/OrderDto.hpp"
#include "domain/StreamBalanceDto.hpp"
#include "domain/SymbolDto.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/json.hpp>

#include "common/domain/OrderQuery.hpp"

#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace json = boost::json;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;
using WebsocketStream = boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;

class BinanceDealService : public DealService
{
  private:
    mutable std::mutex balanceMutex;
    flat_map<std::string, AssetBalance> balances;

    std::map<std::string, SymbolInfo> symbolInfoCache;
    std::mutex symbolInfoMutex;

    std::mutex userWebsocketMutex;
    std::shared_ptr<WebsocketStream> userWebsocketStream;

    StreamStatus streamStatus = StreamStatus::STOPPED;
    std::string streamLastError;
    mutable std::mutex streamStatusMutex;

    long long serverTimeOffset = 0;
    std::atomic<long long> lastSyncMonoMs{0};
    std::mutex timeSyncMutex;

    void setRequestParameters(boost::urls::url &url, const PlaceOrderRequest &request, const SymbolInfo &info);

    void setRequestParameters(boost::urls::url &url, const OrderQuery &request);

    void setRequestParameters(boost::urls::url &url, const PlaceOcoRequest &request);

    void setRequestParameters(boost::urls::url &url, const OrderListQuery &request);

    void setRequestParameters(boost::urls::url &url);

    void setRequestParameters(boost::urls::url &url, const std::string &symbol, bool isPrivate);

    void signUrl(boost::urls::url &url);

    Decimal getTickerPrice(const std::string &symbol);

    bool isQuantityStepValid(Decimal quantity, Decimal stepSize) const;

    std::optional<std::string> validateQuantity(Decimal quantity, Decimal price, const SymbolInfo &info) const;

    void validatePlaceOrderRequest(const PlaceOrderRequest &request) const;

    flat_map<std::string, std::string> createHeaders(const std::string &apiKey);

    json::value parseAndValidate(const std::string &response);

    void checkCancelAllOpenOrdersResult(const std::string &response);

    void updateBalanceCache(const std::string &asset, Decimal free, Decimal locked);

    void updateCache(const std::vector<binance::StreamBalanceDto> &balances);

    void updateCache(const std::vector<binance::AccountBalanceDto> &balances);

    void handleUserStreamMessage(const std::string &msg);

    std::string buildUserStreamSubscribeRequestJson();

    void prepareUserStreamThread();

    std::shared_ptr<WebsocketStream> prepareUserWebsocketStream();

    OrderInfo createOrderInfo(const binance::OrderDto &order);

    SymbolInfo createSymbolInfo(const binance::SymbolDto &symbol);

    void processSymbolFilters(SymbolInfo &info, const binance::FilterDto &filter);

    OcoInfo createOcoInfo(const binance::OcoDto &oco);

    void setStreamStatus(StreamStatus status);

    void setStreamError(const std::string &error);

    long long getServerTime();

    bool isTimeSyncRecent() const;

    void syncTime();

    long long getServerTimestamp();

    void handleUserStreamSubscriptionResponse(WebsocketStream &websocketStream);

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

    Decimal
    ceilQuantityToStep(const std::string &symbol, Decimal quantity, const std::string &category = "spot") override;

    OcoInfo placeOco(const PlaceOcoRequest &request) override;

    OcoInfo cancelOco(const OrderListQuery &request) override;

    void stopUserStream() override;

    StreamStatus getUserStreamStatus() const override;

    std::string getUserStreamLastError() const override;

    void cancelAllOpenOrders(const std::string &symbol, const std::string &) override;

    flat_map<std::string, AssetBalance> getBalancesRest() override;
};

#endif
