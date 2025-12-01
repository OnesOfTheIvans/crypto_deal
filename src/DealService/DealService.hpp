#ifndef DEAL_SERVICE_H
#define DEAL_SERVICE_H

#include "ExchangerType.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>

#include <string>

class DealService
{
  protected:
    boost::asio::io_context ioc;
    boost::asio::ssl::context ctx;
    long recvWindow;
    std::string host;
    std::string apiKey;
    std::string secretKey;
    ExchangerType exchangerType;

    std::string hmac_sha256(const std::string &key, const std::string &data) const;

  public:
    DealService(std::string host,
                std::string apiKey,
                std::string secretKey,
                int recvWindow,
                ExchangerType exchangerType)
        : ioc(), ctx(boost::asio::ssl::context::tls_client), recvWindow(recvWindow), host(host), apiKey(apiKey),
          secretKey(secretKey), exchangerType(exchangerType)
    {}

    virtual bool buyCrypto(const std::string &baseAsset, const std::string &quoteAsset, int quantity) = 0;

    virtual bool sellCrypto(const std::string &baseAsset, const std::string &quoteAsset, int quantity) = 0;

    ExchangerType getExchangerType() const;

    virtual ~DealService() = default;
};

#endif