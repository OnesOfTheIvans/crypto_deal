#ifndef BYBIT_DEAL_SERVICE_H
#define BYBIT_DEAL_SERVICE_H

#include "DealService.hpp"
#include "ExchangerType.hpp"
#include "OrderCategory.hpp"
#include "OrderOperation.hpp"
#include "OrderType.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/json.hpp>

#include <chrono>
#include <string>

using msec = std::chrono::milliseconds::rep;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;

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

    flat_map<std::string, AssetBalance> getBalances() const override;

    std::optional<AssetBalance> getBalance(const std::string &asset) const override;

    void startWalletStream();
};

#endif