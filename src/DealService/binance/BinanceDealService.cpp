#include "BinanceDealService.hpp"
#include "common/EnumStringConverter.hpp"
#include "common/HttpRequestContext.hpp"
#include "common/exception_handling.hpp"
#include "common/http_request.hpp"

#include <chrono>
#include <sstream>
// DEBUG
#include <iostream>

#include "../common/DecimalConverter.hpp"
#include "../common/type_aliasing.hpp"

using namespace std;
using namespace binance;
using namespace exception_handling;

namespace {
    string binanceErrorMessage(const json::object &jsonObject)
    {
        long long code = 0;
        if (auto *codeValue = jsonObject.if_contains("code"); codeValue && codeValue->is_int64())
        {
            code = codeValue->as_int64();
        }

        string msg;
        if (auto *msgValue = jsonObject.if_contains("msg"); msgValue && msgValue->is_string())
        {
            msg = string(msgValue->as_string());
        }

        return "Binance Error " + to_string(code) + ": " + msg;
    }
} // namespace

string BinanceDealService::createQuery(const string &baseAsset,
                                       const string &quoteAsset,
                                       const OrderOperation &operation,
                                       const OrderType &type,
                                       Decimal quantity,
                                       Decimal stepSize)
{
    const string qtyStr = DecimalConverter::formatByStep(quantity, stepSize);

    auto timestamp = chrono::system_clock::now();
    ostringstream queryStream;
    // clang-format off
    queryStream << "symbol=" << baseAsset << quoteAsset
       << "&side=" << EnumStringConverter<OrderOperation>::toString(operation)
       << "&type=" << EnumStringConverter<OrderType>::toString(type)
       << "&quantity=" << qtyStr
       << "&recvWindow=" << recvWindow
       << "&timestamp=" << chrono::duration_cast<chrono::milliseconds>(timestamp.time_since_epoch()).count();
    // clang-format on

    string query_string = queryStream.str();
    string signature = hmac_sha256(secretKey, query_string);
    return query_string + "&signature=" + signature;
}

flat_map<string, string> BinanceDealService::createHeaders(const string &apiKey)
{
    return {{"X-MBX-APIKEY", apiKey}};
}

optional<string> BinanceDealService::binanceResponseOk(const string &response)
{
    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    if (errorCode)
    {
        return "JSON parse error: " + errorCode.message();
    }
    if (!jsonValue.is_object())
    {
        return "Response is not a JSON object";
    }

    const auto &jsonObject = jsonValue.as_object();

    if (jsonObject.contains("code") && jsonObject.contains("msg"))
    {
        long long code = 0;
        if (jsonObject.at("code").is_number())
        {
            code = jsonObject.at("code").as_int64();
        }
        string msg;
        if (jsonObject.at("msg").is_string())
        {
            msg = jsonObject.at("msg").as_string().c_str();
        }
        return "Binance Error " + to_string(code) + ": " + msg;
    }

    if (jsonObject.contains("orderId") && jsonObject.contains("status"))
    {
        return nullopt;
    }

    return "Unexpected response structure (missing orderId/status)";
}

std::string BinanceDealService::sendOrder(const string &query, const flat_map<string, string> &headers)
{
    string target = "/api/v3/order?" + query;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);

    cout << "Sending order..." << endl;
    string response = httpsPost(context);
    cout << "Order response: " << response << endl;

    optional<string> errorOutput = binanceResponseOk(response);
    throwIf(errorOutput.has_value(), "Order failed: " + errorOutput.value());
    return response;
}

Decimal BinanceDealService::parseAmount(const json::object &jsonObject, const char *key)
{
    if (auto *value = jsonObject.if_contains(key))
    {
        if (value->is_string())
        {
            return DecimalConverter::parseDecimal(value->as_string().c_str());
        }
        if (value->is_number())
        {
            return DecimalConverter::parseDecimal(json::serialize(*value));
        }
    }
    return Decimal{};
}

string BinanceDealService::buildUserStreamSubscribeRequestJson()
{

    auto ts = getTimestamp();

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

void BinanceDealService::updateBalanceCache(const string &asset, Decimal free, Decimal locked)
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

void BinanceDealService::updateCache(const json::array &balancesArray)
{
    for (const json::value &item : balancesArray)
    {
        if (item.is_object())
        {
            const json::object &balanceItemObject = item.as_object();

            auto *assetValue = balanceItemObject.if_contains("a");
            if (assetValue && assetValue->is_string())
            {
                const json::string &assetNameVal = assetValue->as_string();
                string assetName(assetNameVal.c_str(), assetNameVal.size());

                Decimal free = parseAmount(balanceItemObject, "f");
                Decimal locked = parseAmount(balanceItemObject, "l");

                updateBalanceCache(assetName, free, locked);
            }
        }
    }
}

void BinanceDealService::handleUserStreamMessage(const string &msg)
{
    beast::error_code ec;
    json::value jsonValue = json::parse(msg, ec);
    if (ec)
    {
        cerr << "User stream message parse error: " << ec.message() << endl;
        return;
    }
    if (!jsonValue.is_object())
    {
        return;
    }

    const json::object &rootObject = jsonValue.as_object();
    auto *eventValue = rootObject.if_contains("event");
    if (!eventValue || !eventValue->is_object())
    {
        return;
    }

    const json::object &eventObject = eventValue->as_object();

    auto *eventTypeNameValue = eventObject.if_contains("e");
    throwIf(!eventTypeNameValue || !eventTypeNameValue->is_string(),
            "User stream message missing or invalid 'e' (event type) field");

    const json::string &eventType = eventTypeNameValue->as_string();
    if (eventType != "outboundAccountPosition")
    {
        return;
    }

    auto *balancesArrayValue = eventObject.if_contains("B");
    if (!balancesArrayValue || !balancesArrayValue->is_array())
    {
        return;
    }

    updateCache(balancesArrayValue->as_array());
}

DealService::StreamStatus BinanceDealService::getUserStreamStatus() const
{
    lock_guard<mutex> lock(streamStatusMutex);
    return streamStatus;
}

string BinanceDealService::getUserStreamLastError() const
{
    lock_guard<mutex> lock(streamStatusMutex);
    return streamLastError;
}

void BinanceDealService::setStreamStatus(StreamStatus status)
{
    lock_guard<mutex> lock(streamStatusMutex);
    streamStatus = status;
}

void BinanceDealService::setStreamError(const string &error)
{
    lock_guard<mutex> lock(streamStatusMutex);
    streamStatus = StreamStatus::ERROR;
    streamLastError = error;
}

long long BinanceDealService::getServerTime()
{
    string target = "/api/v3/time";
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);
    string response = httpsPost(context);

    beast::error_code ec;
    json::value val = json::parse(response, ec);
    if (!ec && val.is_object())
    {
        const auto &obj = val.as_object();
        if (obj.contains("serverTime") && obj.at("serverTime").is_number())
        {
            return obj.at("serverTime").as_int64();
        }
    }
    return 0;
}

void BinanceDealService::syncTime()
{
    long long currentMono = chrono::steady_clock::now().time_since_epoch().count() / 1000000; // ms
    long long lastMono = lastSyncMonoMs.load();
    if (currentMono - lastMono < 10000)
    {
        return;
    }

    lock_guard<mutex> lock(timeSyncMutex);

    currentMono = chrono::steady_clock::now().time_since_epoch().count() / 1000000;
    lastMono = lastSyncMonoMs.load();
    if (currentMono - lastMono < 10000)
    {
        return;
    }

    long long serverTime = getServerTime();
    if (serverTime > 0)
    {
        long long localTime =
            chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
        serverTimeOffset = serverTime - localTime;
        lastSyncMonoMs.store(currentMono);
        cout << "Binance time synced. Offset: " << serverTimeOffset << "ms" << endl;
    }
}

long long BinanceDealService::getTimestamp()
{
    syncTime();
    auto now = chrono::system_clock::now();
    return chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count() + serverTimeOffset;
}

void BinanceDealService::startUserStream()
{
    if (userStream)
    {
        return;
    }

    try
    {
        getBalancesRest();
    }
    catch (const exception &e)
    {
        cerr << "Binance REST balances seed failed (continuing): " << e.what() << endl;
    }

    userStream = true;
    setStreamStatus(StreamStatus::CONNECTING);

    runner = thread(
        [this]()
        {
            try
            {
                const string ws_port = "443";
                const string ws_target = "/ws-api/v3";

                tcp::resolver resolver(ioc);
                auto results = resolver.resolve(websocketHost, ws_port);

                beast::ssl_stream<beast::tcp_stream> tls(ioc, ctx);
                beast::get_lowest_layer(tls).connect(results);

                tls.handshake(ssl::stream_base::client);

                auto sharedWebsocketStream = make_shared<WebsocketStream>(move(tls));
                sharedWebsocketStream->set_option(ws::stream_base::timeout::suggested(beast::role_type::client));
                sharedWebsocketStream->handshake(websocketHost, ws_target);

                {
                    lock_guard<mutex> lock(userWebsocketMutex);
                    userWebsocketStream = sharedWebsocketStream;
                }

                setStreamStatus(StreamStatus::CONNECTING);

                const string sub = buildUserStreamSubscribeRequestJson();
                sharedWebsocketStream->write(net::buffer(sub));

                {
                    beast::flat_buffer buffer;
                    sharedWebsocketStream->read(buffer);
                    string msg = beast::buffers_to_string(buffer.data());

                    beast::error_code ec;
                    json::value val = json::parse(msg, ec);
                    bool ok = false;
                    if (!ec && val.is_object())
                    {
                        json::object &root = val.as_object();
                        if (root.contains("status") && root.at("status").as_int64() == 200)
                        {
                            ok = true;
                        }
                    }

                    if (!ok)
                    {
                        cerr << "Binance stream subscription failed or invalid response: " << msg << endl;
                    }
                    else
                    {
                        cout << "Binance stream subscribed successfully." << endl;
                    }
                }

                setStreamStatus(StreamStatus::CONNECTED);

                while (userStream)
                {
                    try
                    {
                        beast::flat_buffer buffer;
                        beast::error_code errorCode;

                        sharedWebsocketStream->read(buffer, errorCode);

                        if (errorCode)
                        {
                            if (!userStream)
                            {
                                cout << "User stream stopped gracefully." << endl;
                                break;
                            }
                            if (errorCode == net::error::operation_aborted)
                            {
                                break;
                            }

                            cerr << "Websocket read error: " << errorCode.message() << endl;
                            setStreamError("Read error: " + errorCode.message());
                            break;
                        }

                        const string msg = beast::buffers_to_string(buffer.data());
                        handleUserStreamMessage(msg);
                    }
                    catch (const exception &e)
                    {
                        cerr << "Binance stream read loop error: " << e.what() << endl;
                        break;
                    }
                }
            }
            catch (const exception &e)
            {
                cerr << "Binance stream error: " << e.what() << endl;
                setStreamError(e.what());
                userStream = false;
            }

            {
                lock_guard<mutex> lock(userWebsocketMutex);
                userWebsocketStream.reset();
            }
            if (getUserStreamStatus() != StreamStatus::ERROR)
            {
                setStreamStatus(StreamStatus::STOPPED);
            }
        });
}

void BinanceDealService::stopUserStream()
{
    cout << "Requesting user stream stop..." << endl;
    userStream = false;

    shared_ptr<WebsocketStream> sharedWebsocketStream;
    {
        lock_guard<mutex> lock(userWebsocketMutex);
        sharedWebsocketStream = userWebsocketStream;
    }

    if (sharedWebsocketStream)
    {
        cout << "Forcing socket closure to unblock read..." << endl;
        beast::error_code ec;
        // Close the lowest layer (TCP socket) to force read to return error
        beast::get_lowest_layer(*sharedWebsocketStream).socket().close(ec);
        if (ec)
        {
            cerr << "Socket close error (ignored): " << ec.message() << endl;
        }
    }

    if (runner.joinable())
    {
        cout << "Joining stream thread..." << endl;
        runner.join();
        cout << "Stream thread joined." << endl;
    }

    setStreamStatus(StreamStatus::STOPPED);

    {
        lock_guard<mutex> lock(userWebsocketMutex);
        userWebsocketStream.reset();
    }
}

Decimal BinanceDealService::getTickerPrice(const string &symbol)
{
    string target = "/api/v3/ticker/price?symbol=" + symbol;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    string response = httpsPost(context);

    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    if (!errorCode && jsonValue.is_object())
    {
        const auto &jsonObject = jsonValue.as_object();
        if (jsonObject.contains("price") && jsonObject.at("price").is_string())
        {
            return DecimalConverter::parseDecimal(jsonObject.at("price").as_string().c_str());
        }
    }
    return Decimal{};
}

Decimal BinanceDealService::calculateSafeQty(const string &symbol,
                                             Decimal quantity,
                                             Decimal price,
                                             Decimal stepSize,
                                             Decimal minNotional)
{
    if (price <= 0 || stepSize <= 0)
    {
        return quantity;
    }

    Decimal requiredQty{};
    if (minNotional > 0)
    {
        requiredQty = (minNotional * DecimalConverter::parseDecimal("1.10")) / price;
    }

    Decimal safeQty = max(quantity, requiredQty);
    safeQty = DecimalConverter::ceilToStep(safeQty, stepSize);

    cout << "Binance Safe Qty: " << DecimalConverter::formatByStep(safeQty, stepSize)
         << " (Req: " << DecimalConverter::formatDecimal(quantity)
         << ", Price: " << DecimalConverter::formatDecimal(price)
         << ", MinNotional: " << DecimalConverter::formatDecimal(minNotional)
         << ", Step: " << DecimalConverter::formatDecimal(stepSize) << ")" << endl;

    return safeQty;
}

OrderInfo BinanceDealService::buyCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity)
{
    string symbol = baseAsset + quoteAsset;
    Decimal stepSize{};
    Decimal minNotional{};

    try
    {
        SymbolInfo info = getSymbolInfo(symbol);
        stepSize = info.stepSize;
        minNotional = info.minNotional;
    }
    catch (...)
    {
        cerr << "Failed to get symbol info for " << symbol << ", using defaults" << endl;
    }

    Decimal price = getTickerPrice(symbol);
    Decimal safeQty = calculateSafeQty(symbol, quantity, price, stepSize, minNotional);

    string query = createQuery(baseAsset, quoteAsset, OrderOperation::BUY, OrderType::MARKET, safeQty, stepSize);
    flat_map<string, string> headers = createHeaders(apiKey);

    string response = sendOrder(query, headers);

    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode || !jsonValue.is_object(), "Failed to parse buyCrypto response");
    return createOrderInfo(jsonValue.as_object());
}

OrderInfo BinanceDealService::sellCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity)
{
    string symbol = baseAsset + quoteAsset;
    Decimal stepSize{};
    Decimal minNotional{};

    try
    {
        SymbolInfo info = getSymbolInfo(symbol);
        stepSize = info.stepSize;
        minNotional = info.minNotional;
    }
    catch (...)
    {
        cerr << "Failed to get symbol info for " << symbol << ", using defaults" << endl;
    }

    Decimal price = getTickerPrice(symbol);
    Decimal safeQty = calculateSafeQty(symbol, quantity, price, stepSize, minNotional);

    string query = createQuery(baseAsset, quoteAsset, OrderOperation::SELL, OrderType::MARKET, safeQty, stepSize);
    flat_map<string, string> headers = createHeaders(apiKey);

    string response = sendOrder(query, headers);

    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode || !jsonValue.is_object(), "Failed to parse sellCrypto response");
    return createOrderInfo(jsonValue.as_object());
}

OrderInfo BinanceDealService::placeOrder(const PlaceOrderRequest &request)
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(request.quantity <= 0, "Quantity must be greater than 0");
    throwIf(request.side != "BUY" && request.side != "SELL", "Invalid side: " + request.side);
    throwIf(request.type != "MARKET" && request.type != "LIMIT", "Invalid type: " + request.type);
    if (request.type == "LIMIT")
    {
        throwIf(!request.price.has_value() || *request.price <= 0, "Price must be > 0 for LIMIT orders");
        throwIf(!request.timeInForce.has_value() || request.timeInForce->empty(),
                "TimeInForce required for LIMIT orders");
    }

    SymbolInfo info = getSymbolInfo(request.symbol);

    auto timestamp = chrono::system_clock::now();
    ostringstream queryStream;
    queryStream << "symbol=" << request.symbol << "&side=" << request.side << "&type=" << request.type
                << "&quantity=" << DecimalConverter::formatByStep(request.quantity, info.stepSize);

    if (request.type == "LIMIT")
    {
        queryStream << "&price=" << DecimalConverter::formatByStep(*request.price, info.tickSize)
                    << "&timeInForce=" << *request.timeInForce;
    }

    if (request.clientOrderId.has_value() && !request.clientOrderId->empty())
    {
        queryStream << "&newClientOrderId=" << *request.clientOrderId;
    }

    queryStream << "&newOrderRespType=RESULT" << "&recvWindow=" << recvWindow
                << "&timestamp=" << chrono::duration_cast<chrono::milliseconds>(timestamp.time_since_epoch()).count();

    string queryString = queryStream.str();
    string signature = hmac_sha256(secretKey, queryString);
    string fullQuery = queryString + "&signature=" + signature;

    string target = "/api/v3/order?" + fullQuery;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders({{"X-MBX-APIKEY", apiKey}});

    string response = httpsPost(context);

    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Response is not a JSON object");

    json::object &jsonObject = jsonValue.as_object();

    throwIf(jsonObject.contains("code") && jsonObject.contains("msg"), binanceErrorMessage(jsonObject));

    return createOrderInfo(jsonObject);
}

OrderInfo BinanceDealService::cancelOrder(const OrderQuery &request)
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(!request.orderId.has_value() && !request.clientOrderId.has_value(),
            "Either orderId or clientOrderId must be provided");

    auto timestamp = chrono::system_clock::now();
    ostringstream queryStream;
    queryStream << "symbol=" << request.symbol;

    if (request.orderId.has_value())
    {
        queryStream << "&orderId=" << *request.orderId;
    }
    if (request.clientOrderId.has_value())
    {
        queryStream << "&origClientOrderId=" << *request.clientOrderId;
    }

    queryStream << "&recvWindow=" << recvWindow
                << "&timestamp=" << chrono::duration_cast<chrono::milliseconds>(timestamp.time_since_epoch()).count();

    string queryString = queryStream.str();
    string signature = hmac_sha256(secretKey, queryString);
    string fullQuery = queryString + "&signature=" + signature;

    string target = "/api/v3/order?" + fullQuery;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::delete_);
    context.setRequestHeaders({{"X-MBX-APIKEY", apiKey}});

    string response = httpsPost(context);

    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "Binance cancelOrder: JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Response is not a JSON object");

    json::object &jsonObject = jsonValue.as_object();

    throwIf(jsonObject.contains("code") && jsonObject.contains("msg"), binanceErrorMessage(jsonObject));

    return createOrderInfo(jsonObject);
}

OrderInfo BinanceDealService::getOrder(const OrderQuery &request)
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(!request.orderId.has_value() && !request.clientOrderId.has_value(),
            "Either orderId or clientOrderId must be provided");

    auto timestamp = chrono::system_clock::now();
    ostringstream queryStream;
    queryStream << "symbol=" << request.symbol;

    if (request.orderId.has_value())
    {
        queryStream << "&orderId=" << *request.orderId;
    }
    if (request.clientOrderId.has_value())
    {
        queryStream << "&origClientOrderId=" << *request.clientOrderId;
    }

    queryStream << "&recvWindow=" << recvWindow
                << "&timestamp=" << chrono::duration_cast<chrono::milliseconds>(timestamp.time_since_epoch()).count();

    string queryString = queryStream.str();
    string signature = hmac_sha256(secretKey, queryString);
    string fullQuery = queryString + "&signature=" + signature;

    string target = "/api/v3/order?" + fullQuery;
    HttpRequestContext context(ioc, ctx, host, target); // GET request
    context.prepareRequest(http::verb::get);
    context.setRequestHeaders({{"X-MBX-APIKEY", apiKey}});

    string response = httpsPost(context);

    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "Binance getOrder: JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Response is not a JSON object");

    json::object &jsonObject = jsonValue.as_object();

    throwIf(jsonObject.contains("code") && jsonObject.contains("msg"), binanceErrorMessage(jsonObject));

    return createOrderInfo(jsonObject);
}

SymbolInfo BinanceDealService::getSymbolInfo(const string &symbol, const string &category)
{
    throwIf(symbol.empty(), "Symbol cannot be empty");

    {
        lock_guard<mutex> lock(symbolInfoMutex);
        auto it = symbolInfoCache.find(symbol);
        if (it != symbolInfoCache.end())
        {
            return it->second;
        }
    }

    string target = "/api/v3/exchangeInfo?symbol=" + symbol;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "Binance getSymbolInfo: JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Response is not a JSON object");

    json::object &rootObject = jsonValue.as_object();

    throwIf(!rootObject.contains("symbols") || !rootObject.at("symbols").is_array(),
            "Binance response missing 'symbols' array");

    json::array &symbols = rootObject.at("symbols").as_array();
    throwIf(symbols.empty(), "Binance symbol not found: " + symbol);

    throwIf(!symbols[0].is_object(), "Invalid symbol object");

    SymbolInfo info = createSymbolInfo(symbols[0].as_object());
    {
        lock_guard<mutex> lock(symbolInfoMutex);
        symbolInfoCache[symbol] = info;
    }
    return info;
}

OcoInfo BinanceDealService::placeOco(const PlaceOcoRequest &request)
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(request.quantity <= 0, "Quantity must be > 0");
    throwIf(request.price <= 0, "Price must be > 0");
    throwIf(request.stopPrice <= 0, "Stop Price must be > 0");

    if (request.stopLimitPrice.has_value() && *request.stopLimitPrice > 0)
    {
        throwIf(!request.stopLimitTimeInForce.has_value() || request.stopLimitTimeInForce->empty(),
                "stopLimitTimeInForce required if stopLimitPrice is set");
    }

    auto timestamp = chrono::system_clock::now();
    msec timestampMs = chrono::duration_cast<chrono::milliseconds>(timestamp.time_since_epoch()).count();

    string queryString = buildOcoQuery(request, timestampMs);
    string signature = hmac_sha256(secretKey, queryString);
    string fullQuery = queryString + "&signature=" + signature;

    string target = "/api/v3/orderList/oco?" + fullQuery;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders({{"X-MBX-APIKEY", apiKey}});

    string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "Binance placeOco: JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Response is not a JSON object");

    json::object &jsonObject = jsonValue.as_object();
    throwIf(jsonObject.contains("code") && jsonObject.contains("msg"), binanceErrorMessage(jsonObject));

    return createOcoInfo(jsonObject);
}

OcoInfo BinanceDealService::cancelOco(const OrderListQuery &request)
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(!request.orderListId.has_value() && !request.listClientOrderId.has_value(),
            "Either orderListId or listClientOrderId must be provided");

    auto timestamp = chrono::system_clock::now();
    msec timestampMs = chrono::duration_cast<chrono::milliseconds>(timestamp.time_since_epoch()).count();

    string queryString = buildOcoCancelQuery(request, timestampMs);
    string signature = hmac_sha256(secretKey, queryString);
    string fullQuery = queryString + "&signature=" + signature;

    string target = "/api/v3/orderList?" + fullQuery;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::delete_);
    context.setRequestHeaders({{"X-MBX-APIKEY", apiKey}});

    string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "Binance cancelOco: JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Response is not a JSON object");

    json::object &jsonObject = jsonValue.as_object();
    throwIf(jsonObject.contains("code") && jsonObject.contains("msg"), binanceErrorMessage(jsonObject));

    return createOcoInfo(jsonObject);
}

string BinanceDealService::buildOcoQuery(const PlaceOcoRequest &request, long long timestamp)
{
    throwIf(request.symbol.empty(), "Binance placeOco: symbol cannot be empty");
    throwIf(request.side.empty(), "Binance placeOco: side cannot be empty");
    throwIf(request.quantity <= 0, "Binance placeOco: quantity must be > 0");
    throwIf(request.price <= 0, "Binance placeOco: price must be > 0");
    throwIf(request.stopPrice <= 0, "Binance placeOco: stopPrice must be > 0");

    const SymbolInfo info = getSymbolInfo(request.symbol);
    const Decimal belowPrice = request.stopLimitPrice.has_value() ? *request.stopLimitPrice : request.stopPrice;
    const string belowTif = request.stopLimitTimeInForce.has_value() ? *request.stopLimitTimeInForce : string("GTC");

    ostringstream queryStream;
    // clang-format off
    queryStream << "symbol=" << request.symbol
                << "&side=" << request.side
                << "&quantity=" << DecimalConverter::formatByStep(request.quantity, info.stepSize)
                << "&aboveType=LIMIT_MAKER" << "&abovePrice="
                << DecimalConverter::formatByStep(request.price, info.tickSize)
                << "&belowType=STOP_LOSS_LIMIT"
                << "&belowStopPrice=" << DecimalConverter::formatByStep(request.stopPrice, info.tickSize)
                << "&belowPrice=" << DecimalConverter::formatByStep(belowPrice, info.tickSize)
                << "&belowTimeInForce=" << belowTif;
    // clang-format on

    if (request.listClientOrderId.has_value())
    {
        queryStream << "&listClientOrderId=" << *request.listClientOrderId;
    }

    if (request.limitClientOrderId.has_value())
    {
        queryStream << "&aboveClientOrderId=" << *request.limitClientOrderId;
    }

    if (request.stopClientOrderId.has_value())
    {
        queryStream << "&belowClientOrderId=" << *request.stopClientOrderId;
    }

    queryStream << "&recvWindow=" << recvWindow << "&timestamp=" << timestamp;

    return queryStream.str();
}

string BinanceDealService::buildOcoCancelQuery(const OrderListQuery &request, long long timestamp)
{
    ostringstream queryStringStream;
    queryStringStream << "symbol=" << request.symbol;

    if (request.orderListId.has_value())
    {
        queryStringStream << "&orderListId=" << *request.orderListId;
    }

    if (request.listClientOrderId.has_value())
    {
        queryStringStream << "&listClientOrderId=" << *request.listClientOrderId;
    }

    queryStringStream << "&recvWindow=" << recvWindow << "&timestamp=" << timestamp;

    return queryStringStream.str();
}

OcoInfo BinanceDealService::createOcoInfo(const json::object &jsonObject)
{
    OcoInfo info;

    if (jsonObject.contains("orderListId"))
    {
        if (jsonObject.at("orderListId").is_string())
        {
            info.orderListId = jsonObject.at("orderListId").as_string().c_str();
        }
        else
        {
            info.orderListId = to_string(jsonObject.at("orderListId").as_int64());
        }
    }
    else
    {
        throw runtime_error("Missing orderListId");
    }

    if (jsonObject.contains("listClientOrderId"))
    {
        info.listClientOrderId = jsonObject.at("listClientOrderId").as_string().c_str();
    }

    if (jsonObject.contains("transactionTime"))
    {
        info.transactTimeMs = jsonObject.at("transactionTime").as_int64();
    }

    throwIf(!jsonObject.contains("orderReports") || !jsonObject.at("orderReports").is_array(), "Missing orderReports");

    for (const auto &reportItem : jsonObject.at("orderReports").as_array())
    {
        if (reportItem.is_object())
        {
            const auto &reportObject = reportItem.as_object();
            OrderInfo orderInfo;

            if (reportObject.contains("symbol"))
            {
                orderInfo.symbol = reportObject.at("symbol").as_string().c_str();
            }

            if (reportObject.contains("orderId"))
            {
                if (reportObject.at("orderId").is_string())
                {
                    orderInfo.orderId = reportObject.at("orderId").as_string().c_str();
                }
                else
                {
                    orderInfo.orderId = to_string(reportObject.at("orderId").as_int64());
                }
            }

            if (reportObject.contains("clientOrderId"))
            {
                orderInfo.clientOrderId = reportObject.at("clientOrderId").as_string().c_str();
            }
            if (reportObject.contains("side"))
            {
                orderInfo.side = reportObject.at("side").as_string().c_str();
            }
            if (reportObject.contains("type"))
            {
                orderInfo.type = reportObject.at("type").as_string().c_str();
            }
            if (reportObject.contains("status"))
            {
                orderInfo.status = reportObject.at("status").as_string().c_str();
            }
            if (reportObject.contains("timeInForce"))
            {
                orderInfo.timeInForce = reportObject.at("timeInForce").as_string().c_str();
            }

            orderInfo.price = parseAmount(reportObject, "price");
            orderInfo.origQty = parseAmount(reportObject, "origQty");
            orderInfo.executedQty = parseAmount(reportObject, "executedQty");

            if (reportObject.contains("cummulativeQuoteQty"))
            {
                orderInfo.cumQuoteQty = parseAmount(reportObject, "cummulativeQuoteQty");
            }
            else if (reportObject.contains("cumulativeQuoteQty"))
            {
                orderInfo.cumQuoteQty = parseAmount(reportObject, "cumulativeQuoteQty");
            }

            if (reportObject.contains("transactTime"))
            {
                orderInfo.createdTimeMs = reportObject.at("transactTime").as_int64();
            }
            orderInfo.updatedTimeMs = orderInfo.createdTimeMs;

            orderInfo.leavesQty = orderInfo.origQty - orderInfo.executedQty;
            if (orderInfo.executedQty > 0)
            {
                orderInfo.avgPrice = orderInfo.cumQuoteQty / orderInfo.executedQty;
            }

            info.orders.push_back(orderInfo);
        }
    }

    return info;
}

SymbolInfo BinanceDealService::createSymbolInfo(const json::object &symbolObject)
{
    SymbolInfo info;
    info.symbol = symbolObject.at("symbol").as_string().c_str();
    info.status = symbolObject.at("status").as_string().c_str();
    info.baseAsset = symbolObject.at("baseAsset").as_string().c_str();
    info.quoteAsset = symbolObject.at("quoteAsset").as_string().c_str();

    if (symbolObject.contains("baseAssetPrecision"))
    {
        info.qtyPrecision = symbolObject.at("baseAssetPrecision").as_int64();
    }
    if (symbolObject.contains("quotePrecision"))
    {
        info.pricePrecision = symbolObject.at("quotePrecision").as_int64();
    }

    throwIf(!symbolObject.contains("filters") || !symbolObject.at("filters").is_array(), "Missing filters for symbol");

    for (const auto &filterValue : symbolObject.at("filters").as_array())
    {
        if (filterValue.is_object())
        {
            const auto &filter = filterValue.as_object();
            if (filter.contains("filterType"))
            {

                string type = filter.at("filterType").as_string().c_str();

                if (type == "PRICE_FILTER")
                {
                    info.minPrice = parseAmount(filter, "minPrice");
                    info.maxPrice = parseAmount(filter, "maxPrice");
                    info.tickSize = parseAmount(filter, "tickSize");
                }
                else if (type == "LOT_SIZE")
                {
                    info.minQty = parseAmount(filter, "minQty");
                    info.maxQty = parseAmount(filter, "maxQty");
                    info.stepSize = parseAmount(filter, "stepSize");
                }
                else if (type == "MIN_NOTIONAL")
                {
                    info.minNotional = parseAmount(filter, "minNotional");
                }
                else if (type == "NOTIONAL")
                {
                    info.minNotional = parseAmount(filter, "minNotional");
                    if (filter.contains("maxNotional"))
                    {
                        info.maxNotional = parseAmount(filter, "maxNotional");
                    }
                }
            }
        }
    }

    throwIf(info.tickSize <= 0, "Invalid tickSize");
    throwIf(info.stepSize <= 0, "Invalid stepSize");
    throwIf(info.minQty <= 0, "Invalid minQty");

    return info;
}

OrderInfo BinanceDealService::createOrderInfo(const json::object &obj)
{
    OrderInfo info;
    info.symbol = obj.at("symbol").as_string().c_str();

    if (obj.at("orderId").is_number())
    {
        info.orderId = to_string(obj.at("orderId").as_int64());
    }
    else if (obj.at("orderId").is_string())
    {
        info.orderId = obj.at("orderId").as_string().c_str();
    }

    if (obj.contains("clientOrderId"))
    {
        info.clientOrderId = obj.at("clientOrderId").as_string().c_str();
    }

    info.side = obj.at("side").as_string().c_str();
    info.type = obj.at("type").as_string().c_str();
    info.status = obj.at("status").as_string().c_str();

    if (obj.contains("timeInForce"))
    {
        info.timeInForce = obj.at("timeInForce").as_string().c_str();
    }

    info.price = parseAmount(obj, "price");
    info.origQty = parseAmount(obj, "origQty");
    info.executedQty = parseAmount(obj, "executedQty");

    if (obj.contains("cummulativeQuoteQty"))
    {
        info.cumQuoteQty = parseAmount(obj, "cummulativeQuoteQty");
    }
    else if (obj.contains("cumulativeQuoteQty"))
    {
        info.cumQuoteQty = parseAmount(obj, "cumulativeQuoteQty");
    }

    if (obj.contains("transactTime"))
    {
        info.createdTimeMs = obj.at("transactTime").as_int64();
    }

    info.updatedTimeMs = info.createdTimeMs;

    info.leavesQty = info.origQty - info.executedQty;
    if (info.executedQty > 0)
    {
        info.avgPrice = info.cumQuoteQty / info.executedQty;
    }

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

bool BinanceDealService::cancelAllOpenOrders(const string &symbol, const string &category)
{
    (void)category;

    throwIf(symbol.empty(), "Binance cancelAllOpenOrders: symbol cannot be empty");

    ostringstream queryStream;
    queryStream << "symbol=" << symbol << "&recvWindow=" << recvWindow << "&timestamp=" << getTimestamp();

    const string queryString = queryStream.str();
    const string signature = hmac_sha256(secretKey, queryString);
    const string target = "/api/v3/openOrders?" + queryString + "&signature=" + signature;

    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::delete_);
    context.setRequestHeaders({{"X-MBX-APIKEY", apiKey}});

    const string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "Binance cancelAllOpenOrders: JSON parse error: " + errorCode.message());

    if (jsonValue.is_object())
    {
        const json::object &jsonObject = jsonValue.as_object();
        if (jsonObject.contains("code") && jsonObject.contains("msg"))
        {
            long long code = jsonObject.at("code").is_number() ? jsonObject.at("code").as_int64() : 0;
            string msg = jsonObject.at("msg").is_string() ? string(jsonObject.at("msg").as_string().c_str()) : "";

            if (code == -2011 || code == -2013)
            {
                return true;
            }

            throw runtime_error("Binance Error " + to_string(code) + ": " + msg);
        }

        return true;
    }

    if (jsonValue.is_array())
    {
        return true;
    }

    throw runtime_error("Binance cancelAllOpenOrders: Unexpected response type");
}

flat_map<string, AssetBalance> BinanceDealService::getBalancesRest()
{
    ostringstream queryStream;
    queryStream << "recvWindow=" << recvWindow << "&timestamp=" << getTimestamp();

    const string queryString = queryStream.str();
    const string signature = hmac_sha256(secretKey, queryString);
    const string target = "/api/v3/account?" + queryString + "&signature=" + signature;

    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);
    context.setRequestHeaders({{"X-MBX-APIKEY", apiKey}});

    const string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "Binance getBalancesRest: JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Binance getBalancesRest: Response is not a JSON object");

    const json::object &jsonObject = jsonValue.as_object();
    if (jsonObject.contains("code") && jsonObject.contains("msg"))
    {
        long long code = jsonObject.at("code").is_number() ? jsonObject.at("code").as_int64() : 0;
        string msg = jsonObject.at("msg").is_string() ? string(jsonObject.at("msg").as_string().c_str()) : "";
        throw runtime_error("Binance Error " + to_string(code) + ": " + msg);
    }

    const auto it = jsonObject.find("balances");
    throwIf(it == jsonObject.end() || !it->value().is_array(), "Binance getBalancesRest: Missing 'balances' array");

    const json::array &balancesArray = it->value().as_array();
    for (const auto &balanceItem : balancesArray)
    {
        if (balanceItem.is_object())
        {
            const json::object &balanceObject = balanceItem.as_object();
            if (balanceObject.contains("asset") && balanceObject.at("asset").is_string())
            {
                const string asset = string(balanceObject.at("asset").as_string().c_str());
                const Decimal free = parseAmount(balanceObject, "free");
                const Decimal locked = parseAmount(balanceObject, "locked");

                updateBalanceCache(asset, free, locked);
            }
        }
    }

    return getBalances();
}

// add to the paper work like bad example of ai using
void BinanceDealService::waitUntilOrderFilled(const std::string &symbol, const std::string &orderId)
{
    cout << "Waiting for order " << orderId << " to be filled..." << endl;
    int retries = 0;
    while (true)
    {
        if (retries > 60) // 30 seconds
        {
            throw runtime_error("Timeout waiting for order " + orderId + " to fill");
        }
        try
        {
            OrderQuery orderQuery;
            orderQuery.symbol = symbol;
            orderQuery.orderId = orderId;

            OrderInfo orderInfo = getOrder(orderQuery);
            if (orderInfo.status == "FILLED" || orderInfo.status == "CANCELED" || orderInfo.status == "EXPIRED" ||
                orderInfo.status == "REJECTED")
            {
                cout << "Order " << orderId << " is " << orderInfo.status << endl;
                return;
            }
        }
        catch (const std::exception &e)
        {
            cerr << "Error waiting for order: " << e.what() << endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        retries++;
    }
}
