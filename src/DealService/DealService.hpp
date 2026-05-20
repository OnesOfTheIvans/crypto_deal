#ifndef DEAL_SERVICE_H
#define DEAL_SERVICE_H

#include "AssetBalance.hpp"
#include "ExchangerType.hpp"
#include "common/OcoInfo.hpp"
#include "common/OrderInfo.hpp"
#include "common/OrderListQuery.hpp"
#include "common/OrderQuery.hpp"
#include "common/PlaceOcoRequest.hpp"
#include "common/PlaceOrderRequest.hpp"
#include "common/SymbolInfo.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/json.hpp>

#include <atomic>
#include <optional>
#include <string>
#include <thread>
#include <vector>

template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;

class DealService
{
  protected:
    boost::asio::io_context ioc;
    boost::asio::ssl::context ctx;
    long recvWindow;
    std::string host;
    std::string apiKey;
    std::string secretKey;
    std::string websocketHost;
    ExchangerType exchangerType;
    std::atomic<bool> userStream;
    std::thread runner;

    std::string hmac_sha256(const std::string &key, const std::string &data) const;

    void parseAndSetParameter(std::string &destinationField,
                              const boost::json::object &sourceObject,
                              boost::json::string_view fieldName,
                              bool isOptional = false) const;

    void parseAndSetParameter(Decimal &destinationField,
                              const boost::json::object &sourceObject,
                              boost::json::string_view fieldName,
                              bool isOptional = false) const;

    void parseAndSetParameter(long long &destinationField,
                              const boost::json::object &sourceObject,
                              boost::json::string_view fieldName,
                              bool isOptional = false) const;

    void parseAndSetParameter(int &destinationField,
                              const boost::json::object &sourceObject,
                              boost::json::string_view fieldName,
                              bool isOptional = false) const;

  public:
    DealService(std::string host,
                std::string apiKey,
                std::string secretKey,
                std::string websocketHost,
                int recvWindow,
                ExchangerType exchangerType)
        : ioc(), ctx(boost::asio::ssl::context::tls_client), recvWindow(recvWindow), host(host), apiKey(apiKey),
          secretKey(secretKey), websocketHost(websocketHost), exchangerType(exchangerType), userStream(false)
    {}

    virtual OrderInfo buyCrypto(const std::string &baseAsset, const std::string &quoteAsset, Decimal quantity) = 0;

    virtual OrderInfo sellCrypto(const std::string &baseAsset, const std::string &quoteAsset, Decimal quantity) = 0;

    virtual void waitUntilOrderFilled(const std::string &symbol, const std::string &orderId) = 0;

    virtual flat_map<std::string, AssetBalance> getBalances() const = 0;

    virtual std::optional<AssetBalance> getBalance(const std::string &asset) const = 0;

    virtual void startUserStream() = 0;

    virtual void stopUserStream() = 0;

    enum class StreamStatus
    {
        STOPPED,
        CONNECTING,
        CONNECTED,
        ERROR
    };
    virtual StreamStatus getUserStreamStatus() const = 0;

    virtual std::string getUserStreamLastError() const = 0;

    virtual OrderInfo placeOrder(const PlaceOrderRequest &request) = 0;

    virtual OrderInfo cancelOrder(const OrderQuery &request) = 0;

    virtual OrderInfo getOrder(const OrderQuery &request) = 0;

    virtual SymbolInfo getSymbolInfo(const std::string &symbol, const std::string &category = "spot") = 0;

    virtual Decimal
    ceilQuantityToStep(const std::string &symbol, Decimal quantity, const std::string &category = "spot") = 0;

    virtual OcoInfo placeOco(const PlaceOcoRequest &request) = 0;

    virtual OcoInfo cancelOco(const OrderListQuery &request) = 0;

    virtual bool cancelAllOpenOrders(const std::string &symbol, const std::string &category) = 0;

    virtual flat_map<std::string, AssetBalance> getBalancesRest() = 0;

    ExchangerType getExchangerType() const;

    virtual ~DealService() = default;
};

#endif
