#include "BinanceDealService.hpp"
#include "../common/HttpRequest.hpp"
#include "../common/EnumStringConverter.hpp"
#include "../common/HttpRequestContext.hpp"

#include <sstream>
#include <chrono>
//DEBUG
#include <iostream>

using namespace std;
using namespace binance;
namespace http = boost::beast::http;
template <typename K, typename V>
using flat_map = boost::container::flat_map<K, V>;

string BinanceDealService::createQuery(const string& baseAsset, const string& quoteAsset,
    const OrderOperation& operation, const OrderType& type, int quantity) {
    auto timestamp = chrono::system_clock::now();
    ostringstream qs;
    qs << "symbol=" << baseAsset << quoteAsset
       << "&side=" << EnumStringConverter<OrderOperation>::toString(operation)
       << "&type=" << EnumStringConverter<OrderType>::toString(type)
       << "&quantity=" << quantity
       << "&recvWindow=" << recvWindow
       << "&timestamp=" << chrono::duration_cast<chrono::milliseconds>(
        timestamp.time_since_epoch()
    ).count();

    string query_string = qs.str();
    string signature = hmac_sha256(secretKey, query_string);
    return query_string + "&signature=" + signature;
}

flat_map<string, string> BinanceDealService::createHeaders(const string& apiKey) {
    return {
        {"X-MBX-APIKEY", apiKey}
    };
}

bool BinanceDealService::sendOrder(const string& query, const flat_map<string, string>& headers) {
    string test_target = "/api/v3/order/test?" + query;
    HttpRequestContext context(ioc, ctx, host, test_target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);

    cout << "Sending test order..." << endl;
    string response = httpsPost(context);
    cout << "Test order response: " << response << endl;

    if (response == "{}") {
        cout << "Test passed, sending real order..." << endl;
        string real_target = "/api/v3/order?" + query;
        HttpRequestContext context(ioc, ctx, host, real_target);
        context.prepareRequest(http::verb::post);
        context.setRequestHeaders(headers);
        string real_response = httpsPost(context);
        cout << "Real order response: " << real_response << endl;
        return true;
    } else {
        cout << "Test order failed or unexpected response, not placing real order." << endl;
        return false;
    }
}

bool BinanceDealService::buyCrypto(const string& baseAsset, const string& quoteAsset, int quantity) {
    string query = createQuery(baseAsset, quoteAsset, OrderOperation::BUY, OrderType::MARKET, quantity);
    flat_map<string, string> headers = createHeaders(apiKey);
    return sendOrder(query, headers);
}

bool BinanceDealService::sellCrypto(const string& baseAsset, const string& quoteAsset, int quantity) {
    string query = createQuery(baseAsset, quoteAsset, OrderOperation::SELL, OrderType::MARKET, quantity);
    flat_map<string, string> headers = createHeaders(apiKey);
    return sendOrder(query, headers);
}