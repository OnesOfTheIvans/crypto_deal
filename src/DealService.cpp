#include "DealService.hpp"
#include "HttpRequest.hpp"
#include "EnumStringConverter.hpp"

#include <sstream>
#include <chrono>
//debug
#include <iostream>

using namespace std;

std::string DealService::createQuery(const std::string& baseAsset, const std::string& quoteAsset, const OrderOperation& operation, const OrderType& type, int quantity) {
    auto server_time = std::chrono::system_clock::now();
    std::ostringstream qs;
    qs << "symbol=" << baseAsset << quoteAsset
       << "&side=" << EnumStringConverter<OrderOperation>::toString(operation)
       << "&type=" << EnumStringConverter<OrderType>::toString(type)
       << "&quantity=" << quantity
       << "&recvWindow=" << recvWindow
       << "&timestamp=" << chrono::duration_cast<std::chrono::milliseconds>(
        server_time.time_since_epoch()
    ).count();

    std::string query_string = qs.str();
    std::string signature = hmac_sha256(secretKey, query_string);
    return query_string + "&signature=" + signature;
}

bool DealService::sendOrder(const std::string& query) {
    std::string test_target = "/api/v3/order/test?" + query;

    std::cout << "Sending test order..." << std::endl;
    std::string response = httpsPost(ioc, ctx, test_target, host, apiKey);
    std::cout << "Test order response: " << response << std::endl;

    if (response == "{}") {
        std::cout << "Test passed, sending real order..." << std::endl;
        std::string real_target = "/api/v3/order?" + query;
        std::string real_response = httpsPost(ioc, ctx, real_target, host, apiKey);
        std::cout << "Real order response: " << real_response << std::endl;
        return true;
    } else {
        std::cout << "Test order failed or unexpected response, not placing real order." << std::endl;
        return false;
    }
}

bool DealService::buyCrypto(const string& baseAsset, const string& quoteAsset, int quantity) {
    std::string query = createQuery(baseAsset, quoteAsset, OrderOperation::BUY, OrderType::MARKET, quantity);
    return sendOrder(query);
}

bool DealService::sellCrypto(const string& baseAsset, const string& quoteAsset, int quantity) {
   std::string query = createQuery(baseAsset, quoteAsset, OrderOperation::SELL, OrderType::MARKET, quantity);
   return sendOrder(query);
}

// Correct HMAC SHA256 returning hex string
string DealService::hmac_sha256(const string& key, const string& data) const {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(),
         key.data(), (int)key.size(),
         (const unsigned char*)data.data(), data.size(),
         digest, &digest_len);

    ostringstream oss;
    for (unsigned int i = 0; i < digest_len; ++i)
        oss << hex << setw(2) << setfill('0') << (int)digest[i];
    return oss.str();
}