#ifndef DEAL_SERVICE_H
#define DEAL_SERVICE_H

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>

#include <string>

class DealService {
private:
    boost::asio::io_context ioc;
    boost::asio::ssl::context ctx;
    long recvWindow;
    std::string host;
    std::string apiKey;
    std::string secretKey;
public:
    DealService(std::string host, std::string apiKey, std::string secretKey, int recvWindow = 5000):
        ioc(), ctx(boost::asio::ssl::context::tls_client), recvWindow(recvWindow), host(host), apiKey(apiKey), secretKey(secretKey) {}

    bool buyCrypto(const std::string& baseAsset, const std::string& quoteAsset, int quantity);
    std::string hmac_sha256(const std::string& key, const std::string& data) const;
};

#endif