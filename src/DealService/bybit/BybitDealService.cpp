#include "BybitDealService.hpp"
#include "../common/HttpRequest.hpp"
#include "../common/EnumStringConverter.hpp"

#include <sstream>
#include <chrono>
//DEBUG
#include <iostream>

using namespace std;
using namespace bybit;
template <typename K, typename V>
using flat_map = boost::container::flat_map<K, V>;

string BybitDealService::createQuery(const string& baseAsset, const string& quoteAsset,
    const OrderOperation& operation, const OrderType& type, int quantity) {
    ostringstream qr;
    qr << "{"
       << "\"symbol\":\"" << baseAsset << quoteAsset << "\","
       << "\"side\":\"" << EnumStringConverter<OrderOperation>::toString(operation) << "\","
       << "\"orderType\":\"" << EnumStringConverter<OrderType>::toString(type) << "\","
       << "\"qty\":" << quantity
       << "}";

    return qr.str();
}

flat_map<string, string> BybitDealService::createHeaders(const string& apiKey, const string& signature) {
    auto timestamp = chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()
    ).count();
    return {
        {"X-BAPI-API-KEY", apiKey},
        {"X-BAPI-TIMESTAMP", to_string(timestamp)},
        {"X-BAPI-SIGN", signature},
        {"X-BAPI-RECV-WINDOW", to_string(recvWindow)},
        {"Content-Type", "application/json"}
    };
}

string BybitDealService::getSignature(const string& query) {
    auto timestamp = chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()
    ).count();

    ostringstream sign_input;
    sign_input << timestamp << apiKey << recvWindow << query;
    return hmac_sha256(secretKey, sign_input.str());
}

bool BybitDealService::sendOrder(const string& query, const flat_map<string, string>& headers) {
    cout << "Sending order..." << endl;
    string target = "/v5/order/create?" + query;
    string response = httpsPost(ioc, ctx, target, host, apiKey, headers);
    cout << "Order response: " << response << endl;
    return true;
}

bool BybitDealService::buyCrypto(const string& baseAsset, const string& quoteAsset, int quantity) {
    string query = createQuery(baseAsset, quoteAsset, OrderOperation::BUY, OrderType::MARKET, quantity);
    string signature = getSignature(query);
    flat_map<string, string> headers = createHeaders(apiKey, signature);
    return sendOrder(query, headers);
}

bool BybitDealService::sellCrypto(const string& baseAsset, const string& quoteAsset, int quantity) {
    string query = createQuery(baseAsset, quoteAsset, OrderOperation::SELL, OrderType::MARKET, quantity);
    string signature = getSignature(query);
    flat_map<string, string> headers = createHeaders(apiKey, signature);
    return sendOrder(query, headers);
}