#ifndef BYBIT_DEAL_SERVICE_H
#define BYBIT_DEAL_SERVICE_H

#include "DealService.hpp"
#include "ExchangerType.hpp"
#include "common/domain/OcoInfo.hpp"
#include "common/domain/OrderListQuery.hpp"
#include "common/domain/OrderOperation.hpp"
#include "common/domain/OrderQuery.hpp"
#include "common/domain/OrderType.hpp"
#include "common/domain/PlaceOcoRequest.hpp"
#include "common/domain/SymbolInfo.hpp"
#include "common/domain/TradablePair.hpp"
#include "domain/CoinBalanceDto.hpp"
#include "domain/InstrumentDto.hpp"
#include "domain/OrderDto.hpp"
#include "domain/OrderPriceLimitDto.hpp"
#include "domain/OrderResponseDto.hpp"
#include "domain/OrderResultDto.hpp"
#include "domain/StreamMessageDto.hpp"
#include "domain/StreamOrderMessageDto.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>

using msec = std::chrono::milliseconds::rep;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;
using WebsocketStream = boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;
namespace json = boost::json;

namespace bybit {
    struct WalletBalanceResponseDto;
}

class BybitDealService : public DealService
{
  private:
    using OrderKey = std::pair<std::string, std::string>;

    struct PendingOrderWait
    {
        std::optional<OrderInfo> orderInfo;
        std::optional<std::string> error;
        std::size_t registrationCount = 0;
    };

    class PendingOrderRegistration
    {
      private:
        BybitDealService &service;
        OrderKey orderKey;

      public:
        PendingOrderRegistration(BybitDealService &service, const std::string &symbol, const std::string &orderId);

        ~PendingOrderRegistration();

        PendingOrderRegistration(const PendingOrderRegistration &) = delete;

        PendingOrderRegistration &operator=(const PendingOrderRegistration &) = delete;
    };

    class OcoPlacementRegistration
    {
      private:
        BybitDealService &service;
        std::string groupId;
        std::string takeProfitOrderLinkId;
        std::string stopLossOrderLinkId;
        bool active = true;

      public:
        OcoPlacementRegistration(BybitDealService &service,
                                 std::string groupId,
                                 std::string takeProfitOrderLinkId,
                                 std::string stopLossOrderLinkId);

        ~OcoPlacementRegistration();

        OcoPlacementRegistration(const OcoPlacementRegistration &) = delete;

        OcoPlacementRegistration &operator=(const OcoPlacementRegistration &) = delete;

        void release();
    };

    mutable std::mutex balanceMutex;
    flat_map<std::string, AssetBalance> balances;
    std::mutex balanceRefreshMutex;
    std::string balanceAccountType;

    std::map<std::string, SymbolInfo> symbolInfoCache;
    std::mutex symbolInfoMutex;

    std::mutex userWebsocketMutex;
    std::shared_ptr<WebsocketStream> userWebsocketStream;

    StreamStatus streamStatus = StreamStatus::STOPPED;
    std::string streamLastError;
    mutable std::mutex streamStatusMutex;

    std::map<OrderKey, PendingOrderWait> pendingOrderWaits;
    std::mutex pendingOrderWaitsMutex;
    std::condition_variable_any orderUpdateCondition;
    std::mutex streamLifecycleMutex;

    std::atomic<long long> serverTimeOffset{0};
    std::atomic<long long> lastSyncMonoMs{0};
    std::mutex timeSyncMutex;

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
    std::set<std::string> placingOcoOrderLinkIds;
    std::set<std::string> pendingOcoFilledOrderLinkIds;

    AssetBalance parseBalance(const bybit::CoinBalanceDto &coin);

    void updateBalanceCache(const std::string &asset, Decimal free, Decimal locked);

    OrderInfo createOrderInfo(const bybit::OrderResultDto &result, const PlaceOrderRequest &request, msec timestamp);

    OrderInfo createOrderInfo(const bybit::OrderResultDto &result, const OrderQuery &request, msec timestamp);

    OrderInfo createOrderInfo(const bybit::OrderDto &order, const OrderQuery &request, msec timestamp);

    OrderInfo createOrderInfo(const bybit::StreamOrderDto &order);

    SymbolInfo createSymbolInfo(const bybit::InstrumentDto &instrument, const std::string &symbol);

    OrderInfo createTakeProfitOcoRequest(const PlaceOcoRequest &request, const std::string &takeProfitOrderLinkId);

    OrderInfo createStopLossOcoRequest(const PlaceOcoRequest &request,
                                       const std::string &stopLossOrderLinkId,
                                       const std::string &takeProfitOrderLinkId);

    std::optional<std::string> rollbackOcoRequests(const PlaceOcoRequest &request,
                                                   const std::string &takeProfitOrderLinkId);

    void createOcoGroup(const PlaceOcoRequest &request,
                        const std::string &groupId,
                        const std::string &takeProfitOrderLinkId,
                        const std::string &stopLossOrderLinkId,
                        const OrderInfo &takeProfitOrderInfo,
                        const OrderInfo &stopLossOrderInfo);

    OcoInfo createOcoInfo(const std::string &groupId,
                          const OrderInfo &takeProfitOrderInfo,
                          const OrderInfo &stopLossOrderInfo) const;

    static OrderKey getOrderKey(const std::string &symbol, const std::string &orderId);

    static bool isOrderFilled(const std::string &status);

    static bool isOrderTerminal(const std::string &status);

    void registerPendingOrder(const OrderKey &orderKey);

    void unregisterPendingOrder(const OrderKey &orderKey);

    const PendingOrderWait &getPendingOrderWait(const OrderKey &orderKey) const
    {
        return pendingOrderWaits.at(orderKey);
    }

    PendingOrderWait getPendingOrderWait(const std::string &symbol, const std::string &orderId);

    void publishOrderUpdate(const OrderInfo &orderInfo);

    void publishOrderWaitError(const OrderInfo &orderInfo, const std::string &error);

    void ensureUserStreamConnected();

    void reconcileOrder(const OrderInfo &orderInfo);

    void prepareOrderSubscription(const std::string &symbol, const std::string &orderId);

    PendingOrderWait
    waitForOrderTerminalStatus(const std::string &symbol, const std::string &orderId, std::stop_token stopToken);

    OrderInfo processOrderCancellationUpdate(const std::string &symbol,
                                             const std::string &orderId,
                                             const PendingOrderWait &pendingWait);

    OrderInfo
    processOrderUpdate(const std::string &symbol, const std::string &orderId, const PendingOrderWait &pendingWait);

    void prepareOcoSubscription(const OcoInfo &ocoInfo);

    void waitForOcoTerminalStatus(const OcoInfo &ocoInfo,
                                  PendingOrderWait &takeProfitWait,
                                  PendingOrderWait &stopLossWait,
                                  std::stop_token stopToken);

    void waitForOcoCancellationTerminalStatus(const OcoInfo &ocoInfo,
                                              PendingOrderWait &takeProfitWait,
                                              PendingOrderWait &stopLossWait,
                                              std::stop_token stopToken);

    PendingOrderWait
    waitForOcoSiblingTerminalStatus(const OrderInfo &filledOrder, const OcoInfo &ocoInfo, std::stop_token stopToken);

    OcoWaitResult processOcoOrdersUpdate(const OcoInfo &ocoInfo,
                                         const PendingOrderWait &takeProfitWait,
                                         const PendingOrderWait &stopLossWait,
                                         std::stop_token stopToken);

    OcoWaitResult
    reconcileOcoAfterUnfilledTerminalChild(const OcoInfo &ocoInfo, bool isTakeProfitFailed, std::stop_token stopToken);

    std::optional<std::string> reconcileOcoSiblingOrder(const OcoInfo &ocoInfo, bool isTakeProfitFailed);

    std::optional<OrderInfo> getFilledOcoOrder(const OcoInfo &ocoInfo);

    [[noreturn]] void throwOcoWaitAfterReconciliationFailure(const OcoInfo &ocoInfo,
                                                             const std::optional<std::string> &reconciliationError);

    std::optional<std::string> cancelOcoAfterFailure(const OcoInfo &ocoInfo);

    OcoWaitResult completeOcoWait(const OcoInfo &ocoInfo, const OrderInfo &filledOrder, std::stop_token stopToken);

    OcoInfo
    createTerminalOcoInfo(const OcoInfo &ocoInfo, const OrderInfo &firstOrder, const OrderInfo &secondOrder) const;

    std::optional<OcoInfo> getTerminalOcoInfo(const OcoInfo &ocoInfo);

    OcoInfo processOcoCancellationUpdate(const OcoInfo &ocoInfo,
                                         const PendingOrderWait &takeProfitWait,
                                         const PendingOrderWait &stopLossWait);

    [[noreturn]] void throwOrderWaitFailure(const OrderInfo &orderInfo) const;

    [[noreturn]] void throwOcoWaitFailure(const OcoInfo &ocoInfo,
                                          const std::string &reason,
                                          const std::optional<std::string> &cleanupError) const;

    void clearOcoPlacementRegistration(const std::string &takeProfitOrderLinkId,
                                       const std::string &stopLossOrderLinkId);

    std::optional<std::string> cancelOcoGroupOrder(const OrderQuery &order, OrderInfo &cancelInfo);

    std::optional<std::string> requestOcoOtherLegCancellation(const OrderQuery &order);

    flat_map<std::string, std::string>
    createHeaders(const std::string &apiKey, const std::string &signature, const msec &timestamp);

    std::string getSignature(const std::string &query, const msec &timestamp);

    std::optional<std::string> isResponseStatusOk(const std::string &response);

    json::value parseAndValidate(const std::string &response) const;

    bybit::OrderPriceLimitDto getOrderPriceLimit(const std::string &symbol, OrderCategory category);

    void validateOrderPriceLimit(const PlaceOrderRequest &request);

    void checkBalance(const PlaceOrderRequest &request, const SymbolInfo &info);

    std::string createRequestBody(const PlaceOrderRequest &request, const SymbolInfo &info) const;

    std::string createRequestBody(const OrderQuery &request) const;

    void setRequestParameters(boost::urls::url &url,
                              const std::string &accountType,
                              const std::optional<std::string> &coinFilter);

    void setRequestParameters(boost::urls::url &url, const std::string &symbol, OrderCategory category);

    void setRequestParameters(boost::urls::url &url, const OrderQuery &request, OrderCategory category);

    void handleUserStreamMessage(const std::string &msg);

    void authenticateUserWebsocketStream(WebsocketStream &websocketStream);

    void subscribeUserWebsocketStream(WebsocketStream &websocketStream);

    void prepareUserStreamThread();

    std::shared_ptr<WebsocketStream> prepareUserWebsocketStream();

    void startUserStreamConcurrent();

    void stopUserStreamConcurrent();

    void handleWalletUpdate(const bybit::StreamMessageDto &message);

    void handleOrderUpdate(const bybit::StreamOrderMessageDto &message);

    std::optional<std::string> processOcoUpdate(const std::string &orderLinkId);

    void publishOcoError(const std::string &orderLinkId, const std::string &error);

    void setStreamStatus(StreamStatus status);

    void setStreamError(const std::string &error);

    bool isTimeSyncRecent() const;

    void syncTime();

    long long getServerTimestamp();

    void refreshBalancesCache(const std::string &accountType,
                              const std::optional<std::string> &coinFilter = std::nullopt);

    void processCoins(const bybit::WalletBalanceResponseDto &responseDto);

    std::optional<std::string> validateQuantity(Decimal quantity, Decimal price, const SymbolInfo &symbolInfo) const;

    std::optional<std::string> validateNotional(Decimal notional, const SymbolInfo &symbolInfo) const;

    std::optional<std::string> validatePrice(Decimal price, const SymbolInfo &symbolInfo) const;

    void validatePlaceOrderRequest(const PlaceOrderRequest &request) const;

    void validatePlaceOcoRequest(const PlaceOcoRequest &request) const;

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

    OrderInfo waitUntilOrderFilled(const std::string &symbol, const std::string &orderId) override;

    OrderInfo
    waitUntilOrderFilled(const std::string &symbol, const std::string &orderId, std::stop_token stopToken) override;

    OcoWaitResult waitUntilOcoOrderFilled(const OcoInfo &ocoInfo) override;

    OcoWaitResult waitUntilOcoOrderFilled(const OcoInfo &ocoInfo, std::stop_token stopToken) override;

    Decimal getTickerPrice(const std::string &symbol);

    OrderInfo placeOrder(const PlaceOrderRequest &request) override;

    OrderInfo cancelOrder(const OrderQuery &request) override;

    OrderInfo cancelOrderAndWaitUntilTerminal(const OrderQuery &request) override;

    OrderInfo cancelOrderAndWaitUntilTerminal(const OrderQuery &request, std::stop_token stopToken) override;

    OrderInfo getOrder(const OrderQuery &request) override;

    SymbolInfo getSymbolInfo(const std::string &symbol, OrderCategory category = OrderCategory::SPOT) override;

    std::vector<TradablePair> getTradablePairs() override;

    Decimal ceilQuantityToStep(const std::string &symbol,
                               Decimal quantity,
                               OrderCategory category = OrderCategory::SPOT) override;

    OcoInfo placeOco(const PlaceOcoRequest &request) override;

    void cancelOco(const OrderListQuery &request) override;

    OcoInfo cancelOcoAndWaitUntilTerminal(const OcoInfo &ocoInfo) override;

    OcoInfo cancelOcoAndWaitUntilTerminal(const OcoInfo &ocoInfo, std::stop_token stopToken) override;

    flat_map<std::string, AssetBalance> getBalances() const override;

    std::optional<AssetBalance> getBalance(const std::string &asset) const override;

    void startUserStream() override;

    void stopUserStream() override;

    StreamStatus getUserStreamStatus() const override;

    std::string getUserStreamLastError() const override;

    void cancelAllOpenOrders(const std::string &symbol, OrderCategory category) override;

    flat_map<std::string, AssetBalance> getBalancesRest() override;

    ~BybitDealService() override;
};

#endif
