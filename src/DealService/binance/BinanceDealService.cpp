#include "BinanceDealService.hpp"
#include "common/EnumStringConverter.hpp"
#include "common/HttpRequestContext.hpp"
#include "common/http_request.hpp"

#include <chrono>
#include <sstream>
// DEBUG
#include <iostream>

#include "../common/type_aliasing.hpp"

using namespace std;
using namespace binance;

string BinanceDealService::createQuery(const string &baseAsset,
                                       const string &quoteAsset,
                                       const OrderOperation &operation,
                                       const OrderType &type,
                                       double quantity)
{
    auto timestamp = chrono::system_clock::now();
    ostringstream qs;
    // clang-format off
    qs << "symbol=" << baseAsset << quoteAsset
       << "&side=" << EnumStringConverter<OrderOperation>::toString(operation)
       << "&type=" << EnumStringConverter<OrderType>::toString(type)
       << "&quantity=" << quantity
       << "&recvWindow=" << recvWindow
       << "&timestamp=" << chrono::duration_cast<chrono::milliseconds>(timestamp.time_since_epoch()).count();
    // clang-format on

    string query_string = qs.str();
    string signature = hmac_sha256(secretKey, query_string);
    return query_string + "&signature=" + signature;
}

string BinanceDealService::createInfoQuery()
{
    auto timestamp = chrono::system_clock::now();
    ostringstream qs;
    // clang-format off
    qs << "&recvWindow=" << recvWindow
       << "&timestamp=" << chrono::duration_cast<chrono::milliseconds>(timestamp.time_since_epoch()).count();
    // clang-format on

    string query_string = qs.str();
    string signature = hmac_sha256(secretKey, query_string);
    return query_string + "&signature=" + signature;
}

flat_map<string, string> BinanceDealService::createHeaders(const string &apiKey)
{
    return {{"X-MBX-APIKEY", apiKey}};
}

bool BinanceDealService::sendOrder(const string &query, const flat_map<string, string> &headers)
{
    string test_target = "/api/v3/order/test?" + query;
    HttpRequestContext context(ioc, ctx, host, test_target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);

    cout << "Sending test order..." << endl;
    string response = httpsPost(context);
    cout << "Test order response: " << response << endl;

    if (response == "{}")
    {
        cout << "Test passed, sending real order..." << endl;
        string real_target = "/api/v3/order?" + query;
        HttpRequestContext context(ioc, ctx, host, real_target);
        context.prepareRequest(http::verb::post);
        context.setRequestHeaders(headers);
        string real_response = httpsPost(context);
        cout << "Real order response: " << real_response << endl;
        return true;
    }
    else
    {
        cout << "Test order failed or unexpected response, not placing real order." << endl;
        return false;
    }
}

vector<AssetBalance> BinanceDealService::parseBalances(const string &response)
{
    vector<AssetBalance> result;

    boost::system::error_code ec;
    json::value val = json::parse(response, ec);
    if (ec || !val.is_object())
    {
        return result;
    }

    json::object &obj = val.as_object();

    auto *balances_val = obj.if_contains("balances");
    if (!balances_val || !balances_val->is_array())
    {
        return result;
    }

    json::array &balances = balances_val->as_array();

    auto parse_amount = [](const json::object &o, const char *key) -> double
    {
        auto *v = o.if_contains(key);
        if (!v)
        {
            return 0.0;
        }

        if (v->is_string())
        {
            const string &js = v->as_string().c_str();
            return strtod(js.c_str(), nullptr);
        }
        if (v->is_number())
        {
            return v->as_double();
        }
        return 0.0;
    };

    for (json::value &v : balances)
    {
        if (!v.is_object())
        {
            continue;
        }

        json::object &bal = v.as_object();

        auto *asset_val = bal.if_contains("asset");
        if (!asset_val || !asset_val->is_string())
        {
            continue;
        }

        const string &js_asset = asset_val->as_string().c_str();
        string asset(js_asset.c_str(), js_asset.size());

        double free = parse_amount(bal, "free");
        double locked = parse_amount(bal, "locked");

        if (asset.empty())
        {
            continue;
        }

        if (free == 0.0 && locked == 0.0)
        {
            continue;
        }

        result.push_back(AssetBalance{move(asset), free, locked});
    }

    return result;
}

bool BinanceDealService::buyCrypto(const string &baseAsset, const string &quoteAsset, double quantity)
{
    string query = createQuery(baseAsset, quoteAsset, OrderOperation::BUY, OrderType::MARKET, quantity);
    flat_map<string, string> headers = createHeaders(apiKey);
    return sendOrder(query, headers);
}

bool BinanceDealService::sellCrypto(const string &baseAsset, const string &quoteAsset, double quantity)
{
    string query = createQuery(baseAsset, quoteAsset, OrderOperation::SELL, OrderType::MARKET, quantity);
    flat_map<string, string> headers = createHeaders(apiKey);
    return sendOrder(query, headers);
}

vector<AssetBalance> BinanceDealService::getBalances()
{
    string query = createInfoQuery();
    string target = "/api/v3/account?" + query;

    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    flat_map<string, string> headers = createHeaders(apiKey);
    context.setRequestHeaders(headers);

    string response = httpsGet(context);

    // DEBUG
    cout << "Account response: " << response << endl;

    return parseBalances(response);
}

optional<AssetBalance> BinanceDealService::getBalance(const string &asset) {}