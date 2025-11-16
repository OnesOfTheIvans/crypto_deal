#ifndef BYBIT_DEAL_SERVICE_H
#define BYBIT_DEAL_SERVICE_H

#include "../DealService.hpp"
#include "OrderCategory.hpp"
#include "OrderOperation.hpp"
#include "OrderType.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/container/flat_map.hpp>

#include <chrono>
#include <string>

using msec = std::chrono::milliseconds::rep;
template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;

class BybitDealService : public DealService
{
  private:
    msec getTimestamp();

    std::string createBody(const std::string &baseAsset,
                           const std::string &quoteAsset,
                           const bybit::OrderCategory &category,
                           const bybit::OrderOperation &operation,
                           const bybit::OrderType &type,
                           int quantity);

    flat_map<std::string, std::string>
    createHeaders(const std::string &apiKey, const std::string &signature, const msec &timestamp);

    std::string getSignature(const std::string &query, const msec &timestamp);

    bool sendOrder(const std::string &query, const flat_map<std::string, std::string> &headers);

  public:
    BybitDealService(const std::string &host,
                     const std::string &apiKey,
                     const std::string &secretKey,
                     const int recvWindow = 5000)
        : DealService(host, apiKey, secretKey, recvWindow)
    {}

    bool buyCrypto(const std::string &baseAsset, const std::string &quoteAsset, int quantity) override;

    bool sellCrypto(const std::string &baseAsset, const std::string &quoteAsset, int quantity) override;
};

#endif