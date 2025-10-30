#ifndef BYBIT_DEAL_SERVICE_H
#define BYBIT_DEAL_SERVICE_H

#include "../DealService.hpp"
#include "OrderOperation.hpp"
#include "OrderType.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/container/flat_map.hpp>

#include <string>

class BybitDealService: public DealService {
private:
    std::string createQuery(const std::string& baseAsset, const std::string& quoteAsset,
        const bybit::OrderOperation& operation, const bybit::OrderType& type, int quantity);

    boost::container::flat_map<std::string, std::string> createHeaders(const std::string& apiKey, const std::string& signature);
    
    std::string getSignature(const std::string& query);

    bool sendOrder(const std::string& query, const boost::container::flat_map<std::string, std::string>& headers);
public:
    BybitDealService(const std::string& host, const std::string& apiKey, const std::string& secretKey, const int recvWindow = 5000):
        DealService(host, apiKey, secretKey, recvWindow) {}

    bool buyCrypto(const std::string& baseAsset, const std::string& quoteAsset, int quantity) override;

    bool sellCrypto(const std::string& baseAsset, const std::string& quoteAsset, int quantity) override;
};

#endif