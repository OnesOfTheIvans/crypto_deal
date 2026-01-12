#ifndef BINANCE_DEAL_SERVICE_H
#define BINANCE_DEAL_SERVICE_H

#include "DealService.hpp"
#include "ExchangerType.hpp"
#include "OrderOperation.hpp"
#include "OrderType.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/json.hpp>

#include <mutex>
#include <string>

namespace json = boost::json;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;

class BinanceDealService : public DealService
{
  private:
    mutable std::mutex balanceMutex;
    flat_map<std::string, AssetBalance> balances;

    std::string createQuery(const std::string &baseAsset,
                            const std::string &quoteAsset,
                            const binance::OrderOperation &operation,
                            const binance::OrderType &type,
                            double quantity);

    flat_map<std::string, std::string> createHeaders(const std::string &apiKey);

    bool sendOrder(const std::string &query, const flat_map<std::string, std::string> &headers);

    double parseAmount(const json::object &o, const char *key);

    void updateBalanceCache(const std::string &asset, double free, double locked);

    void handleUserStreamMessage(const std::string &msg);

    std::string buildUserStreamSubscribeRequestJson();

  public:
    BinanceDealService(const std::string &host,
                       const std::string &apiKey,
                       const std::string &secretKey,
                       const std::string &websocketHost,
                       const int recvWindow = 5000)
        : DealService(host, apiKey, secretKey, websocketHost, recvWindow, ExchangerType::BINANCE)
    {}

    bool buyCrypto(const std::string &baseAsset, const std::string &quoteAsset, double quantity) override;

    bool sellCrypto(const std::string &baseAsset, const std::string &quoteAsset, double quantity) override;

    flat_map<std::string, AssetBalance> getBalances() const override;

    std::optional<AssetBalance> getBalance(const std::string &asset) const override;

    void startUserStream();
};

#endif