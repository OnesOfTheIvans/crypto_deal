#ifndef BYBIT_DEAL_SERVICE_H
#define BYBIT_DEAL_SERVICE_H

#include "DealService.hpp"
#include "ExchangerType.hpp"
#include "OrderCategory.hpp"
#include "OrderOperation.hpp"
#include "OrderType.hpp"
#include "common/SymbolInfo.hpp"
#include "common/OcoInfo.hpp"
#include "common/PlaceOcoRequest.hpp"
#include "common/OrderListQuery.hpp"

#include "common/OrderQuery.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/json.hpp>

#include <chrono>
#include <string>

using msec = std::chrono::milliseconds::rep;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;
namespace json = boost::json;

class BybitDealService : public DealService
{
  private:
    mutable std::mutex balanceMutex;
    flat_map<std::string, AssetBalance> balances;

    double parseAmount(const boost::json::object &jsonObject, const char *key);

    AssetBalance parseBalance(const boost::json::object &coinObject);

    void updateBalanceCache(const std::string &asset, double free, double locked);

    msec getTimestamp();

    std::string createBody(const std::string &baseAsset,
                           const std::string &quoteAsset,
                           const bybit::OrderCategory &category,
                           const bybit::OrderOperation &operation,
                           const bybit::OrderType &type,
                           double quantity);

    OrderInfo createOrderInfo(const json::object &result,
                              const PlaceOrderRequest &request,
                              const std::string &side,
                              const std::string &type,
                              msec timestamp);

    OrderInfo createOrderInfo(const json::object &result,
                              const OrderQuery &request,
                              msec timestamp);

    OrderInfo createDetailedOrderInfo(const json::object &orderObj,
                                      const OrderQuery &request,
                                      const std::string &category,
                                      msec timestamp);

    SymbolInfo createSymbolInfo(const json::object &instrument, const std::string &symbol);

    flat_map<std::string, std::string>
    createHeaders(const std::string &apiKey, const std::string &signature, const msec &timestamp);

    std::string getSignature(const std::string &query, const msec &timestamp);

    bool sendOrder(const std::string &query, const flat_map<std::string, std::string> &headers);

    void handleWalletStreamMessage(const std::string &msg);

  public:
    BybitDealService(const std::string &host,
                     const std::string &apiKey,
                     const std::string &secretKey,
                     const std::string &websocketHost,
                     const int recvWindow = 5000)
        : DealService(host, apiKey, secretKey, websocketHost, recvWindow, ExchangerType::BYBIT)
    {}

    bool buyCrypto(const std::string &baseAsset, const std::string &quoteAsset, double quantity) override;

    bool sellCrypto(const std::string &baseAsset, const std::string &quoteAsset, double quantity) override;

    OrderInfo placeOrder(const PlaceOrderRequest &request) override;
    
    OrderInfo cancelOrder(const OrderQuery &request) override;

    OrderInfo getOrder(const OrderQuery &request) override;

    SymbolInfo getSymbolInfo(const std::string& symbol, const std::string& category = "spot") override;

    OcoInfo placeOco(const PlaceOcoRequest& request) override;

    OcoInfo cancelOco(const OrderListQuery& request) override;

    flat_map<std::string, AssetBalance> getBalances() const override;

    std::optional<AssetBalance> getBalance(const std::string &asset) const override;

    void startUserStream() override;

    void stopUserStream() override;
};

#endif