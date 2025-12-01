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

#include <string>

template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;

class BinanceDealService : public DealService
{
  private:
    std::string createQuery(const std::string &baseAsset,
                            const std::string &quoteAsset,
                            const binance::OrderOperation &operation,
                            const binance::OrderType &type,
                            int quantity);

    flat_map<std::string, std::string> createHeaders(const std::string &apiKey);

    bool sendOrder(const std::string &query, const flat_map<std::string, std::string> &headers);

  public:
    BinanceDealService(const std::string &host,
                       const std::string &apiKey,
                       const std::string &secretKey,
                       const int recvWindow = 5000)
        : DealService(host, apiKey, secretKey, recvWindow, ExchangerType::BINANCE)
    {}

    bool buyCrypto(const std::string &baseAsset, const std::string &quoteAsset, int quantity) override;

    bool sellCrypto(const std::string &baseAsset, const std::string &quoteAsset, int quantity) override;
};

#endif