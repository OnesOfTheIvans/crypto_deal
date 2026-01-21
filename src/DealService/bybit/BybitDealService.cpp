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
    if (!topicValue)
    {
        return;
    }
    if (!topicValue->is_string())
    {
        throw runtime_error("Bybit stream: 'topic' field is not a string");
    }

    if (topicValue->as_string() != "wallet")
    {
        return;
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

void BybitDealService::startUserStream()
{
    if (userStream)
    {
        return;
    }
    userStream = true;

    runner = std::thread(
        [this]()
        {
            try
            {
                const string wsPort = "443";
                const string wsTarget = "/v5/private";

                tcp::resolver resolver(ioc);
                auto results = resolver.resolve(websocketHost, wsPort);

                beast::ssl_stream<beast::tcp_stream> tls(ioc, ctx);
                beast::get_lowest_layer(tls).connect(results);
                tls.handshake(ssl::stream_base::client);

                ws::stream<beast::ssl_stream<beast::tcp_stream>> socket(move(tls));
                socket.set_option(ws::stream_base::timeout::suggested(beast::role_type::client));
                socket.handshake(websocketHost, wsTarget);

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

                while (userStream)
                {
                    beast::flat_buffer buffer;
                    socket.read(buffer);

                    const string msg = beast::buffers_to_string(buffer.data());
                    handleWalletStreamMessage(msg);
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "Bybit stream error: " << e.what() << std::endl;
                userStream = false;
            }
        });
}

void BybitDealService::stopUserStream()
{
    userStream = false;
    if (runner.joinable())
    {
        runner.join();
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

OrderInfo BybitDealService::placeOrder(const PlaceOrderRequest &request)
{
    if (request.symbol.empty())
    {
        throw runtime_error("Symbol cannot be empty");
    }
    if (request.quantity <= 0)
    {
        throw runtime_error("Quantity must be greater than 0");
    }
    if (request.side != "BUY" && request.side != "SELL")
    {
        throw runtime_error("Invalid side: " + request.side);
    }
    if (request.type != "MARKET" && request.type != "LIMIT")
    {
        throw runtime_error("Invalid type: " + request.type);
    }
    if (request.type == "LIMIT")
    {
        if (!request.price.has_value() || *request.price <= 0)
        {
            throw runtime_error("Price must be > 0 for LIMIT orders");
        }
        if (!request.timeInForce.has_value() || request.timeInForce->empty())
        {
            throw runtime_error("TimeInForce required for LIMIT orders");
        }
    }

    string side = (request.side == "BUY") ? "Buy" : "Sell";
    string type = (request.type == "MARKET") ? "Market" : "Limit";
    string category = request.category;
    if (category.empty())
    {
        category = "spot";
    }

    json::object body;
    body["category"] = category;
    body["symbol"] = request.symbol;
    body["side"] = side;
    body["orderType"] = type;
    body["qty"] = to_string(request.quantity);

    if (request.type == "LIMIT")
    {
        body["price"] = to_string(*request.price);
        body["timeInForce"] = *request.timeInForce;
    }

    if (request.clientOrderId.has_value() && !request.clientOrderId->empty())
    {
        body["orderLinkId"] = *request.clientOrderId;
    }

    string bodyStr = json::serialize(body);

    msec timestamp = getTimestamp();
    string signature = getSignature(bodyStr, timestamp);
    auto headers = createHeaders(apiKey, signature, timestamp);
    string target = "/v5/order/create";
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);
    context.setRequestBody(bodyStr);

    string response = httpsPost(context);

    boost::system::error_code ec;
    json::value jsonValue = json::parse(response, ec);
    if (ec)
    {
        throw runtime_error("JSON parse error: " + ec.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Response is not a JSON object");
    }

    json::object &obj = jsonValue.as_object();

    int retCode = -1;
    if (obj.contains("retCode") && obj.at("retCode").is_number())
    {
        retCode = obj.at("retCode").as_int64();
    }

    if (retCode != 0)
    {
        string msg = "Unknown Error";
        if (obj.contains("retMsg") && obj.at("retMsg").is_string())
        {
            msg = obj.at("retMsg").as_string().c_str();
        }
        throw runtime_error("Bybit Error " + to_string(retCode) + ": " + msg);
    }

    if (!obj.contains("result") || !obj.at("result").is_object())
    {
        throw runtime_error("Missing result object in response");
    }

    json::object &result = obj.at("result").as_object();

    return createOrderInfo(result, request, side, type, timestamp);
}

OrderInfo BybitDealService::createOrderInfo(const json::object &result,
                                            const PlaceOrderRequest &request,
                                            const std::string &side,
                                            const std::string &type,
                                            msec timestamp)
{
    OrderInfo info;
    info.symbol = request.symbol;

    if (request.category.empty())
    {
        info.category = "spot";
    }
    else
    {
        info.category = request.category;
    }

    if (result.contains("orderId") && result.at("orderId").is_string())
    {
        info.orderId = result.at("orderId").as_string().c_str();
    }
    else
    {
        throw runtime_error("Missing orderId in response");
    }

    if (result.contains("orderLinkId") && result.at("orderLinkId").is_string())
    {
        info.clientOrderId = result.at("orderLinkId").as_string().c_str();
    }

    info.side = side;
    info.type = type;

    info.status = "New";

    if (request.timeInForce.has_value())
    {
        info.timeInForce = *request.timeInForce;
    }

    if (request.price.has_value())
    {
        info.price = *request.price;
    }

    info.origQty = request.quantity;
    info.leavesQty = request.quantity;

    info.executedQty = 0.0;
    info.cumQuoteQty = 0.0;
    info.avgPrice = 0.0;

    info.createdTimeMs = timestamp;
    info.updatedTimeMs = timestamp;

    return info;
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