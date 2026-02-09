#include "BinanceDealService.hpp"
#include "common/EnumStringConverter.hpp"
#include "common/HttpRequestContext.hpp"
#include "common/http_request.hpp"

#include <chrono>
#include <sstream>
// DEBUG
#include <iostream>

#include "../common/DealUtils.hpp"
#include "../common/type_aliasing.hpp"

using namespace std;
using namespace binance;

string BinanceDealService::createQuery(const string &baseAsset,
                                       const string &quoteAsset,
                                       const OrderOperation &operation,
                                       const OrderType &type,
                                       double quantity,
                                       double stepSize)
{
    const std::string qtyStr = DealUtils::formatByStep(quantity, stepSize);
    DealUtils::verifyNoScientificNotation(qtyStr);

    auto timestamp = chrono::system_clock::now();
    ostringstream qs;
    // clang-format off
    qs << "symbol=" << baseAsset << quoteAsset
       << "&side=" << EnumStringConverter<OrderOperation>::toString(operation)
       << "&type=" << EnumStringConverter<OrderType>::toString(type)
       << "&quantity=" << qtyStr
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

bool BinanceDealService::binanceResponseOk(const string &response, string *errOut)
{
    boost::system::error_code ec;
    json::value val = json::parse(response, ec);
    if (ec)
    {
        if (errOut)
        {
            *errOut = "JSON parse error: " + ec.message();
        }
        return false;
    }
    if (!val.is_object())
    {
        if (errOut)
        {
            *errOut = "Response is not a JSON object";
        }
        return false;
    }

    const auto &obj = val.as_object();

    if (obj.contains("code") && obj.contains("msg"))
    {
        if (errOut)
        {
            long long code = 0;
            if (obj.at("code").is_number())
            {
                code = obj.at("code").as_int64();
            }
            string msg;
            if (obj.at("msg").is_string())
            {
                msg = obj.at("msg").as_string().c_str();
            }
            *errOut = "Binance Error " + to_string(code) + ": " + msg;
        }
        return false;
    }

    if (obj.contains("orderId") && obj.contains("status"))
    {
        return true;
    }

    if (errOut)
    {
        *errOut = "Unexpected response structure (missing orderId/status)";
    }
    return false;
}

bool BinanceDealService::sendOrder(const string &query, const flat_map<string, string> &headers)
{
    string target = "/api/v3/order?" + query;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);

    cout << "Sending order..." << endl;
    string response = httpsPost(context);
    cout << "Order response: " << response << endl;

    string err;
    if (!binanceResponseOk(response, &err))
    {
        cout << "Order failed: " << err << endl;
        return false;
    }
    return true;
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
    if (ec)
    {
        cerr << "User stream message parse error: " << ec.message() << endl;
        return;
    }
    if (!jsonValue.is_object())
    {
        return;
    }

    json::object &rootObject = jsonValue.as_object();
    json::object *eventObject = nullptr;

    if (rootObject.contains("e") && rootObject.at("e").is_string())
    {
        eventObject = &rootObject;
    }

    else if (rootObject.contains("event") && rootObject.at("event").is_object())
    {
        eventObject = &rootObject.at("event").as_object();
    }

    if (!eventObject)
    {
        return;
    }

    auto *eventTypeNameValue = eventObject->if_contains("e");
    if (!eventTypeNameValue || !eventTypeNameValue->is_string())
    {
        throw runtime_error("User stream message missing or invalid 'e' (event type) field");
    }

    const json::string &eventType = eventTypeNameValue->as_string();
    if (eventType != "outboundAccountPosition")
    {
        return;
    }

    auto *balancesArrayValue = eventObject->if_contains("B");
    if (!balancesArrayValue || !balancesArrayValue->is_array())
    {
        return;
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

    boost::system::error_code ec;
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

                    boost::system::error_code ec;
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
                        boost::system::error_code errorCode;

                        sharedWebsocketStream->read(buffer, errorCode);

                        if (errorCode)
                        {
                            if (!userStream)
                            {
                                cout << "User stream stopped gracefully." << endl;
                                break;
                            }
                            if (errorCode == boost::asio::error::operation_aborted)
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
        boost::system::error_code ec;
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

double BinanceDealService::getTickerPrice(const string &symbol)
{
    string target = "/api/v3/ticker/price?symbol=" + symbol;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    string response = httpsPost(context);

    boost::system::error_code ec;
    json::value val = json::parse(response, ec);
    if (!ec && val.is_object())
    {
        const auto &obj = val.as_object();
        if (obj.contains("price") && obj.at("price").is_string())
        {
            return strtod(obj.at("price").as_string().c_str(), nullptr);
        }
    }
    return 0.0;
}

double BinanceDealService::calculateSafeQty(const string &symbol,
                                            double quantity,
                                            double price,
                                            double stepSize,
                                            double minNotional)
{
    if (price <= 0 || stepSize <= 0)
    {
        return quantity;
    }

    double requiredQty = 0.0;
    if (minNotional > 0)
    {
        requiredQty = (minNotional * 1.10) / price;
    }

    double safeQty = max(quantity, requiredQty);

    safeQty = ceil(safeQty / stepSize) * stepSize;

    cout << "Binance Safe Qty: " << DealUtils::formatByStep(safeQty, stepSize) << " (Req: " << quantity
         << ", Price: " << price << ", MinNotional: " << minNotional << ", Step: " << stepSize << ")" << endl;

    return safeQty;
}

bool BinanceDealService::buyCrypto(const string &baseAsset, const string &quoteAsset, double quantity)
{
    string symbol = baseAsset + quoteAsset;
    double stepSize = 0.0;
    double minNotional = 0.0;

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

    double price = getTickerPrice(symbol);
    double safeQty = calculateSafeQty(symbol, quantity, price, stepSize, minNotional);

    string query = createQuery(baseAsset, quoteAsset, OrderOperation::BUY, OrderType::MARKET, safeQty, stepSize);
    flat_map<string, string> headers = createHeaders(apiKey);
    return sendOrder(query, headers);
}

bool BinanceDealService::sellCrypto(const string &baseAsset, const string &quoteAsset, double quantity)
{
    string symbol = baseAsset + quoteAsset;
    double stepSize = 0.0;
    double minNotional = 0.0;

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

    double price = getTickerPrice(symbol);
    double safeQty = calculateSafeQty(symbol, quantity, price, stepSize, minNotional);

    string query = createQuery(baseAsset, quoteAsset, OrderOperation::SELL, OrderType::MARKET, safeQty, stepSize);
    flat_map<string, string> headers = createHeaders(apiKey);
    return sendOrder(query, headers);
}

OrderInfo BinanceDealService::placeOrder(const PlaceOrderRequest &request)
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

    SymbolInfo info = getSymbolInfo(request.symbol);

    auto timestamp = chrono::system_clock::now();
    ostringstream qs;
    qs << "symbol=" << request.symbol << "&side=" << request.side << "&type=" << request.type
       << "&quantity=" << DealUtils::formatByStep(request.quantity, info.stepSize);

    if (request.type == "LIMIT")
    {
        qs << "&price=" << DealUtils::formatByStep(*request.price, info.tickSize)
           << "&timeInForce=" << *request.timeInForce;
    }

    if (request.clientOrderId.has_value() && !request.clientOrderId->empty())
    {
        qs << "&newClientOrderId=" << *request.clientOrderId;
    }

    qs << "&newOrderRespType=RESULT" << "&recvWindow=" << recvWindow
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
    {
        throw runtime_error("JSON parse error: " + ec.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Response is not a JSON object");
    }

    json::object &obj = jsonValue.as_object();

    if (obj.contains("code") && obj.contains("msg"))
    {
        throw runtime_error("Binance Error " + to_string(obj["code"].as_int64()) + ": " +
                            string(obj["msg"].as_string()));
    }

    return createOrderInfo(obj);
}

OrderInfo BinanceDealService::cancelOrder(const OrderQuery &request)
{
    if (request.symbol.empty())
    {
        throw runtime_error("Symbol cannot be empty");
    }
    if (!request.orderId.has_value() && !request.clientOrderId.has_value())
    {
        throw runtime_error("Either orderId or clientOrderId must be provided");
    }

    auto timestamp = chrono::system_clock::now();
    ostringstream qs;
    qs << "symbol=" << request.symbol;

    if (request.orderId.has_value())
    {
        qs << "&orderId=" << *request.orderId;
    }
    if (request.clientOrderId.has_value())
    {
        qs << "&origClientOrderId=" << *request.clientOrderId;
    }

    qs << "&recvWindow=" << recvWindow
       << "&timestamp=" << chrono::duration_cast<chrono::milliseconds>(timestamp.time_since_epoch()).count();

    string queryString = qs.str();
    string signature = hmac_sha256(secretKey, queryString);
    string fullQuery = queryString + "&signature=" + signature;

    string target = "/api/v3/order?" + fullQuery;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::delete_);
    context.setRequestHeaders({{"X-MBX-APIKEY", apiKey}});

    string response = httpsPost(context);

    boost::system::error_code ec;
    json::value jsonValue = json::parse(response, ec);
    if (ec)
    {
        throw runtime_error("Binance cancelOrder: JSON parse error: " + ec.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Response is not a JSON object");
    }

    json::object &obj = jsonValue.as_object();

    if (obj.contains("code") && obj.contains("msg"))
    {
        throw runtime_error("Binance Error " + to_string(obj["code"].as_int64()) + ": " +
                            string(obj["msg"].as_string()));
    }

    return createOrderInfo(obj);
}

OrderInfo BinanceDealService::getOrder(const OrderQuery &request)
{
    if (request.symbol.empty())
    {
        throw runtime_error("Symbol cannot be empty");
    }
    if (!request.orderId.has_value() && !request.clientOrderId.has_value())
    {
        throw runtime_error("Either orderId or clientOrderId must be provided");
    }

    auto timestamp = chrono::system_clock::now();
    ostringstream qs;
    qs << "symbol=" << request.symbol;

    if (request.orderId.has_value())
    {
        qs << "&orderId=" << *request.orderId;
    }
    if (request.clientOrderId.has_value())
    {
        qs << "&origClientOrderId=" << *request.clientOrderId;
    }

    qs << "&recvWindow=" << recvWindow
       << "&timestamp=" << chrono::duration_cast<chrono::milliseconds>(timestamp.time_since_epoch()).count();

    string queryString = qs.str();
    string signature = hmac_sha256(secretKey, queryString);
    string fullQuery = queryString + "&signature=" + signature;

    string target = "/api/v3/order?" + fullQuery;
    HttpRequestContext context(ioc, ctx, host, target); // GET request
    context.prepareRequest(http::verb::get);
    context.setRequestHeaders({{"X-MBX-APIKEY", apiKey}});

    string response = httpsPost(context);

    boost::system::error_code ec;
    json::value jsonValue = json::parse(response, ec);
    if (ec)
    {
        throw runtime_error("Binance getOrder: JSON parse error: " + ec.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Response is not a JSON object");
    }

    json::object &obj = jsonValue.as_object();

    if (obj.contains("code") && obj.contains("msg"))
    {
        throw runtime_error("Binance Error " + to_string(obj["code"].as_int64()) + ": " +
                            string(obj["msg"].as_string()));
    }

    return createOrderInfo(obj);
}

SymbolInfo BinanceDealService::getSymbolInfo(const string &symbol, const string &category)
{
    if (symbol.empty())
    {
        throw runtime_error("Symbol cannot be empty");
    }

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

    boost::system::error_code ec;
    json::value jsonValue = json::parse(response, ec);
    if (ec)
    {
        throw runtime_error("Binance getSymbolInfo: JSON parse error: " + ec.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Response is not a JSON object");
    }

    json::object &root = jsonValue.as_object();

    if (!root.contains("symbols") || !root.at("symbols").is_array())
    {
        throw runtime_error("Binance response missing 'symbols' array");
    }

    json::array &symbols = root.at("symbols").as_array();
    if (symbols.empty())
    {
        throw runtime_error("Binance symbol not found: " + symbol);
    }

    if (!symbols[0].is_object())
    {
        throw runtime_error("Invalid symbol object");
    }

    SymbolInfo info = createSymbolInfo(symbols[0].as_object());
    {
        lock_guard<mutex> lock(symbolInfoMutex);
        symbolInfoCache[symbol] = info;
    }
    return info;
}

OcoInfo BinanceDealService::placeOco(const PlaceOcoRequest &request)
{
    if (request.symbol.empty())
    {
        throw runtime_error("Symbol cannot be empty");
    }
    if (request.quantity <= 0)
    {
        throw runtime_error("Quantity must be > 0");
    }
    if (request.price <= 0)
    {
        throw runtime_error("Price must be > 0");
    }
    if (request.stopPrice <= 0)
    {
        throw runtime_error("Stop Price must be > 0");
    }

    if (request.stopLimitPrice.has_value() && *request.stopLimitPrice > 0)
    {
        if (!request.stopLimitTimeInForce.has_value() || request.stopLimitTimeInForce->empty())
        {
            throw runtime_error("stopLimitTimeInForce required if stopLimitPrice is set");
        }
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

    boost::system::error_code ec;
    json::value jsonValue = json::parse(response, ec);
    if (ec)
    {
        throw runtime_error("Binance placeOco: JSON parse error: " + ec.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Response is not a JSON object");
    }

    json::object &jsonObject = jsonValue.as_object();
    if (jsonObject.contains("code") && jsonObject.contains("msg"))
    {
        throw runtime_error("Binance Error " + to_string(jsonObject["code"].as_int64()) + ": " +
                            string(jsonObject["msg"].as_string()));
    }

    return createOcoInfo(jsonObject);
}

OcoInfo BinanceDealService::cancelOco(const OrderListQuery &request)
{
    if (request.symbol.empty())
    {
        throw runtime_error("Symbol cannot be empty");
    }
    if (!request.orderListId.has_value() && !request.listClientOrderId.has_value())
    {
        throw runtime_error("Either orderListId or listClientOrderId must be provided");
    }

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

    boost::system::error_code ec;
    json::value jsonValue = json::parse(response, ec);
    if (ec)
    {
        throw runtime_error("Binance cancelOco: JSON parse error: " + ec.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Response is not a JSON object");
    }

    json::object &jsonObject = jsonValue.as_object();
    if (jsonObject.contains("code") && jsonObject.contains("msg"))
    {
        throw runtime_error("Binance Error " + to_string(jsonObject["code"].as_int64()) + ": " +
                            string(jsonObject["msg"].as_string()));
    }

    return createOcoInfo(jsonObject);
}

string BinanceDealService::buildOcoQuery(const PlaceOcoRequest &request, long long timestamp)
{
    ostringstream queryStringStream;
    queryStringStream << "symbol=" << request.symbol << "&side=" << request.side << "&quantity=" << request.quantity
                      << "&price=" << request.price << "&stopPrice=" << request.stopPrice;

    if (request.stopLimitPrice.has_value())
    {
        queryStringStream << "&stopLimitPrice=" << *request.stopLimitPrice;
    }

    if (request.stopLimitTimeInForce.has_value())
    {
        queryStringStream << "&stopLimitTimeInForce=" << *request.stopLimitTimeInForce;
    }

    if (request.listClientOrderId.has_value())
    {
        queryStringStream << "&listClientOrderId=" << *request.listClientOrderId;
    }

    if (request.limitClientOrderId.has_value())
    {
        queryStringStream << "&limitClientOrderId=" << *request.limitClientOrderId;
    }

    if (request.stopClientOrderId.has_value())
    {
        queryStringStream << "&stopClientOrderId=" << *request.stopClientOrderId;
    }

    queryStringStream << "&recvWindow=" << recvWindow << "&timestamp=" << timestamp;

    return queryStringStream.str();
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

    if (!jsonObject.contains("orderReports") || !jsonObject.at("orderReports").is_array())
    {
        throw runtime_error("Missing orderReports");
    }

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

    if (!symbolObject.contains("filters") || !symbolObject.at("filters").is_array())
    {
        throw runtime_error("Missing filters for symbol");
    }

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

    if (info.tickSize <= 0)
    {
        throw runtime_error("Invalid tickSize");
    }
    if (info.stepSize <= 0)
    {
        throw runtime_error("Invalid stepSize");
    }
    if (info.minQty <= 0)
    {
        throw runtime_error("Invalid minQty");
    }

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