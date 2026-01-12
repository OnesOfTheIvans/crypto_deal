#include "BybitDealService.hpp"
#include "common/EnumStringConverter.hpp"
#include "common/http_request.hpp"

#include <sstream>
// DEBUG
#include <iostream>

#include "common/type_aliasing.hpp"

using namespace std;
using namespace bybit;

msec BybitDealService::getTimestamp()
{
    return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
}

string BybitDealService::createBody(const string &baseAsset,
                                    const string &quoteAsset,
                                    const OrderCategory &category,
                                    const OrderOperation &operation,
                                    const OrderType &type,
                                    double quantity)
{
    ostringstream body;
    // TODO maybe prepare json file with parameters
    // clang-format off
    body << "{"
         << "\"category\":\"" << EnumStringConverter<OrderCategory>::toString(category) << "\","
         << "\"symbol\":\"" << baseAsset << quoteAsset << "\","
         << "\"side\":\"" << EnumStringConverter<OrderOperation>::toString(operation) << "\","
         << "\"orderType\":\"" << EnumStringConverter<OrderType>::toString(type) << "\","
         << "\"qty\":\"" << quantity << "\""
         << "}";
    // clang-format on

    return body.str();
}

flat_map<string, string>
BybitDealService::createHeaders(const string &apiKey, const string &signature, const msec &timestamp)
{
    return {{"X-BAPI-API-KEY", apiKey},
            {"X-BAPI-TIMESTAMP", to_string(timestamp)},
            {"X-BAPI-SIGN", signature},
            {"X-BAPI-RECV-WINDOW", to_string(recvWindow)},
            {"Content-Type", "application/json"}};
}

string BybitDealService::getSignature(const string &body, const msec &timestamp)
{
    ostringstream sign_input;
    sign_input << timestamp << apiKey << recvWindow << body;
    return hmac_sha256(secretKey, sign_input.str());
}

double BybitDealService::parseAmount(const json::object &jsonObject, const char *key)
{
    if (auto *value = jsonObject.if_contains(key))
    {
        if (value->is_string())
        {
            return strtod(value->as_string().c_str(), nullptr);
        }
        if (value->is_number())
        {
            return value->as_double();
        }
    }
    return 0.0;
}

void BybitDealService::updateBalanceCache(const string &asset, double free, double locked)
{
    lock_guard<mutex> g(balanceMutex);
    auto it = balances.find(asset);
    if (it == balances.end())
    {
        balances.emplace(asset, AssetBalance{asset, free, locked});
    }
    else
    {
        it->second.free = free;
        it->second.locked = locked;
    }
}

AssetBalance BybitDealService::parseBalance(const json::object &coinObject)
{
    auto *nameValue = coinObject.if_contains("coin");
    if (!nameValue || !nameValue->is_string())
    {
        throw runtime_error("Bybit balance stream: missing or invalid 'coin' field");
    }

    const json::string &assetNameString = nameValue->as_string();
    string asset(assetNameString.c_str(), assetNameString.size());

    const double walletBalance = parseAmount(coinObject, "walletBalance");
    const double locked = parseAmount(coinObject, "locked");
    const double free = walletBalance - locked;

    return AssetBalance{asset, free, locked};
}

void BybitDealService::handleWalletStreamMessage(const string &msg)
{
    boost::system::error_code ec;
    json::value jsonValue = json::parse(msg, ec);
    if (ec)
    {
        throw runtime_error("Bybit stream: JSON parse error: " + ec.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Bybit stream: message is not an object");
    }

    json::object &root = jsonValue.as_object();

    auto *topicValue = root.if_contains("topic");
    if (!topicValue || !topicValue->is_string())
    {
        throw runtime_error("Bybit stream: missing or invalid 'topic' field");
    }
    if (topicValue->as_string() != "wallet")
    {
        const json::string &topic = topicValue->as_string();
        throw runtime_error("Bybit stream: unexpected topic: " + string(topic.c_str(), topic.size()));
    }

    auto *dataValue = root.if_contains("data");
    if (!dataValue || !dataValue->is_array())
    {
        throw runtime_error("Bybit wallet stream: missing or invalid 'data' field");
    }

    json::array &dataArray = dataValue->as_array();
    for (json::value &itemValue : dataArray)
    {
        if (itemValue.is_object())
        {
            json::object &itemObject = itemValue.as_object();

            auto *coinValue = itemObject.if_contains("coin");
            if (coinValue && coinValue->is_array())
            {
                json::array &coins = coinValue->as_array();
                for (json::value &coinItem : coins)
                {
                    if (coinItem.is_object())
                    {
                        json::object &coinObject = coinItem.as_object();

                        AssetBalance balance = parseBalance(coinObject);
                        updateBalanceCache(balance.asset, balance.free, balance.locked);
                    }
                }
            }
        }
    }
}

void BybitDealService::startWalletStream()
{
    const string ws_port = "443";
    const string ws_target = "/v5/private";

    tcp::resolver resolver(ioc);
    auto results = resolver.resolve(websocketHost, ws_port);

    beast::ssl_stream<beast::tcp_stream> tls(ioc, ctx);
    beast::get_lowest_layer(tls).connect(results);
    tls.handshake(ssl::stream_base::client);

    ws::stream<beast::ssl_stream<beast::tcp_stream>> socket(move(tls));
    socket.set_option(ws::stream_base::timeout::suggested(beast::role_type::client));
    socket.handshake(websocketHost, ws_target);

    const long long expires = getTimestamp() + 1000;
    const string payload = "GET/realtime" + to_string(expires);
    const string sig = hmac_sha256(secretKey, payload);

    json::object auth;
    auth["op"] = "auth";
    auth["args"] = json::array{apiKey, expires, sig};
    socket.write(net::buffer(json::serialize(auth)));

    json::object sub;
    sub["op"] = "subscribe";
    sub["args"] = json::array{"wallet"};
    socket.write(net::buffer(json::serialize(sub)));

    while (true)
    {
        beast::flat_buffer buffer;
        socket.read(buffer);

        const string msg = beast::buffers_to_string(buffer.data());
        handleWalletStreamMessage(msg);
    }
}

bool BybitDealService::sendOrder(const string &body, const flat_map<string, string> &headers)
{
    cout << "Sending order..." << endl;
    string target = "/v5/order/create";
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);
    context.setRequestBody(body);
    string response = httpsPost(context);
    cout << "Order response: " << response << endl;
    return true;
}

bool BybitDealService::buyCrypto(const string &baseAsset, const string &quoteAsset, double quantity)
{
    msec timestamp = getTimestamp();
    string body =
        createBody(baseAsset, quoteAsset, OrderCategory::SPOT, OrderOperation::BUY, OrderType::MARKET, quantity);
    string signature = getSignature(body, timestamp);
    flat_map<string, string> headers = createHeaders(apiKey, signature, timestamp);
    return sendOrder(body, headers);
}

bool BybitDealService::sellCrypto(const string &baseAsset, const string &quoteAsset, double quantity)
{
    msec timestamp = getTimestamp();
    string body =
        createBody(baseAsset, quoteAsset, OrderCategory::SPOT, OrderOperation::SELL, OrderType::MARKET, quantity);
    string signature = getSignature(body, timestamp);
    flat_map<string, string> headers = createHeaders(apiKey, signature, timestamp);
    return sendOrder(body, headers);
}

flat_map<string, AssetBalance> BybitDealService::getBalances() const
{
    lock_guard<mutex> g(balanceMutex);
    return balances;
}

optional<AssetBalance> BybitDealService::getBalance(const string &asset) const
{
    lock_guard<mutex> g(balanceMutex);

    auto it = balances.find(asset);
    if (it == balances.end())
    {
        return nullopt;
    }

    return it->second;
}