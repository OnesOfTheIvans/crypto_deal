#include "BybitDealService.hpp"
#include "../common/HttpRequest.hpp"
#include "../common/EnumStringConverter.hpp"

#include <sstream>
//DEBUG
#include <iostream>

using namespace std;
using namespace bybit;
template <typename K, typename V>
using flat_map = boost::container::flat_map<K, V>;
using msec = chrono::milliseconds::rep;

msec BybitDealService::getTimestamp() {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()
    ).count();
}

string BybitDealService::createQuery(const string& baseAsset, const string& quoteAsset,
    const OrderOperation& operation, const OrderType& type, int quantity) {
    ostringstream body;
    //TODO new enum for the category
    //TODO maybe prepare json file with parameters
    body << "{"
       << "\"category\":\"spot\","
       << "\"symbol\":\"" << baseAsset << quoteAsset << "\","
       << "\"side\":\"" << EnumStringConverter<OrderOperation>::toString(operation) << "\","
       << "\"orderType\":\"" << EnumStringConverter<OrderType>::toString(type) << "\","
       << "\"qty\":\"" << quantity << "\""
       << "}";

    return body.str();
}

flat_map<string, string> BybitDealService::createHeaders(const string& apiKey, const string& signature,
    const msec& timestamp) {
    return {
        {"X-BAPI-API-KEY", apiKey},
        {"X-BAPI-TIMESTAMP", to_string(timestamp)},
        {"X-BAPI-SIGN", signature},
        {"X-BAPI-RECV-WINDOW", to_string(recvWindow)},
        {"Content-Type", "application/json"}
    };
}

string BybitDealService::getSignature(const string& body, const msec& timestamp) {
    ostringstream sign_input;
    sign_input << timestamp << apiKey << recvWindow << body;
    return hmac_sha256(secretKey, sign_input.str());
}

bool BybitDealService::sendOrder(const string& body, const flat_map<string, string>& headers) {
    cout << "Sending order..." << endl;
    string target = "/v5/order/create";
    string response = httpsPost(ioc, ctx, target, host, apiKey, headers, body);
    cout << "Order response: " << response << endl;
    return true;
}

bool BybitDealService::buyCrypto(const string& baseAsset, const string& quoteAsset, int quantity) {
    msec timestamp = getTimestamp();
    string body = createQuery(baseAsset, quoteAsset, OrderOperation::BUY, OrderType::MARKET, quantity);
    string signature = getSignature(body, timestamp);
    flat_map<string, string> headers = createHeaders(apiKey, signature, timestamp);
    return sendOrder(body, headers);
}

bool BybitDealService::sellCrypto(const string& baseAsset, const string& quoteAsset, int quantity) {
    msec timestamp = getTimestamp();
    string body = createQuery(baseAsset, quoteAsset, OrderOperation::SELL, OrderType::MARKET, quantity);
    string signature = getSignature(body, timestamp);
    flat_map<string, string> headers = createHeaders(apiKey, signature, timestamp);
    return sendOrder(body, headers);
}