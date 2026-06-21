#ifndef BYBIT_DEAL_SERVICE_H
#define BYBIT_DEAL_SERVICE_H

#include "DealService.hpp"
#include "ExchangerType.hpp"
#include "common/domain/OcoInfo.hpp"
#include "common/domain/OrderListQuery.hpp"
#include "common/domain/OrderOperation.hpp"
#include "common/domain/OrderType.hpp"
#include "common/domain/PlaceOcoRequest.hpp"
#include "common/domain/SymbolInfo.hpp"
#include "domain/CoinBalanceDto.hpp"
#include "domain/InstrumentDto.hpp"
#include "domain/OrderDto.hpp"
#include "domain/OrderPriceLimitDto.hpp"
#include "domain/OrderResponseDto.hpp"
#include "domain/OrderResultDto.hpp"
#include "domain/StreamMessageDto.hpp"
#include "domain/StreamOrderMessageDto.hpp"

#include "common/domain/OrderQuery.hpp"

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
using WebsocketStream = boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;
namespace json = boost::json;

namespace bybit {
    struct WalletBalanceResponseDto;
}

class BybitDealService : public DealService
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

    AssetBalance parseBalance(const bybit::CoinBalanceDto &coin);

    void updateBalanceCache(const std::string &asset, Decimal free, Decimal locked);

    OrderInfo createOrderInfo(const bybit::OrderResultDto &result, const PlaceOrderRequest &request, msec timestamp);

    OrderInfo createOrderInfo(const bybit::OrderResultDto &result, const OrderQuery &request, msec timestamp);

    OrderInfo createOrderInfo(const bybit::OrderDto &order, const OrderQuery &request, msec timestamp);

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

    std::optional<std::string> cancelOcoGroupOrder(const OrderQuery &order, OrderInfo &cancelInfo);

    std::optional<std::string> cancelOcoOtherLegFromUpdate(const OrderQuery &order);

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

    void handleWalletUpdate(const bybit::StreamMessageDto &message);

    void handleOrderUpdate(const bybit::StreamOrderMessageDto &message);

    void processOcoUpdate(const std::string &orderLinkId);

    void setStreamStatus(StreamStatus status);

    void setStreamError(const std::string &error);

    void syncTime();

    long long getServerTimestamp();

    void refreshBalancesCache(const std::string &accountType,
                              const std::optional<std::string> &coinFilter = std::nullopt);

    void tryRefreshBalancesCache(const std::string &accountType,
                                 const std::optional<std::string> &coinFilter = std::nullopt);

    void processCoins(const bybit::WalletBalanceResponseDto &responseDto);

    bool isQuantityStepValid(Decimal quantity, Decimal stepSize) const;

    std::optional<std::string> validateQuantity(Decimal quantity, Decimal price, const SymbolInfo &symbolInfo) const;

    std::optional<std::string> validateNotional(Decimal notional, const SymbolInfo &symbolInfo) const;

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

    void waitUntilOrderFilled(const std::string &symbol, const std::string &orderId) override;

    Decimal getTickerPrice(const std::string &symbol);

    OrderInfo placeOrder(const PlaceOrderRequest &request) override;

    OrderInfo cancelOrder(const OrderQuery &request) override;

    OrderInfo getOrder(const OrderQuery &request) override;

    SymbolInfo getSymbolInfo(const std::string &symbol, OrderCategory category = OrderCategory::SPOT) override;

    Decimal ceilQuantityToStep(const std::string &symbol,
                               Decimal quantity,
                               OrderCategory category = OrderCategory::SPOT) override;

    OcoInfo placeOco(const PlaceOcoRequest &request) override;

    OcoInfo cancelOco(const OrderListQuery &request) override;

    flat_map<std::string, AssetBalance> getBalances() const override;

    std::optional<AssetBalance> getBalance(const std::string &asset) const override;

    void startUserStream() override;

    void stopUserStream() override;

    StreamStatus getUserStreamStatus() const override;

    std::string getUserStreamLastError() const override;

    void cancelAllOpenOrders(const std::string &symbol, OrderCategory category) override;

    flat_map<std::string, AssetBalance> getBalancesRest() override;
};

#endif
