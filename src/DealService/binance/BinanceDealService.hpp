#ifndef BINANCE_DEAL_SERVICE_H
#define BINANCE_DEAL_SERVICE_H

#include "DealService.hpp"
#include "ExchangerType.hpp"
#include "common/domain/OcoInfo.hpp"
#include "common/domain/OrderInfo.hpp"
#include "common/domain/OrderListQuery.hpp"
#include "common/domain/OrderOperation.hpp"
#include "common/domain/OrderQuery.hpp"
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

#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace json = boost::json;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;
using WebsocketStream = boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;

namespace binance {
    struct ExecutionReportEventDto;
    struct ListStatusEventDto;
    struct OutboundAccountPositionEventDto;
}

class BinanceDealService : public DealService
{
  private:
    using OrderKey = std::pair<std::string, std::string>;
    using PendingOrderUpdate = std::optional<OrderInfo>;

    class PendingOrderRegistration
    {
      private:
        BinanceDealService &service;
        OrderKey orderKey;

      public:
        PendingOrderRegistration(BinanceDealService &service, const std::string &symbol, const std::string &orderId);

        ~PendingOrderRegistration();

        PendingOrderRegistration(const PendingOrderRegistration &) = delete;

        PendingOrderRegistration &operator=(const PendingOrderRegistration &) = delete;
    };

    mutable std::mutex balanceMutex;
    flat_map<std::string, AssetBalance> balances;

    std::map<std::string, SymbolInfo> symbolInfoCache;
    std::mutex symbolInfoMutex;

    std::mutex userWebsocketMutex;
    std::shared_ptr<WebsocketStream> userWebsocketStream;

    StreamStatus streamStatus = StreamStatus::STOPPED;
    std::string streamLastError;
    mutable std::mutex streamStatusMutex;

    std::map<OrderKey, PendingOrderUpdate> pendingOrderWaits;
    std::mutex pendingOrderWaitsMutex;
    std::condition_variable orderUpdateCondition;
    std::mutex streamLifecycleMutex;

    std::atomic<long long> serverTimeOffset{0};
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

    void validatePlaceOcoRequest(const PlaceOcoRequest &request) const;

    void checkBalance(const PlaceOrderRequest &request, const SymbolInfo &info, Decimal validationPrice);

    void checkBalance(const PlaceOcoRequest &request, const SymbolInfo &info);

    flat_map<std::string, std::string> createHeaders(const std::string &apiKey);

    json::value parseAndValidate(const std::string &response);

    void checkCancelAllOpenOrdersResult(const std::string &response);

    void updateBalanceCache(const std::string &asset, Decimal free, Decimal locked);

    void updateCache(const std::vector<binance::StreamBalanceDto> &balances);

    void updateCache(const std::vector<binance::AccountBalanceDto> &balances);

    void handleUserStreamMessage(const std::string &msg);

    void handleAccountPositionUpdate(const binance::OutboundAccountPositionEventDto &event);

    void handleExecutionReport(const binance::ExecutionReportEventDto &event);

    void handleListStatusUpdate(const binance::ListStatusEventDto &event);

    std::string buildUserStreamSubscribeRequestJson();

    void prepareUserStreamThread();

    std::shared_ptr<WebsocketStream> prepareUserWebsocketStream();

    void startUserStreamConcurrent();

    void stopUserStreamConcurrent();

    OrderInfo createOrderInfo(const binance::OrderDto &order);

    SymbolInfo createSymbolInfo(const binance::SymbolDto &symbol);

    void processSymbolFilters(SymbolInfo &info, const binance::FilterDto &filter);

    OcoInfo createOcoInfo(const binance::OcoDto &oco);

    static OrderKey getOrderKey(const std::string &symbol, const std::string &orderId);

    static bool isOrderFilled(const std::string &status);

    static bool isOrderTerminal(const std::string &status);

    void registerPendingOrder(const OrderKey &orderKey);

    void unregisterPendingOrder(const OrderKey &orderKey);

    const PendingOrderUpdate &getPendingOrderUpdate(const OrderKey &orderKey) const
    {
        return pendingOrderWaits.at(orderKey);
    }

    void publishOrderUpdate(const OrderInfo &orderInfo);

    void ensureUserStreamConnected();

    void reconcileOrder(const OrderInfo &orderInfo);

    void prepareOrderSubscription(const std::string &symbol, const std::string &orderId);

    PendingOrderUpdate waitForOrderTerminalStatus(const std::string &symbol, const std::string &orderId);

    OrderInfo
    processOrderUpdate(const std::string &symbol, const std::string &orderId, const PendingOrderUpdate &pendingUpdate);

    void prepareOcoSubscription(const OcoInfo &ocoInfo);

    void waitForOcoTerminalStatus(const OcoInfo &ocoInfo,
                                  PendingOrderUpdate &takeProfitUpdate,
                                  PendingOrderUpdate &stopLossUpdate);

    OrderInfo processOcoOrdersUpdate(const OcoInfo &ocoInfo,
                                     const PendingOrderUpdate &takeProfitUpdate,
                                     const PendingOrderUpdate &stopLossUpdate);

    OrderInfo reconcileOcoAfterUnfilledTerminalChild(const OcoInfo &ocoInfo, bool isTakeProfitFailed);

    std::optional<std::string> reconcileOcoSiblingOrder(const OcoInfo &ocoInfo, bool isTakeProfitFailed);

    std::optional<OrderInfo> getFilledOcoOrder(const OcoInfo &ocoInfo);

    [[noreturn]] void throwOcoWaitAfterReconciliationFailure(const OcoInfo &ocoInfo,
                                                             const std::optional<std::string> &reconciliationError);

    std::optional<std::string> cancelOcoAfterFailure(const OcoInfo &ocoInfo);

    [[noreturn]] void throwOrderWaitFailure(const OrderInfo &orderInfo) const;

    [[noreturn]] void throwOcoWaitFailure(const OcoInfo &ocoInfo,
                                          const std::string &reason,
                                          const std::optional<std::string> &cleanupError) const;

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

    OrderInfo waitUntilOrderFilled(const std::string &symbol, const std::string &orderId) override;

    OrderInfo waitUntilOcoOrderFilled(const OcoInfo &ocoInfo) override;

    flat_map<std::string, AssetBalance> getBalances() const override;

    std::optional<AssetBalance> getBalance(const std::string &asset) const override;

    void startUserStream() override;

    OrderInfo placeOrder(const PlaceOrderRequest &request) override;

    OrderInfo cancelOrder(const OrderQuery &request) override;

    OrderInfo getOrder(const OrderQuery &request) override;

    SymbolInfo getSymbolInfo(const std::string &symbol, OrderCategory = OrderCategory::SPOT) override;

    Decimal
    ceilQuantityToStep(const std::string &symbol, Decimal quantity, OrderCategory = OrderCategory::SPOT) override;

    OcoInfo placeOco(const PlaceOcoRequest &request) override;

    void cancelOco(const OrderListQuery &request) override;

    void stopUserStream() override;

    StreamStatus getUserStreamStatus() const override;

    std::string getUserStreamLastError() const override;

    void cancelAllOpenOrders(const std::string &symbol, OrderCategory) override;

    flat_map<std::string, AssetBalance> getBalancesRest() override;

    ~BinanceDealService() override;
};

#endif
