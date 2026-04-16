#include "BybitDealService.hpp"
#include "common/EnumStringConverter.hpp"
#include "common/http_request.hpp"
#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <sstream>
// DEBUG
#include <iostream>

#include "common/DealUtils.hpp"
#include "common/type_aliasing.hpp"

using namespace std;
using namespace bybit;

DealService::StreamStatus BybitDealService::getUserStreamStatus() const
{
    lock_guard<mutex> lock(streamStatusMutex);
    return streamStatus;
}

string BybitDealService::getUserStreamLastError() const
{
    lock_guard<mutex> lock(streamStatusMutex);
    return streamLastError;
}

void BybitDealService::setStreamStatus(StreamStatus status)
{
    lock_guard<mutex> lock(streamStatusMutex);
    streamStatus = status;
}

void BybitDealService::setStreamError(const string &error)
{
    lock_guard<mutex> lock(streamStatusMutex);
    streamStatus = StreamStatus::ERROR;
    streamLastError = error;
}

void BybitDealService::syncTime()
{
    string target = "/v5/market/time";
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);
    string response = httpsPost(context);

    beast::error_code ec;
    json::value val = json::parse(response, ec);
    if (!ec && val.is_object())
    {
        const auto &root = val.as_object();
        if (root.contains("result") && root.at("result").is_object())
        {
            const auto &res = root.at("result").as_object();
            if (res.contains("timeSecond") && res.at("timeSecond").is_string())
            {
                long long serverTime = stoll(res.at("timeSecond").as_string().c_str()) * 1000;
                long long localTime =
                    chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
                serverTimeOffset = serverTime - localTime;
                timeSynced = true;
                cout << "Bybit time synced. Offset: " << serverTimeOffset << "ms" << endl;
            }
        }
    }
}

long long BybitDealService::getTimestamp()
{
    if (!timeSynced)
    {
        syncTime();
    }
    long long localTime =
        chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    return localTime + serverTimeOffset;
}

void BybitDealService::refreshBalancesFromRest(const string &accountType, const optional<string> &coinFilter)
{
    ostringstream queryStream;
    queryStream << "accountType=" << accountType;
    if (coinFilter.has_value() && !coinFilter->empty())
    {
        queryStream << "&coin=" << *coinFilter;
    }
    const string queryString = queryStream.str();

    const msec timestamp = getTimestamp();
    const string signature = getSignature(queryString, timestamp);
    const auto headers = createHeaders(apiKey, signature, timestamp);

    const string target = "/v5/account/wallet-balance?" + queryString;
    HttpRequestContext requestContext(ioc, ctx, host, target);
    requestContext.prepareRequest(http::verb::get);
    requestContext.setRequestHeaders(headers);

    const string response = httpsPost(requestContext);

    string errorMessage;
    if (!bybitResponseOk(response, &errorMessage))
    {
        throw runtime_error("Bybit wallet-balance failed: " + errorMessage);
    }

    beast::error_code jsonError;
    json::value parsedValue = json::parse(response, jsonError);
    if (jsonError || !parsedValue.is_object())
    {
        throw runtime_error("Bybit wallet-balance JSON parse error");
    }

    const json::object &rootObject = parsedValue.as_object();
    const auto *resultValue = rootObject.if_contains("result");
    if (!resultValue || !resultValue->is_object())
    {
        return;
    }

    const json::object &resultObject = resultValue->as_object();
    const auto *listValue = resultObject.if_contains("list");
    if (!listValue || !listValue->is_array())
    {
        return;
    }

    const json::array &accountList = listValue->as_array();
    for (const json::value &accountValue : accountList)
    {
        if (!accountValue.is_object())
        {
            continue;
        }

        const json::object &accountObject = accountValue.as_object();
        const auto *coinListValue = accountObject.if_contains("coin");
        if (!coinListValue || !coinListValue->is_array())
        {
            continue;
        }

        const json::array &coinList = coinListValue->as_array();
        for (const json::value &coinValue : coinList)
        {
            if (!coinValue.is_object())
            {
                continue;
            }

            const json::object &coinObject = coinValue.as_object();
            const auto *coinNameValue = coinObject.if_contains("coin");
            if (!coinNameValue || !coinNameValue->is_string())
            {
                continue;
            }

            const string assetName = string(coinNameValue->as_string().c_str());

            const double walletBalance = parseAmount(coinObject, "walletBalance");

            double freeAmount = parseAmount(coinObject, "availableToWithdraw");
            if (freeAmount <= 0.0)
            {
                freeAmount = parseAmount(coinObject, "availableToTrade");
            }

            double lockedAmount = parseAmount(coinObject, "locked");

            if (freeAmount <= 0.0 && walletBalance > 0.0)
            {
                freeAmount = walletBalance - lockedAmount;
            }
            if (lockedAmount <= 0.0 && walletBalance > 0.0 && freeAmount > 0.0)
            {
                lockedAmount = max(0.0, walletBalance - freeAmount);
            }

            updateBalanceCache(assetName, freeAmount, lockedAmount);
        }
    }
}

void BybitDealService::ensureBalancesSeeded(const optional<string> &coinFilter)
{
    {
        lock_guard<mutex> lock(balanceMutex);

        if (coinFilter.has_value() && !coinFilter->empty())
        {
            if (balances.find(*coinFilter) != balances.end())
            {
                return;
            }
        }
        else
        {
            if (!balances.empty())
            {
                return;
            }
        }
    }

    try
    {
        refreshBalancesFromRest("UNIFIED", coinFilter);
    }
    catch (const exception &e)
    {
        cerr << "Bybit balance REST seed (UNIFIED) failed: " << e.what() << endl;
    }

    {
        lock_guard<mutex> lock(balanceMutex);

        if (coinFilter.has_value() && !coinFilter->empty())
        {
            if (balances.find(*coinFilter) != balances.end())
            {
                return;
            }
        }
        else
        {
            if (!balances.empty())
            {
                return;
            }
        }
    }

    try
    {
        refreshBalancesFromRest("SPOT", coinFilter);
    }
    catch (const exception &e)
    {
        cerr << "Bybit balance REST seed (SPOT) failed: " << e.what() << endl;
    }

    if (coinFilter.has_value() && !coinFilter->empty())
    {
        lock_guard<mutex> lock(balanceMutex);
        if (balances.find(*coinFilter) == balances.end())
        {
            balances.emplace(*coinFilter, AssetBalance{*coinFilter, 0.0, 0.0});
        }
    }
}

void BybitDealService::ensureBalancesSeeded(const string &coinFilter)
{
    ensureBalancesSeeded(optional<string>(coinFilter));
}

bool BybitDealService::capMarketQtyByBalance(bool isBuy,
                                             const string &baseAsset,
                                             const string &quoteAsset,
                                             const SymbolInfo &symbolInfo,
                                             double lastPrice,
                                             double &qtyInBase,
                                             string &reason)
{
    try
    {
        ensureBalancesSeeded(quoteAsset);
        ensureBalancesSeeded(baseAsset);
    }
    catch (const exception &e)
    {
        cerr << "Bybit capMarketQtyByBalance: ensureBalancesSeeded failed (continuing): " << e.what() << endl;
    }

    const double stepSize = symbolInfo.stepSize > 0.0 ? symbolInfo.stepSize : 0.0;
    const double minQty = symbolInfo.minQty > 0.0 ? symbolInfo.minQty : 0.0;
    const double minNotional = symbolInfo.minNotional > 0.0 ? symbolInfo.minNotional : 0.0;

    if (qtyInBase <= 0.0)
    {
        reason = "requested quantity <= 0";
        return false;
    }

    const double price = lastPrice;

    if (isBuy)
    {
        const auto quoteBalance = getBalance(quoteAsset);
        const double quoteFree = quoteBalance.has_value() ? quoteBalance->free : 0.0;

        if (quoteFree <= 0.0)
        {
            reason = "no free " + quoteAsset + " balance";
            return false;
        }
        if (price <= 0.0)
        {
            reason = "missing last price";
            return false;
        }

        const double maximumQtyByQuote = (quoteFree * 0.99) / price;
        qtyInBase = min(qtyInBase, maximumQtyByQuote);
    }
    else
    {
        const auto baseBalance = getBalance(baseAsset);
        const double baseFree = baseBalance.has_value() ? baseBalance->free : 0.0;

        if (baseFree <= 0.0)
        {
            reason = "no free " + baseAsset + " balance";
            return false;
        }

        qtyInBase = min(qtyInBase, baseFree);
    }

    if (stepSize > 0.0)
    {
        qtyInBase = floor(qtyInBase / stepSize) * stepSize;
    }

    if (minQty > 0.0 && qtyInBase < minQty)
    {
        reason = "quantity below minQty after balance cap";
        return false;
    }

    if (minNotional > 0.0 && price > 0.0 && (qtyInBase * price) < minNotional)
    {
        reason = "notional below minOrderAmt after balance cap";
        return false;
    }

    return true;
}

void BybitDealService::startUserStream()
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
        cerr << "Bybit REST balances seed failed (continuing): " << e.what() << endl;
    }

    userStream = true;
    setStreamStatus(StreamStatus::CONNECTING);

    runner = thread(
        [this]()
        {
            try
            {
                const string wsPort = "443";
                const string wsTarget = "/v5/private";

                tcp::resolver resolver(ioc);
                auto results = resolver.resolve(websocketHost, wsPort);

                beast::ssl_stream<beast::tcp_stream> tls(ioc, ctx);

                if (!SSL_set_tlsext_host_name(tls.native_handle(), websocketHost.c_str()))
                {
                    throw beast::system_error(
                        beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
                        "Bybit SSL_set_tlsext_host_name");
                }

                beast::get_lowest_layer(tls).connect(results);
                tls.handshake(ssl::stream_base::client);

                auto sharedWebsocketStream = make_shared<WebsocketStream>(move(tls));
                sharedWebsocketStream->set_option(ws::stream_base::timeout::suggested(beast::role_type::client));
                sharedWebsocketStream->handshake(websocketHost, wsTarget);

                {
                    lock_guard<mutex> lock(userWebsocketMutex);
                    userWebsocketStream = sharedWebsocketStream;
                }

                setStreamStatus(StreamStatus::CONNECTING);

                const long long expires = getTimestamp() + 5000;
                const string payload = "GET/realtime" + to_string(expires);
                const string sig = hmac_sha256(secretKey, payload);

                json::object auth;
                auth["op"] = "auth";
                auth["args"] = json::array{apiKey, expires, sig};
                sharedWebsocketStream->write(net::buffer(json::serialize(auth)));

                {
                    beast::flat_buffer buffer;
                    sharedWebsocketStream->read(buffer);
                    string msg = beast::buffers_to_string(buffer.data());

                    beast::error_code ec;
                    json::value val = json::parse(msg, ec);
                    if (!ec && val.is_object())
                    {
                        json::object &root = val.as_object();
                        if (root.contains("op") && root.at("op").as_string() == "auth")
                        {
                            if (!root.contains("success") || !root.at("success").as_bool())
                            {
                                throw runtime_error("Bybit stream: auth failed: " + msg);
                            }
                            cout << "Bybit stream authenticated successfully." << endl;
                        }
                    }
                }

                json::object sub;
                sub["op"] = "subscribe";
                sub["args"] = json::array{"wallet", "order"};
                sharedWebsocketStream->write(net::buffer(json::serialize(sub)));
                {
                    beast::flat_buffer buffer;
                    sharedWebsocketStream->read(buffer);
                    string msg = beast::buffers_to_string(buffer.data());
                    cout << "Bybit stream subscription response: " << msg << endl;
                }

                setStreamStatus(StreamStatus::CONNECTED);
                try
                {
                    ensureBalancesSeeded();
                }
                catch (const exception &exception)
                {
                    cerr << "Bybit: initial balance seed failed: " << exception.what() << endl;
                }

                shared_ptr<WebsocketStream> websocketStreamCopy;
                {
                    lock_guard<mutex> lock(userWebsocketMutex);
                    websocketStreamCopy = userWebsocketStream;
                }

                while (userStream)
                {
                    beast::flat_buffer readBuffer;
                    beast::error_code errorCode;

                    websocketStreamCopy->read(readBuffer, errorCode);

                    if (errorCode)
                    {
                        if (!userStream || errorCode == net::error::operation_aborted || errorCode == net::error::eof ||
                            errorCode == ssl::error::stream_truncated || errorCode == ws::error::closed)
                        {
                            break;
                        }

                        continue;
                    }

                    const string message = beast::buffers_to_string(readBuffer.data());
                    handleUserStreamMessage(message);
                }
            }
            catch (const exception &e)
            {
                if (userStream)
                {
                    cerr << "Bybit stream error: " << e.what() << endl;
                    setStreamError(e.what());
                }
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

string BybitDealService::createBody(const string &baseAsset,
                                    const string &quoteAsset,
                                    const OrderCategory &category,
                                    const OrderOperation &operation,
                                    const OrderType &type,
                                    double quantity,
                                    double stepSize)
{
    const string qtyStr = DealUtils::formatByStep(quantity, stepSize);
    DealUtils::verifyNoScientificNotation(qtyStr);

    ostringstream body;
    body << "{" << "\"category\":\"" << EnumStringConverter<OrderCategory>::toString(category) << "\","
         << "\"symbol\":\"" << baseAsset << quoteAsset << "\"," << "\"side\":\""
         << EnumStringConverter<OrderOperation>::toString(operation) << "\"," << "\"orderType\":\""
         << EnumStringConverter<OrderType>::toString(type) << "\",";

    // IMPORTANT: For spot MARKET BUY, force qty to be interpreted as baseCoin amount
    if (type == OrderType::MARKET && operation == OrderOperation::BUY)
    {
        body << "\"marketUnit\":\"baseCoin\",";
    }

    body << "\"qty\":\"" << qtyStr << "\"" << "}";

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

void BybitDealService::stopUserStream()
{
    userStream = false;

    shared_ptr<WebsocketStream> websocketStreamCopy;
    {
        lock_guard<mutex> lock(userWebsocketMutex);
        websocketStreamCopy = userWebsocketStream;
    }

    if (websocketStreamCopy)
    {
        beast::error_code errorCode;
        auto &lowestLayer = beast::get_lowest_layer(*websocketStreamCopy);

        lowestLayer.cancel();

        lowestLayer.socket().shutdown(tcp::socket::shutdown_both, errorCode);
        lowestLayer.socket().close(errorCode);
    }

    if (runner.joinable())
    {
        runner.join();
    }

    setStreamStatus(StreamStatus::STOPPED);

    {
        lock_guard<mutex> lock(userWebsocketMutex);
        userWebsocketStream.reset();
    }
}

bool BybitDealService::bybitResponseOk(const string &response, string *errOut)
{
    beast::error_code ec;
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
    if (obj.contains("retCode") && obj.at("retCode").is_number())
    {
        long long retCode = obj.at("retCode").as_int64();
        if (retCode != 0)
        {
            if (errOut)
            {
                string msg = "Unknown Error";
                if (obj.contains("retMsg") && obj.at("retMsg").is_string())
                {
                    msg = obj.at("retMsg").as_string().c_str();
                }
                *errOut = "Bybit Error " + to_string(retCode) + ": " + msg;
            }
            return false;
        }
    }
    else
    {
        if (errOut)
        {
            *errOut = "Missing retCode in response";
        }
        return false;
    }

    return true;
}

string BybitDealService::sendOrder(const string &body, const flat_map<string, string> &headers)
{
    cout << "Sending order..." << endl;
    string target = "/v5/order/create";
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);
    context.setRequestBody(body);
    string response = httpsPost(context);
    cout << "Order response: " << response << endl;

    string err;
    if (!bybitResponseOk(response, &err))
    {
        throw runtime_error("Order failed: " + err);
    }
    return response;
}

double BybitDealService::getTickerPrice(const string &symbol)
{
    string target = "/v5/market/tickers?category=spot&symbol=" + symbol;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    string response = httpsPost(context);

    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    if (!errorCode && jsonValue.is_object())
    {
        const auto &rootObject = jsonValue.as_object();
        if (rootObject.contains("result") && rootObject.at("result").is_object())
        {
            const auto &resultObject = rootObject.at("result").as_object();
            if (resultObject.contains("list") && resultObject.at("list").is_array())
            {
                const auto &tickerList = resultObject.at("list").as_array();
                if (!tickerList.empty() && tickerList[0].is_object())
                {
                    const auto &tickerItem = tickerList[0].as_object();
                    if (tickerItem.contains("lastPrice") && tickerItem.at("lastPrice").is_string())
                    {
                        return strtod(tickerItem.at("lastPrice").as_string().c_str(), nullptr);
                    }
                }
            }
        }
    }
    return 0.0;
}

OrderInfo BybitDealService::buyCrypto(const string &baseAsset, const string &quoteAsset, double quantity)
{
    const string symbol = baseAsset + quoteAsset;

    SymbolInfo symbolInfo;
    double stepSize = 0.0;
    double minOrderAmount = 0.0;

    try
    {
        symbolInfo = getSymbolInfo(symbol);
        stepSize = symbolInfo.stepSize;
        minOrderAmount = symbolInfo.minNotional;
    }
    catch (...)
    {
        cerr << "Failed to get symbol info for " << symbol << ", using defaults" << endl;
    }

    const double lastPrice = getTickerPrice(symbol);

    double minimumQuantityForNotional = 0.0;
    if (lastPrice > 0.0 && minOrderAmount > 0.0)
    {
        minimumQuantityForNotional = (minOrderAmount * 1.10) / lastPrice;
    }

    double safeQuantity = max(quantity, minimumQuantityForNotional);
    if (stepSize > 0.0)
    {
        safeQuantity = ceil(safeQuantity / stepSize) * stepSize;
    }

    if (symbolInfo.stepSize > 0.0 && symbolInfo.minQty > 0.0)
    {
        string reason;
        double cappedQuantity = safeQuantity;

        if (!capMarketQtyByBalance(true, baseAsset, quoteAsset, symbolInfo, lastPrice, cappedQuantity, reason))
        {
            ostringstream messageStream;
            messageStream << "Bybit BUY aborted: " << reason << " (requestedQty=" << DealUtils::formatByStep(safeQuantity, stepSize) << ")";
            throw runtime_error(messageStream.str());
        }

        safeQuantity = cappedQuantity;
    }

    cout << "Bybit Safe Qty: " << DealUtils::formatByStep(safeQuantity, stepSize) << " (Req: " << quantity
         << ", Price: " << lastPrice << ", MinOrderAmt: " << minOrderAmount << ", Step: " << stepSize << ")" << endl;

    const msec timestamp = getTimestamp();
    const string body = createBody(baseAsset,
                                   quoteAsset,
                                   OrderCategory::SPOT,
                                   OrderOperation::BUY,
                                   OrderType::MARKET,
                                   safeQuantity,
                                   stepSize);

    const string signature = getSignature(body, timestamp);
    const flat_map<string, string> headers = createHeaders(apiKey, signature, timestamp);
    
    string response = sendOrder(body, headers);
    
    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    if (errorCode || !jsonValue.is_object())
    {
         throw runtime_error("Failed to parse buyCrypto response");
    }
    
    const json::object &jsonObject = jsonValue.as_object();
    if (!jsonObject.contains("result") || !jsonObject.at("result").is_object())
    {
        throw runtime_error("Missing result in buyCrypto response");
    }
    const json::object &resultObject = jsonObject.at("result").as_object();
    
    OrderInfo info;
    info.symbol = symbol;
    info.category = "spot";
    info.side = "Buy";
    info.type = "Market";
    info.status = "New";
    info.origQty = safeQuantity;
    info.leavesQty = safeQuantity;
    
    if (resultObject.contains("orderId")) info.orderId = resultObject.at("orderId").as_string().c_str();
    if (resultObject.contains("orderLinkId")) info.clientOrderId = resultObject.at("orderLinkId").as_string().c_str();
    
    info.createdTimeMs = timestamp;
    info.updatedTimeMs = timestamp;
    
    return info;
}

OrderInfo BybitDealService::sellCrypto(const string &baseAsset, const string &quoteAsset, double quantity)
{
    const string symbol = baseAsset + quoteAsset;

    SymbolInfo symbolInfo;
    double stepSize = 0.0;
    double minOrderAmount = 0.0;

    try
    {
        symbolInfo = getSymbolInfo(symbol);
        stepSize = symbolInfo.stepSize;
        minOrderAmount = symbolInfo.minNotional;
    }
    catch (...)
    {
        cerr << "Failed to get symbol info for " << symbol << ", using defaults" << endl;
    }

    const double lastPrice = getTickerPrice(symbol);

    double minimumQuantityForNotional = 0.0;
    if (lastPrice > 0.0 && minOrderAmount > 0.0)
    {
        minimumQuantityForNotional = (minOrderAmount * 1.10) / lastPrice;
    }

    double safeQuantity = max(quantity, minimumQuantityForNotional);
    if (stepSize > 0.0)
    {
        safeQuantity = ceil(safeQuantity / stepSize) * stepSize;
    }

    if (symbolInfo.stepSize > 0.0 && symbolInfo.minQty > 0.0)
    {
        string reason;
        double cappedQuantity = safeQuantity;

        if (!capMarketQtyByBalance(false, baseAsset, quoteAsset, symbolInfo, lastPrice, cappedQuantity, reason))
        {
            ostringstream messageStream;
            messageStream << "Bybit SELL aborted: " << reason << " (requestedQty=" << DealUtils::formatByStep(safeQuantity, stepSize) << ")";
            throw runtime_error(messageStream.str());
        }

        safeQuantity = cappedQuantity;
    }

    cout << "Bybit Safe Qty: " << DealUtils::formatByStep(safeQuantity, stepSize) << " (Req: " << quantity
         << ", Price: " << lastPrice << ", MinOrderAmt: " << minOrderAmount << ", Step: " << stepSize << ")" << endl;

    const msec timestamp = getTimestamp();
    const string body = createBody(baseAsset,
                                   quoteAsset,
                                   OrderCategory::SPOT,
                                   OrderOperation::SELL,
                                   OrderType::MARKET,
                                   safeQuantity,
                                   stepSize);

    const string signature = getSignature(body, timestamp);
    const flat_map<string, string> headers = createHeaders(apiKey, signature, timestamp);
    
    string response = sendOrder(body, headers);
    
    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    if (errorCode || !jsonValue.is_object())
    {
         throw runtime_error("Failed to parse sellCrypto response");
    }
    
    const json::object &jsonObject = jsonValue.as_object();
    if (!jsonObject.contains("result") || !jsonObject.at("result").is_object())
    {
        throw runtime_error("Missing result in sellCrypto response");
    }
    const json::object &resultObject = jsonObject.at("result").as_object();
    
    OrderInfo info;
    info.symbol = symbol;
    info.category = "spot";
    info.side = "Sell";
    info.type = "Market";
    info.status = "New";
    info.origQty = safeQuantity;
    info.leavesQty = safeQuantity;
    
    if (resultObject.contains("orderId")) info.orderId = resultObject.at("orderId").as_string().c_str();
    if (resultObject.contains("orderLinkId")) info.clientOrderId = resultObject.at("orderLinkId").as_string().c_str();
    
    info.createdTimeMs = timestamp;
    info.updatedTimeMs = timestamp;
    
    return info;
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

    string side = (request.side == "BUY") ? "Buy" : "Sell";        // TODO solve compare issue
    string type = (request.type == "MARKET") ? "Market" : "Limit"; // TODO solve compare issue
    string category = request.category;
    if (category.empty())
    {
        category = "spot";
    }

    // Fetch symbol info for precision
    SymbolInfo info = getSymbolInfo(request.symbol, category);
    try
    {
        const bool isBuyOrder = (request.side == "BUY");
        const string &baseAsset = info.baseAsset;
        const string &quoteAsset = info.quoteAsset;

        if (isBuyOrder)
        {
            ensureBalancesSeeded(quoteAsset);

            const auto quoteBalance = getBalance(quoteAsset);
            const double quoteFree = quoteBalance.has_value() ? quoteBalance->free : 0.0;

            double requiredQuoteAmount = 0.0;

            if (request.type == "LIMIT" && request.price.has_value())
            {
                requiredQuoteAmount = request.quantity * (*request.price);
            }
            else
            {
                if (request.marketUnit.has_value() && *request.marketUnit == "quoteCoin")
                {
                    requiredQuoteAmount = request.quantity;
                }
                else
                {
                    const double lastPrice = getTickerPrice(request.symbol);
                    if (lastPrice > 0.0)
                    {
                        requiredQuoteAmount = request.quantity * lastPrice;
                    }
                }
            }

            if (requiredQuoteAmount > 0.0)
            {
                requiredQuoteAmount *= 1.01;
                if (quoteFree < requiredQuoteAmount)
                {
                    ostringstream messageStream;
                    messageStream << "Insufficient balance: need ~" << requiredQuoteAmount << " " << quoteAsset
                                  << ", have " << quoteFree;
                    throw runtime_error(messageStream.str());
                }
            }
            else if (quoteFree <= 0.0)
            {
                throw runtime_error("Insufficient balance: no free " + quoteAsset);
            }
        }
        else
        {
            ensureBalancesSeeded(baseAsset);

            const auto baseBalance = getBalance(baseAsset);
            const double baseFree = baseBalance.has_value() ? baseBalance->free : 0.0;

            if (baseFree < request.quantity)
            {
                ostringstream messageStream;
                messageStream << "Insufficient balance: need " << request.quantity << " " << baseAsset << ", have "
                              << baseFree;
                throw runtime_error(messageStream.str());
            }
        }
    }
    catch (const exception &exception)
    {
        throw runtime_error(string("Bybit placeOrder: ") + exception.what());
    }

    json::object body;
    body["category"] = category;
    body["symbol"] = request.symbol;
    body["side"] = side;
    body["orderType"] = type;

    // Use formatByStep for qty and price to avoid scientific notation and ensure correct precision
    body["qty"] = DealUtils::formatByStep(request.quantity, info.stepSize);

    if (request.type == "LIMIT")
    {
        body["price"] = DealUtils::formatByStep(*request.price, info.tickSize);
        body["timeInForce"] = *request.timeInForce;
    }

    if (request.clientOrderId.has_value() && !request.clientOrderId->empty())
    {
        body["orderLinkId"] = *request.clientOrderId;
    }

    if (request.triggerPrice.has_value() && !request.triggerPrice->empty())
    {
        body["triggerPrice"] = *request.triggerPrice;
    }
    if (request.orderFilter.has_value() && !request.orderFilter->empty())
    {
        body["orderFilter"] = *request.orderFilter;
    }
    if (request.marketUnit.has_value() && !request.marketUnit->empty())
    {
        body["marketUnit"] = *request.marketUnit;
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

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    if (errorCode)
    {
        throw runtime_error("JSON parse error: " + errorCode.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Response is not a JSON object");
    }

    json::object &jsonObject = jsonValue.as_object();

    int retCode = -1;
    if (jsonObject.contains("retCode") && jsonObject.at("retCode").is_number())
    {
        retCode = jsonObject.at("retCode").as_int64();
    }

    if (retCode != 0)
    {
        string message = "Unknown Error";
        if (jsonObject.contains("retMsg") && jsonObject.at("retMsg").is_string())
        {
            message = jsonObject.at("retMsg").as_string().c_str();
        }
        throw runtime_error("Bybit Error " + to_string(retCode) + ": " + message);
    }

    if (!jsonObject.contains("result") || !jsonObject.at("result").is_object())
    {
        throw runtime_error("Missing result object in response");
    }

    json::object &result = jsonObject.at("result").as_object();

    return createOrderInfo(result, request, side, type, timestamp);
}

OrderInfo BybitDealService::cancelOrder(const OrderQuery &request)
{
    if (request.symbol.empty())
    {
        throw runtime_error("Symbol cannot be empty");
    }
    if (!request.orderId.has_value() && !request.clientOrderId.has_value())
    {
        throw runtime_error("Either orderId or clientOrderId must be provided");
    }

    string category = request.category;
    if (category.empty())
    {
        category = "spot";
    }

    json::object body;
    body["category"] = category;
    body["symbol"] = request.symbol;

    if (request.orderId.has_value())
    {
        body["orderId"] = *request.orderId;
    }
    if (request.clientOrderId.has_value())
    {
        body["orderLinkId"] = *request.clientOrderId;
    }

    string bodyStr = json::serialize(body);

    msec timestamp = getTimestamp();
    string signature = getSignature(bodyStr, timestamp);
    auto headers = createHeaders(apiKey, signature, timestamp);
    string target = "/v5/order/cancel";

    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);
    context.setRequestBody(bodyStr);

    string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    if (errorCode)
    {
        throw runtime_error("Bybit cancelOrder: JSON parse error: " + errorCode.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Response is not a JSON object");
    }

    json::object &jsonObject = jsonValue.as_object();

    int retCode = -1;
    if (jsonObject.contains("retCode") && jsonObject.at("retCode").is_number())
    {
        retCode = jsonObject.at("retCode").as_int64();
    }

    if (retCode != 0)
    {
        string message = "Unknown Error";
        if (jsonObject.contains("retMsg") && jsonObject.at("retMsg").is_string())
        {
            message = jsonObject.at("retMsg").as_string().c_str();
        }
        throw runtime_error("Bybit Error " + to_string(retCode) + ": " + message);
    }

    if (!jsonObject.contains("result") || !jsonObject.at("result").is_object())
    {
        throw runtime_error("Missing result object in response");
    }

    json::object &result = jsonObject.at("result").as_object();

    return createOrderInfo(result, request, timestamp);
}

OrderInfo BybitDealService::getOrder(const OrderQuery &request)
{
    if (request.symbol.empty())
    {
        throw runtime_error("Symbol cannot be empty");
    }
    if (!request.orderId.has_value() && !request.clientOrderId.has_value())
    {
        throw runtime_error("Either orderId or clientOrderId must be provided");
    }

    string category = request.category;
    if (category.empty())
    {
        category = "spot";
    }

    ostringstream queryStream;
    queryStream << "category=" << category << "&symbol=" << request.symbol;

    if (request.orderId.has_value())
    {
        queryStream << "&orderId=" << *request.orderId;
    }
    if (request.clientOrderId.has_value())
    {
        queryStream << "&orderLinkId=" << *request.clientOrderId;
    }

    string queryString = queryStream.str();
    msec timestamp = getTimestamp();

    string signature = getSignature(queryString, timestamp);

    auto headers = createHeaders(apiKey, signature, timestamp);

    string target = "/v5/order/realtime?" + queryString;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);
    context.setRequestHeaders(headers);

    string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    if (errorCode)
    {
        throw runtime_error("Bybit getOrder: JSON parse error: " + errorCode.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Response is not a JSON object");
    }

    json::object &jsonObject = jsonValue.as_object();

    int retCode = -1;
    if (jsonObject.contains("retCode") && jsonObject.at("retCode").is_number())
    {
        retCode = jsonObject.at("retCode").as_int64();
    }

    if (retCode != 0)
    {
        string message = "Unknown Error";
        if (jsonObject.contains("retMsg") && jsonObject.at("retMsg").is_string())
        {
            message = jsonObject.at("retMsg").as_string().c_str();
        }
        throw runtime_error("Bybit Error " + to_string(retCode) + ": " + message);
    }

    if (!jsonObject.contains("result") || !jsonObject.at("result").is_object())
    {
        throw runtime_error("Missing result object in response");
    }
    json::object &result = jsonObject.at("result").as_object();

    if (!result.contains("list") || !result.at("list").is_array())
    {
        throw runtime_error("Missing or invalid list in response");
    }
    json::array &orderList = result.at("list").as_array();

    if (orderList.empty())
    {
        throw runtime_error("Order not found (empty list)");
    }
    if (!orderList[0].is_object())
    {
        throw runtime_error("Invalid order object in list");
    }
    const json::object &orderObject = orderList[0].as_object();

    return createDetailedOrderInfo(orderObject, request, category, timestamp);
}

OrderInfo BybitDealService::createOrderInfo(const json::object &result, const OrderQuery &request, msec timestamp)
{
    OrderInfo info;
    info.symbol = request.symbol;
    info.category = request.category;
    info.status = "Cancelled";
    info.updatedTimeMs = timestamp;

    if (request.orderId.has_value())
    {
        info.orderId = *request.orderId;
    }
    else if (result.contains("orderId") && result.at("orderId").is_string())
    {
        info.orderId = result.at("orderId").as_string().c_str();
    }

    if (request.clientOrderId.has_value())
    {
        info.clientOrderId = *request.clientOrderId;
    }
    else if (result.contains("orderLinkId") && result.at("orderLinkId").is_string())
    {
        info.clientOrderId = result.at("orderLinkId").as_string().c_str();
    }

    info.executedQty = 0.0;
    info.cumQuoteQty = 0.0;
    info.avgPrice = 0.0;
    info.origQty = 0.0;
    info.leavesQty = 0.0;

    return info;
}

OrderInfo BybitDealService::createOrderInfo(const json::object &result,
                                            const PlaceOrderRequest &request,
                                            const string &side,
                                            const string &type,
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

SymbolInfo BybitDealService::getSymbolInfo(const string &symbol, const string &category)
{
    if (symbol.empty())
    {
        throw runtime_error("Symbol cannot be empty");
    }

    string effectiveCategory = category.empty() ? "spot" : category;

    {
        lock_guard<mutex> lock(symbolInfoMutex);
        auto it = symbolInfoCache.find(symbol);
        if (it != symbolInfoCache.end())
        {
            return it->second;
        }
    }

    string target = "/v5/market/instruments-info?category=" + effectiveCategory + "&symbol=" + symbol;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    if (errorCode)
    {
        throw runtime_error("Bybit getSymbolInfo: JSON parse error: " + errorCode.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Response is not a JSON object");
    }

    json::object &rootObject = jsonValue.as_object();

    int retCode = -1;
    if (rootObject.contains("retCode") && rootObject.at("retCode").is_number())
    {
        retCode = rootObject.at("retCode").as_int64();
    }

    if (retCode != 0)
    {
        string message = "Unknown Error";
        if (rootObject.contains("retMsg") && rootObject.at("retMsg").is_string())
        {
            message = rootObject.at("retMsg").as_string().c_str();
        }
        throw runtime_error("Bybit Error " + to_string(retCode) + ": " + message);
    }

    if (!rootObject.contains("result") || !rootObject.at("result").is_object())
    {
        throw runtime_error("Missing result object");
    }

    json::object &resultObject = rootObject.at("result").as_object();

    if (!resultObject.contains("list") || !resultObject.at("list").is_array())
    {
        throw runtime_error("Missing list in result");
    }

    json::array &instrumentList = resultObject.at("list").as_array();
    if (instrumentList.empty())
    {
        throw runtime_error("Symbol not found: " + symbol);
    }

    if (!instrumentList[0].is_object())
    {
        throw runtime_error("Invalid instrument object");
    }

    SymbolInfo info = createSymbolInfo(instrumentList[0].as_object(), symbol);
    {
        lock_guard<mutex> lock(symbolInfoMutex);
        symbolInfoCache[symbol] = info;
    }
    return info;
}

OrderInfo BybitDealService::createDetailedOrderInfo(const json::object &orderObj,
                                                    const OrderQuery &request,
                                                    const string &category,
                                                    msec timestamp)
{
    OrderInfo info;
    if (orderObj.contains("symbol"))
    {
        info.symbol = orderObj.at("symbol").as_string().c_str();
    }
    else
    {
        info.symbol = request.symbol;
    }

    info.category = category;

    if (orderObj.contains("orderId"))
    {
        info.orderId = orderObj.at("orderId").as_string().c_str();
    }
    else if (request.orderId.has_value())
    {
        info.orderId = *request.orderId;
    }

    if (orderObj.contains("orderLinkId"))
    {
        info.clientOrderId = orderObj.at("orderLinkId").as_string().c_str();
    }
    else if (request.clientOrderId.has_value())
    {
        info.clientOrderId = *request.clientOrderId;
    }

    if (orderObj.contains("side"))
    {
        info.side = orderObj.at("side").as_string().c_str();
    }

    if (orderObj.contains("orderType"))
    {
        info.type = orderObj.at("orderType").as_string().c_str();
    }

    if (orderObj.contains("timeInForce"))
    {
        info.timeInForce = orderObj.at("timeInForce").as_string().c_str();
    }

    if (orderObj.contains("orderStatus"))
    {
        info.status = orderObj.at("orderStatus").as_string().c_str();
    }

    info.price = parseAmount(orderObj, "price");

    info.origQty = parseAmount(orderObj, "qty");

    info.executedQty = parseAmount(orderObj, "cumExecQty");

    info.cumQuoteQty = parseAmount(orderObj, "cumExecValue");

    info.leavesQty = parseAmount(orderObj, "leavesQty");

    info.avgPrice = parseAmount(orderObj, "avgPrice");

    if (orderObj.contains("createdTime"))
    {
        const auto &ct = orderObj.at("createdTime");
        if (ct.is_string())
        {
            info.createdTimeMs = strtoll(ct.as_string().c_str(), nullptr, 10);
        }
        else if (ct.is_number())
        {
            info.createdTimeMs = ct.as_int64();
        }
    }

    if (orderObj.contains("updatedTime"))
    {
        const auto &ut = orderObj.at("updatedTime");
        if (ut.is_string())
        {
            info.updatedTimeMs = strtoll(ut.as_string().c_str(), nullptr, 10);
        }
        else if (ut.is_number())
        {
            info.updatedTimeMs = ut.as_int64();
        }
    }
    if (info.updatedTimeMs == 0)
    {
        info.updatedTimeMs = timestamp;
    }

    return info;
}

SymbolInfo BybitDealService::createSymbolInfo(const json::object &instrument, const string &symbol)
{
    SymbolInfo info;
    info.symbol = symbol;
    if (instrument.contains("symbol"))
    {
        info.symbol = instrument.at("symbol").as_string().c_str();
    }

    if (instrument.contains("status"))
    {
        info.status = instrument.at("status").as_string().c_str();
    }

    if (instrument.contains("baseCoin"))
    {
        info.baseAsset = instrument.at("baseCoin").as_string().c_str();
    }
    if (instrument.contains("quoteCoin"))
    {
        info.quoteAsset = instrument.at("quoteCoin").as_string().c_str();
    }

    if (instrument.contains("priceFilter") && instrument.at("priceFilter").is_object())
    {
        const json::object &priceFilter = instrument.at("priceFilter").as_object();
        info.tickSize = parseAmount(priceFilter, "tickSize");
        info.minPrice = parseAmount(priceFilter, "minPrice");
        info.maxPrice = parseAmount(priceFilter, "maxPrice");
    }

    if (instrument.contains("lotSizeFilter") && instrument.at("lotSizeFilter").is_object())
    {
        const json::object &lotSizeFilter = instrument.at("lotSizeFilter").as_object();
        info.stepSize = parseAmount(lotSizeFilter, "qtyStep");

        if (info.stepSize <= 0.0)
        {
            if (lotSizeFilter.contains("basePrecision"))
            {
                info.stepSize = parseAmount(lotSizeFilter, "basePrecision");
            }
        }

        info.minQty = parseAmount(lotSizeFilter, "minOrderQty");
        info.maxQty = parseAmount(lotSizeFilter, "maxOrderQty");

        info.minNotional = parseAmount(lotSizeFilter, "minOrderAmt");
        info.maxNotional = parseAmount(lotSizeFilter, "maxOrderAmt");
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

namespace {
    bool isTerminalStatus(const string &status)
    {
        return status == "Filled" || status == "Cancelled" || status == "Rejected" || status == "Deactivated" ||
               status == "Triggered";
    }
}

OcoInfo BybitDealService::placeOco(const PlaceOcoRequest &request)
{
    if (request.symbol.empty())
    {
        throw runtime_error("Symbol cannot be empty");
    }
    if (request.quantity <= 0)
    {
        throw runtime_error("Quantity must be greater than 0");
    }
    if (request.price <= 0)
    {
        throw runtime_error("Price must be greater than 0");
    }
    if (request.stopPrice <= 0)
    {
        throw runtime_error("StopPrice must be greater than 0");
    }
    if (request.side != "BUY" && request.side != "SELL")
    {
        throw runtime_error("Invalid side: " + request.side);
    }
    if (request.stopLimitPrice.has_value() && *request.stopLimitPrice <= 0)
    {
        throw runtime_error("StopLimitPrice must be greater than 0 if set");
    }

    string groupId;
    if (request.listClientOrderId.has_value() && !request.listClientOrderId->empty())
    {
        groupId = *request.listClientOrderId;
    }
    else
    {
        msec now = getTimestamp();
        int randomSuffix = rand() % 10000;
        groupId = "OCO_" + to_string(now) + "_" + to_string(randomSuffix);
    }

    string takeProfitOrderLinkId = groupId + "_TP";
    string stopLossOrderLinkId = groupId + "_SL";
    if (takeProfitOrderLinkId.length() > 36)
    {
        takeProfitOrderLinkId = takeProfitOrderLinkId.substr(0, 36);
    }
    if (stopLossOrderLinkId.length() > 36)
    {
        stopLossOrderLinkId = stopLossOrderLinkId.substr(0, 36);
    }

    PlaceOrderRequest takeProfitRequest;
    takeProfitRequest.symbol = request.symbol;
    takeProfitRequest.side = request.side;
    takeProfitRequest.type = "LIMIT";
    takeProfitRequest.quantity = request.quantity;
    takeProfitRequest.price = request.price;
    takeProfitRequest.timeInForce = "GTC";
    takeProfitRequest.clientOrderId = takeProfitOrderLinkId;
    takeProfitRequest.category = "spot";

    OrderInfo takeProfitOrderInfo;
    try
    {
        takeProfitOrderInfo = placeOrder(takeProfitRequest);
    }
    catch (const exception &e)
    {
        throw runtime_error("Bybit placeOco: failed to place TP leg: " + string(e.what()));
    }

    PlaceOrderRequest stopLossRequest;
    stopLossRequest.symbol = request.symbol;
    stopLossRequest.side = request.side;
    stopLossRequest.category = "spot";
    stopLossRequest.clientOrderId = stopLossOrderLinkId;
    stopLossRequest.quantity = request.quantity;

    stopLossRequest.orderFilter = "StopOrder";
    stopLossRequest.triggerPrice = to_string(request.stopPrice);

    if (request.stopLimitPrice.has_value())
    {
        stopLossRequest.type = "LIMIT";
        stopLossRequest.price = *request.stopLimitPrice;
        stopLossRequest.timeInForce = request.stopLimitTimeInForce.has_value() ? *request.stopLimitTimeInForce : "GTC";
    }
    else
    {
        stopLossRequest.type = "MARKET";
        stopLossRequest.marketUnit = "baseCoin";
    }

    OrderInfo stopLossOrderInfo;
    try
    {
        stopLossOrderInfo = placeOrder(stopLossRequest);
    }
    catch (const exception &e)
    {
        string rollbackError;
        try
        {
            OrderQuery rollbackQuery;
            rollbackQuery.symbol = request.symbol;
            rollbackQuery.clientOrderId = takeProfitOrderLinkId;
            cancelOrder(rollbackQuery);
        }
        catch (const exception &rollbackEx)
        {
            rollbackError = "; Rollback failed: " + string(rollbackEx.what());
        }
        throw runtime_error("Bybit placeOco: failed to place SL leg: " + string(e.what()) + rollbackError);
    }

    {
        lock_guard<mutex> lock(ocoMutex);

        BybitOcoGroup group;
        group.groupId = groupId;

        group.takeProfit.symbol = request.symbol;
        group.takeProfit.orderId = takeProfitOrderInfo.orderId;
        group.takeProfit.clientOrderId = takeProfitOrderLinkId;
        group.takeProfit.category = "spot";

        group.stopLeg.symbol = request.symbol;
        group.stopLeg.orderId = stopLossOrderInfo.orderId;
        group.stopLeg.clientOrderId = stopLossOrderLinkId;
        group.stopLeg.category = "spot";

        group.tpOrderLinkId = takeProfitOrderLinkId;
        group.slOrderLinkId = stopLossOrderLinkId;

        ocoGroups[groupId] = group;
        ocoLegToGroup[takeProfitOrderLinkId] = groupId;
        ocoLegToGroup[stopLossOrderLinkId] = groupId;
    }

    OcoInfo ocoInfo;
    ocoInfo.orderListId = groupId;
    ocoInfo.listClientOrderId = groupId;
    ocoInfo.transactTimeMs = takeProfitOrderInfo.createdTimeMs;
    ocoInfo.orders.push_back(takeProfitOrderInfo);
    ocoInfo.orders.push_back(stopLossOrderInfo);

    return ocoInfo;
}

OcoInfo BybitDealService::cancelOco(const OrderListQuery &request)
{
    string groupId;
    if (request.orderListId.has_value())
    {
        groupId = *request.orderListId;
    }
    else if (request.listClientOrderId.has_value())
    {
        groupId = *request.listClientOrderId;
    }
    else
    {
        throw runtime_error("Bybit cancelOco: missing orderListId or listClientOrderId");
    }

    BybitOcoGroup ocoGroup;
    bool found = false;

    {
        lock_guard<mutex> lock(ocoMutex);
        auto it = ocoGroups.find(groupId);
        if (it != ocoGroups.end())
        {
            ocoGroup = it->second;
            found = true;
        }
    }

    if (!found)
    {
        throw runtime_error("Bybit cancelOco: unknown group params");
    }

    OrderInfo takeProfitCancelInfo, stopLossCancelInfo;
    bool tpTerminal = false;
    bool slTerminal = false;
    string errorMsg;

    try
    {
        takeProfitCancelInfo = cancelOrder(ocoGroup.takeProfit);
        tpTerminal = true;
    }
    catch (const exception &e)
    {
        try
        {
            OrderInfo check = getOrder(ocoGroup.takeProfit);
            takeProfitCancelInfo = check;
            if (isTerminalStatus(check.status))
            {
                tpTerminal = true;
            }
            else
            {
                errorMsg += "TP cancel failed and not terminal: " + string(e.what()) + "; ";
            }
        }
        catch (...)
        {
            errorMsg += "TP cancel failed and status check failed: " + string(e.what()) + "; ";
        }

        if (!tpTerminal)
        {
            takeProfitCancelInfo.status = "ERROR";
            takeProfitCancelInfo.symbol = ocoGroup.takeProfit.symbol;
            if (ocoGroup.takeProfit.orderId)
            {
                takeProfitCancelInfo.orderId = *ocoGroup.takeProfit.orderId;
            }
        }
    }

    try
    {
        stopLossCancelInfo = cancelOrder(ocoGroup.stopLeg);
        slTerminal = true;
    }
    catch (const exception &e)
    {
        try
        {
            OrderInfo check = getOrder(ocoGroup.stopLeg);
            stopLossCancelInfo = check;
            if (isTerminalStatus(check.status))
            {
                slTerminal = true;
            }
            else
            {
                errorMsg += "SL cancel failed and not terminal: " + string(e.what()) + "; ";
            }
        }
        catch (...)
        {
            errorMsg += "SL cancel failed and status check failed: " + string(e.what()) + "; ";
        }

        if (!slTerminal)
        {
            stopLossCancelInfo.status = "ERROR";
            stopLossCancelInfo.symbol = ocoGroup.stopLeg.symbol;
            if (ocoGroup.stopLeg.orderId)
            {
                stopLossCancelInfo.orderId = *ocoGroup.stopLeg.orderId;
            }
        }
    }

    if (tpTerminal && slTerminal)
    {
        lock_guard<mutex> lock(ocoMutex);
        auto it = ocoGroups.find(groupId);
        if (it != ocoGroups.end())
        {
            ocoLegToGroup.erase(ocoGroup.tpOrderLinkId);
            ocoLegToGroup.erase(ocoGroup.slOrderLinkId);
            ocoGroups.erase(it);
        }
    }
    else
    {
        throw runtime_error("Bybit cancelOco incomplete: " + errorMsg);
    }

    OcoInfo ocoCancelResult;
    ocoCancelResult.orderListId = groupId;
    ocoCancelResult.listClientOrderId = groupId;
    ocoCancelResult.orders.push_back(takeProfitCancelInfo);
    ocoCancelResult.orders.push_back(stopLossCancelInfo);

    return ocoCancelResult;
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

void BybitDealService::handleUserStreamMessage(const string &msg)
{
    try
    {
        boost::system::error_code errorCode;
        json::value jsonValue = json::parse(msg, errorCode);
        if (errorCode)
        {
            cerr << "Bybit stream: JSON parse error: " << errorCode.message() << endl;
            return;
        }
        if (!jsonValue.is_object())
        {
            return;
        }

        json::object &rootObject = jsonValue.as_object();

        auto *topicValue = rootObject.if_contains("topic");
        if (!topicValue || !topicValue->is_string())
        {
            return;
        }

        string topic = topicValue->as_string().c_str();

        if (topic == "wallet")
        {
            handleWalletUpdate(rootObject);
        }
        else if (topic == "order")
        {
            handleOrderUpdate(rootObject);
        }
    }
    catch (const exception &e)
    {
        cerr << "Bybit stream internal error: " << e.what() << endl;
    }
}

void BybitDealService::handleWalletUpdate(const json::object &root)
{
    auto *dataValue = root.if_contains("data");
    if (dataValue && dataValue->is_array())
    {
        const json::array &dataArray = dataValue->as_array();
        for (const json::value &itemValue : dataArray)
        {
            if (itemValue.is_object())
            {
                const json::object &itemObject = itemValue.as_object();
                auto *coinValue = itemObject.if_contains("coin");
                if (coinValue && coinValue->is_array())
                {
                    const json::array &coins = coinValue->as_array();
                    for (const json::value &coinItem : coins)
                    {
                        if (coinItem.is_object())
                        {
                            try
                            {
                                AssetBalance balance = parseBalance(coinItem.as_object());
                                updateBalanceCache(balance.asset, balance.free, balance.locked);
                            }
                            catch (const exception &e)
                            {
                                cerr << "Bybit stream wallet parsing error: " << e.what() << endl;
                            }
                        }
                    }
                }
            }
        }
    }
}

void BybitDealService::handleOrderUpdate(const json::object &root)
{
    auto *dataValue = root.if_contains("data");
    if (dataValue && dataValue->is_array())
    {
        const json::array &dataArray = dataValue->as_array();
        for (const json::value &itemValue : dataArray)
        {
            if (itemValue.is_object())
            {
                const json::object &order = itemValue.as_object();
                string status;
                if (order.contains("orderStatus"))
                {
                    status = order.at("orderStatus").as_string().c_str();
                }

                if (status == "Filled")
                {
                    string orderLinkId;
                    if (order.contains("orderLinkId"))
                    {
                        orderLinkId = order.at("orderLinkId").as_string().c_str();
                    }

                    if (!orderLinkId.empty())
                    {
                        processOcoUpdate(orderLinkId);
                    }
                }
            }
        }
    }
}

void BybitDealService::processOcoUpdate(const string &orderLinkId)
{
    string groupId;
    OrderQuery otherLegQuery;
    bool shouldCancel = false;

    {
        lock_guard<mutex> lock(ocoMutex);
        auto legIt = ocoLegToGroup.find(orderLinkId);
        if (legIt != ocoLegToGroup.end())
        {
            groupId = legIt->second;
            auto groupIt = ocoGroups.find(groupId);
            if (groupIt != ocoGroups.end())
            {
                BybitOcoGroup &group = groupIt->second;
                if (!group.closing)
                {
                    group.closing = true;
                    shouldCancel = true;
                    if (orderLinkId == group.tpOrderLinkId)
                    {
                        otherLegQuery = group.stopLeg;
                    }
                    else
                    {
                        otherLegQuery = group.takeProfit;
                    }
                }
            }
        }
    }

    if (shouldCancel)
    {
        bool cancellationSuccessfulOrTerminal = false;
        string cancelError;
        try
        {
            cancelOrder(otherLegQuery);
            cancellationSuccessfulOrTerminal = true;
        }
        catch (const exception &e)
        {
            cancelError = e.what();
            try
            {
                OrderInfo info = getOrder(otherLegQuery);
                if (isTerminalStatus(info.status))
                {
                    cancellationSuccessfulOrTerminal = true;
                }
            }
            catch (const exception &statusEx)
            {
                cancelError += "; Status check failed: " + string(statusEx.what());
            }
            catch (...)
            {
                cancelError += "; Status check failed: Unknown error";
            }
        }

        lock_guard<mutex> lock(ocoMutex);
        auto groupIt = ocoGroups.find(groupId);
        if (groupIt != ocoGroups.end())
        {
            if (cancellationSuccessfulOrTerminal)
            {
                ocoLegToGroup.erase(groupIt->second.tpOrderLinkId);
                ocoLegToGroup.erase(groupIt->second.slOrderLinkId);
                ocoGroups.erase(groupIt);
            }
            else
            {
                groupIt->second.closing = false;
                groupIt->second.lastError = cancelError;
            }
        }
    }
}

bool BybitDealService::cancelAllOpenOrders(const string &symbol, const string &category)
{
    const string effectiveCategory = category.empty() ? "spot" : category;

    json::object body;
    body["category"] = effectiveCategory;

    if (!symbol.empty())
    {
        body["symbol"] = symbol;
    }

    const string bodyStr = json::serialize(body);

    const msec timestamp = getTimestamp();
    const string signature = getSignature(bodyStr, timestamp);
    const auto headers = createHeaders(apiKey, signature, timestamp);

    const string target = "/v5/order/cancel-all";
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);
    context.setRequestBody(bodyStr);

    const string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    if (errorCode)
    {
        throw runtime_error("Bybit cancelAllOpenOrders: JSON parse error: " + errorCode.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Bybit cancelAllOpenOrders: Response is not a JSON object");
    }

    json::object &jsonObject = jsonValue.as_object();

    int retCode = -1;
    if (jsonObject.contains("retCode") && jsonObject.at("retCode").is_number())
    {
        retCode = static_cast<int>(jsonObject.at("retCode").as_int64());
    }

    if (retCode != 0)
    {
        string message = "Unknown Error";
        if (jsonObject.contains("retMsg") && jsonObject.at("retMsg").is_string())
        {
            message = jsonObject.at("retMsg").as_string().c_str();
        }
        throw runtime_error("Bybit Error " + to_string(retCode) + ": " + message);
    }

    return true;
}

flat_map<string, AssetBalance> BybitDealService::getBalancesRest()
{
    ensureBalancesSeeded(nullopt);
    return getBalances();
}

void BybitDealService::waitUntilOrderFilled(const std::string &symbol, const std::string &orderId)
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
             orderQuery.category = "spot"; // default
             
             OrderInfo orderInfo = getOrder(orderQuery);
             if (orderInfo.status == "Filled" || orderInfo.status == "Cancelled" || orderInfo.status == "Rejected" || orderInfo.status == "Deactivated" || orderInfo.status == "Triggered")
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