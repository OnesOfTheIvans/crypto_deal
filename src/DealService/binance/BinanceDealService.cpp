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

double BinanceDealService::parseAmount(const json::object &jsonObject, const char *key)
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

string BinanceDealService::buildUserStreamSubscribeRequestJson()
{
    auto ts = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();

    string payload = "apiKey=" + apiKey + "&timestamp=" + to_string(ts);

    string sig = hmac_sha256(secretKey, payload);

    json::object params;
    params["apiKey"] = apiKey;
    params["timestamp"] = ts;
    params["signature"] = move(sig);

    json::object req;
    req["id"] = "user_stream_subscribe_1";
    req["method"] = "userDataStream.subscribe.signature";
    req["params"] = move(params);

    return json::serialize(req);
}

void BinanceDealService::updateBalanceCache(const string &asset, double free, double locked)
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

void BinanceDealService::handleUserStreamMessage(const string &msg)
{
    boost::system::error_code ec;
    json::value jsonValue = json::parse(msg, ec);
    if (ec || !jsonValue.is_object())
    {
        throw runtime_error("User stream message parse error: " + ec.message());
    }

    json::object &rootObject = jsonValue.as_object();

    auto *eventTypeValue = rootObject.if_contains("event");
    if (!eventTypeValue)
    {
        return;
    }

    if (!eventTypeValue->is_object())
    {
        throw runtime_error("User stream message has invalid 'event' field (not an object)");
    }

    json::object &eventObject = eventTypeValue->as_object();

    auto *eventTypeNameValue = eventObject.if_contains("e");
    if (!eventTypeNameValue || !eventTypeNameValue->is_string())
    {
        throw runtime_error("User stream message missing or invalid 'e' (event type) field");
    }

    const json::string &eventType = eventTypeNameValue->as_string();
    if (eventType != "outboundAccountPosition")
    {
        return;
    }

    auto *balancesArrayValue = eventObject.if_contains("B");
    if (!balancesArrayValue || !balancesArrayValue->is_array())
    {
        throw runtime_error("User stream message missing or invalid 'B' (balances) field");
    }

    json::array &balancesArray = balancesArrayValue->as_array();

    for (json::value &item : balancesArray)
    {
        if (item.is_object())
        {
            json::object &balanceItemObject = item.as_object();

            auto *assetValue = balanceItemObject.if_contains("a");
            if (assetValue && assetValue->is_string())
            {
                const json::string &assetNameVal = assetValue->as_string();
                string assetName(assetNameVal.c_str(), assetNameVal.size());

                double free = parseAmount(balanceItemObject, "f");
                double locked = parseAmount(balanceItemObject, "l");

                updateBalanceCache(assetName, free, locked);
            }
        }
    }
}

void BinanceDealService::startUserStream()
{
    if (userStream)
    {
        return;
    }
    userStream = true;

    runner = std::thread([this]() {
        try
        {
            const string ws_port = "443";
            const string ws_target = "/ws-api/v3";

            tcp::resolver resolver(ioc);
            auto results = resolver.resolve(websocketHost, ws_port);

            beast::ssl_stream<beast::tcp_stream> tls(ioc, ctx);
            beast::get_lowest_layer(tls).connect(results);

            tls.handshake(ssl::stream_base::client);

            ws::stream<beast::ssl_stream<beast::tcp_stream>> socket(move(tls));
            socket.set_option(ws::stream_base::timeout::suggested(beast::role_type::client));
            socket.handshake(websocketHost, ws_target);

            const string sub = buildUserStreamSubscribeRequestJson();
            socket.write(net::buffer(sub));

            while (userStream)
            {
                beast::flat_buffer buffer;
                socket.read(buffer);

                const string msg = beast::buffers_to_string(buffer.data());
                handleUserStreamMessage(msg);
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "User stream error: " << e.what() << std::endl;
            userStream = false;
        }
    });
}

void BinanceDealService::stopUserStream()
{
    userStream = false;
    if (runner.joinable())
    {
        runner.join();
    }
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

OrderInfo BinanceDealService::placeOrder(const PlaceOrderRequest &request)
{
    if (request.symbol.empty())
        throw runtime_error("Symbol cannot be empty");
    if (request.quantity <= 0)
        throw runtime_error("Quantity must be greater than 0");
    if (request.side != "BUY" && request.side != "SELL")
        throw runtime_error("Invalid side: " + request.side);
    if (request.type != "MARKET" && request.type != "LIMIT")
        throw runtime_error("Invalid type: " + request.type);
    if (request.type == "LIMIT")
    {
        if (!request.price.has_value() || *request.price <= 0)
            throw runtime_error("Price must be > 0 for LIMIT orders");
        if (!request.timeInForce.has_value() || request.timeInForce->empty())
            throw runtime_error("TimeInForce required for LIMIT orders");
    }

    auto timestamp = chrono::system_clock::now();
    ostringstream qs;
    qs << "symbol=" << request.symbol
       << "&side=" << request.side
       << "&type=" << request.type
       << "&quantity=" << request.quantity;

    if (request.type == "LIMIT")
    {
        qs << "&price=" << *request.price
           << "&timeInForce=" << *request.timeInForce;
    }

    if (request.clientOrderId.has_value() && !request.clientOrderId->empty())
    {
        qs << "&newClientOrderId=" << *request.clientOrderId;
    }

    qs << "&newOrderRespType=RESULT"
       << "&recvWindow=" << recvWindow
       << "&timestamp=" << chrono::duration_cast<chrono::milliseconds>(timestamp.time_since_epoch()).count();

    string queryString = qs.str();
    string signature = hmac_sha256(secretKey, queryString);
    string fullQuery = queryString + "&signature=" + signature;

    string target = "/api/v3/order?" + fullQuery;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders({{"X-MBX-APIKEY", apiKey}});

    string response = httpsPost(context);

    boost::system::error_code ec;
    json::value jsonValue = json::parse(response, ec);
    if (ec)
        throw runtime_error("JSON parse error: " + ec.message());
    if (!jsonValue.is_object())
        throw runtime_error("Response is not a JSON object");

    json::object &obj = jsonValue.as_object();

    if (obj.contains("code") && obj.contains("msg"))
    {
        throw runtime_error("Binance Error " + to_string(obj["code"].as_int64()) + ": " + string(obj["msg"].as_string()));
    }

    return createOrderInfo(obj);
}

OrderInfo BinanceDealService::createOrderInfo(const json::object &obj)
{
    OrderInfo info;
    info.symbol = obj.at("symbol").as_string().c_str();
    
    if (obj.at("orderId").is_number())
        info.orderId = to_string(obj.at("orderId").as_int64());
    else if (obj.at("orderId").is_string())
        info.orderId = obj.at("orderId").as_string().c_str();

    if (obj.contains("clientOrderId"))
        info.clientOrderId = obj.at("clientOrderId").as_string().c_str();
    
    info.side = obj.at("side").as_string().c_str();
    info.type = obj.at("type").as_string().c_str();
    info.status = obj.at("status").as_string().c_str();
    
    if (obj.contains("timeInForce"))
        info.timeInForce = obj.at("timeInForce").as_string().c_str();

    info.price = parseAmount(obj, "price");
    info.origQty = parseAmount(obj, "origQty");
    info.executedQty = parseAmount(obj, "executedQty");

    if (obj.contains("cummulativeQuoteQty"))
        info.cumQuoteQty = parseAmount(obj, "cummulativeQuoteQty");
    else if (obj.contains("cumulativeQuoteQty"))
        info.cumQuoteQty = parseAmount(obj, "cumulativeQuoteQty");

    if (obj.contains("transactTime"))
        info.createdTimeMs = obj.at("transactTime").as_int64();
    
    info.updatedTimeMs = info.createdTimeMs;

    info.leavesQty = info.origQty - info.executedQty;
    if (info.executedQty > 0)
        info.avgPrice = info.cumQuoteQty / info.executedQty;

    return info;
}

optional<AssetBalance> BinanceDealService::getBalance(const string &asset) const
{
    lock_guard<mutex> g(balanceMutex);

    auto it = balances.find(asset);
    if (it == balances.end())
    {
        return nullopt;
    }

    return it->second;
}

flat_map<string, AssetBalance> BinanceDealService::getBalances() const
{
    lock_guard<mutex> g(balanceMutex);

    return balances;
}