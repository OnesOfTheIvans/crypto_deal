#include "BinanceDealService.hpp"
#include "EnumStringConverter.hpp"
#include "common/HttpRequestContext.hpp"
#include "common/exception_handling.hpp"
#include "common/http_request.hpp"
#include "domain/AccountDto.hpp"
#include "domain/ErrorDto.hpp"
#include "domain/ExchangeInfoDto.hpp"
#include "domain/ServerTimeDto.hpp"
#include "domain/SubscriptionResponseDto.hpp"
#include "domain/TickerPriceDto.hpp"
#include "domain/UserStreamMessageDto.hpp"
#include "domain/UserStreamSubscribeRequestDto.hpp"

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
    string getErrorMessage(const ErrorDto &error)
    {
        const long long code = error.code.value_or(0);
        const string msg = error.msg.value_or("");

        return "Binance Error " + to_string(code) + ": " + msg;
    }

    bool hasErrorResponse(const json::value &value)
    {
        if (!value.is_object())
        {
            return false;
        }

        const json::object &object = value.as_object();
        return object.contains("code") && object.contains("msg");
    }

    Decimal parseToDecimal(const optional<string> &value)
    {
        return value.has_value() && !value->empty() ? DecimalConverter::parseDecimal(value.value()) : Decimal{};
    }

    int parseToInt(const optional<int64_t> &value)
    {
        return value.has_value() ? static_cast<int>(value.value()) : 0;
    }

    string toString(boost::urls::pct_string_view value)
    {
        return {value.begin(), value.end()};
    }

    string getTarget(const boost::urls::url &url)
    {
        return toString(url.encoded_target());
    }

    string getQuery(const boost::urls::url &url)
    {
        return toString(url.encoded_query());
    }
} // namespace

void BinanceDealService::setRequestParameters(boost::urls::url &url,
                                              const PlaceOrderRequest &request,
                                              const SymbolInfo &info)
{
    map<string, string> parameterMap;
    parameterMap["newOrderRespType"] = "RESULT";
    parameterMap["quantity"] = DecimalConverter::formatByStep(request.quantity, info.stepSize);
    parameterMap["recvWindow"] = to_string(recvWindow);
    parameterMap["side"] = EnumStringConverter<OrderOperation>::toString(request.side);
    parameterMap["symbol"] = request.symbol;
    parameterMap["timestamp"] = to_string(getServerTimestamp());
    parameterMap["type"] = EnumStringConverter<OrderType>::toString(request.type);

    if (request.type == OrderType::LIMIT)
    {
        parameterMap["price"] = DecimalConverter::formatByStep(request.price.value(), info.tickSize);
        parameterMap["timeInForce"] = request.timeInForce.value();
    }

    if (request.clientOrderId.has_value() && !request.clientOrderId->empty())
    {
        setParameterIfPresent(parameterMap, "newClientOrderId", request.clientOrderId);
    }

    setUrlParameters(url, parameterMap);
}

void BinanceDealService::setRequestParameters(boost::urls::url &url, const OrderQuery &request)
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(!request.orderId.has_value() && !request.clientOrderId.has_value(),
            "Either orderId or clientOrderId must be provided");

    map<string, string> parameterMap;
    parameterMap["recvWindow"] = to_string(recvWindow);
    parameterMap["symbol"] = request.symbol;
    parameterMap["timestamp"] = to_string(getServerTimestamp());
    setParameterIfPresent(parameterMap, "orderId", request.orderId);
    setParameterIfPresent(parameterMap, "origClientOrderId", request.clientOrderId);

    setUrlParameters(url, parameterMap);
}

void BinanceDealService::setRequestParameters(boost::urls::url &url, const PlaceOcoRequest &request)
{
    throwIf(request.symbol.empty(), "Binance placeOco: symbol cannot be empty");
    throwIf(request.quantity <= 0, "Binance placeOco: quantity must be > 0");
    throwIf(request.price <= 0, "Binance placeOco: price must be > 0");
    throwIf(request.stopPrice <= 0, "Binance placeOco: stopPrice must be > 0");

    const SymbolInfo info = getSymbolInfo(request.symbol);
    const Decimal belowPrice = request.stopLimitPrice.has_value() ? request.stopLimitPrice.value() : request.stopPrice;
    const string belowTif =
        request.stopLimitTimeInForce.has_value() ? request.stopLimitTimeInForce.value() : string("GTC");
    optional<string> aboveQuantityError = validateQuantity(request.quantity, request.price, info);
    throwIf(aboveQuantityError.has_value(), "Binance placeOco above leg: " + aboveQuantityError.value_or(""));
    optional<string> belowQuantityError = validateQuantity(request.quantity, belowPrice, info);
    throwIf(belowQuantityError.has_value(), "Binance placeOco below leg: " + belowQuantityError.value_or(""));

    map<string, string> parameterMap;
    parameterMap["abovePrice"] = DecimalConverter::formatByStep(request.price, info.tickSize);
    parameterMap["aboveType"] = "LIMIT_MAKER";
    parameterMap["belowPrice"] = DecimalConverter::formatByStep(belowPrice, info.tickSize);
    parameterMap["belowStopPrice"] = DecimalConverter::formatByStep(request.stopPrice, info.tickSize);
    parameterMap["belowTimeInForce"] = belowTif;
    parameterMap["belowType"] = "STOP_LOSS_LIMIT";
    parameterMap["quantity"] = DecimalConverter::formatByStep(request.quantity, info.stepSize);
    parameterMap["recvWindow"] = to_string(recvWindow);
    parameterMap["side"] = EnumStringConverter<OrderOperation>::toString(request.side);
    parameterMap["symbol"] = request.symbol;
    parameterMap["timestamp"] = to_string(getServerTimestamp());
    setParameterIfPresent(parameterMap, "listClientOrderId", request.listClientOrderId);
    setParameterIfPresent(parameterMap, "aboveClientOrderId", request.limitClientOrderId);
    setParameterIfPresent(parameterMap, "belowClientOrderId", request.stopClientOrderId);

    setUrlParameters(url, parameterMap);
}

void BinanceDealService::setRequestParameters(boost::urls::url &url, const OrderListQuery &request)
{
    map<string, string> parameterMap;
    parameterMap["recvWindow"] = to_string(recvWindow);
    parameterMap["symbol"] = request.symbol;
    parameterMap["timestamp"] = to_string(getServerTimestamp());
    setParameterIfPresent(parameterMap, "orderListId", request.orderListId);
    setParameterIfPresent(parameterMap, "listClientOrderId", request.listClientOrderId);

    setUrlParameters(url, parameterMap);
}

void BinanceDealService::setRequestParameters(boost::urls::url &url, const string &symbol, bool isPrivate)
{
    map<string, string> parameterMap;
    parameterMap["symbol"] = symbol;
    if (isPrivate)
    {
        parameterMap["recvWindow"] = to_string(recvWindow);
        parameterMap["timestamp"] = to_string(getServerTimestamp());
    }
    setUrlParameters(url, parameterMap);
}

void BinanceDealService::setRequestParameters(boost::urls::url &url)
{
    map<string, string> parameterMap;
    parameterMap["recvWindow"] = to_string(recvWindow);
    parameterMap["timestamp"] = to_string(getServerTimestamp());
    setUrlParameters(url, parameterMap);
}

void BinanceDealService::signUrl(boost::urls::url &url)
{
    const string queryString = getQuery(url);
    const string signature = hmac_sha256(secretKey, queryString);
    url.params().append({"signature", signature});
}

flat_map<string, string> BinanceDealService::createHeaders(const string &apiKey)
{
    return {{"X-MBX-APIKEY", apiKey}};
}

json::value BinanceDealService::parseAndValidate(const string &response)
{
    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Response is not a JSON object");

    throwIf(hasErrorResponse(jsonValue), getErrorMessage(json::value_to<ErrorDto>(jsonValue)));
    return jsonValue;
}

void BinanceDealService::checkCancelAllOpenOrdersResult(const string &response)
{
    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "Binance cancelAllOpenOrders: JSON parse error: " + errorCode.message());

    if (jsonValue.is_object())
    {
        if (hasErrorResponse(jsonValue))
        {
            const ErrorDto error = json::value_to<ErrorDto>(jsonValue);
            const long long code = error.code.value_or(0);

            throwIf(code != -2011 && code != -2013, getErrorMessage(error));
        }

        return;
    }

    throwIf(!jsonValue.is_array(), "Binance cancelAllOpenOrders: Unexpected response type");
}

string BinanceDealService::buildUserStreamSubscribeRequestJson()
{

    auto timestamp = getServerTimestamp();
    string payload = "apiKey=" + apiKey + "&timestamp=" + to_string(timestamp);
    string signature = hmac_sha256(secretKey, payload);

    const UserStreamSubscribeRequestDto request{"user_stream_subscribe_1",
                                                "userDataStream.subscribe.signature",
                                                {apiKey, timestamp, move(signature)}};

    return json::serialize(json::value_from(request));
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

void BinanceDealService::updateCache(const vector<StreamBalanceDto> &balanceDtos)
{
    for (const StreamBalanceDto &balance : balanceDtos)
    {
        const Decimal free = parseToDecimal(balance.f);
        const Decimal locked = parseToDecimal(balance.l);

        updateBalanceCache(balance.a, free, locked);
    }
}

void BinanceDealService::updateCache(const vector<AccountBalanceDto> &balanceDtos)
{
    for (const AccountBalanceDto &balance : balanceDtos)
    {
        const Decimal free = parseToDecimal(balance.free);
        const Decimal locked = parseToDecimal(balance.locked);

        updateBalanceCache(balance.asset, free, locked);
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

    const UserStreamMessageDto message = json::value_to<UserStreamMessageDto>(jsonValue);
    if (!message.event.has_value())
    {
        return;
    }

    const OutboundAccountPositionEventDto &event = message.event.value();
    throwIf(!event.e.has_value(), "User stream message missing or invalid 'e' (event type) field");

    if (event.e.value() != "outboundAccountPosition" || !event.B.has_value())
    {
        return;
    }

    updateCache(event.B.value());
}

StreamStatus BinanceDealService::getUserStreamStatus() const
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
    boost::urls::url requestUrl;
    requestUrl.set_path("/api/v3/time");
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);
    string response = httpsPost(context);

    beast::error_code ec;
    json::value val = json::parse(response, ec);
    if (!ec && val.is_object())
    {
        const ServerTimeDto serverTimeDto = json::value_to<ServerTimeDto>(val);
        return serverTimeDto.serverTime;
    }

    throw runtime_error("Failed to gain Binance server time");
}

bool BinanceDealService::isTimeSyncRecent() const
{
    long long currentMonoMs = chrono::steady_clock::now().time_since_epoch().count() / 1000000;
    long long lastMonoMs = lastSyncMonoMs.load();
    return currentMonoMs - lastMonoMs < 10000;
}

void BinanceDealService::syncTime()
{
    if (isTimeSyncRecent())
    {
        return;
    }

    lock_guard<mutex> lock(timeSyncMutex);

    if (isTimeSyncRecent())
    {
        return;
    }

    long long serverTime = getServerTime();
    long long localTime =
        chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    serverTimeOffset = serverTime - localTime;
    long long currentMonoMs = chrono::steady_clock::now().time_since_epoch().count() / 1000000;
    lastSyncMonoMs.store(currentMonoMs);
    cout << "Binance time synced. Offset: " << serverTimeOffset << "ms" << endl;
}

long long BinanceDealService::getServerTimestamp()
{
    syncTime();
    auto now = chrono::system_clock::now();
    long long localTime = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch()).count();
    return localTime + serverTimeOffset;
}

void BinanceDealService::handleUserStreamSubscriptionResponse(WebsocketStream &websocketStream)
{
    beast::flat_buffer buffer;
    websocketStream.read(buffer);
    string msg = beast::buffers_to_string(buffer.data());

    beast::error_code ec;
    json::value val = json::parse(msg, ec);
    if (!ec && val.is_object())
    {
        const SubscriptionResponseDto response = json::value_to<SubscriptionResponseDto>(val);
        if (response.status.has_value() && response.status.value() == 200)
        {
            cout << "Binance stream subscribed successfully." << endl;
            return;
        }
    }

    cerr << "Binance stream subscription failed or invalid response: " << msg << endl;
}

shared_ptr<WebsocketStream> BinanceDealService::prepareUserWebsocketStream()
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

    handleUserStreamSubscriptionResponse(*sharedWebsocketStream);

    return sharedWebsocketStream;
}

void BinanceDealService::prepareUserStreamThread()
{
    runner = thread(
        [this]()
        {
            try
            {
                auto sharedWebsocketStream = prepareUserWebsocketStream();

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

    prepareUserStreamThread();
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
    boost::urls::url requestUrl;
    requestUrl.set_path("/api/v3/ticker/price");
    setRequestParameters(requestUrl, symbol, false);
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    string response = httpsPost(context);

    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    if (!errorCode && jsonValue.is_object())
    {
        const TickerPriceDto ticker = json::value_to<TickerPriceDto>(jsonValue);
        return DecimalConverter::parseDecimal(ticker.price);
    }
    return Decimal{};
}

bool BinanceDealService::isQuantityStepValid(Decimal quantity, Decimal stepSize) const
{
    if (stepSize <= 0)
    {
        return true;
    }

    const Decimal units = quantity / stepSize;
    return units == boost::decimal::floor(units);
}

optional<string> BinanceDealService::validateQuantity(Decimal quantity, Decimal price, const SymbolInfo &info) const
{
    if (!isQuantityStepValid(quantity, info.stepSize))
    {
        return "quantity is not valid for step size";
    }
    if (info.minQty > 0 && quantity < info.minQty)
    {
        return "quantity " + DecimalConverter::formatDecimal(quantity) + " is below minQty " +
               DecimalConverter::formatDecimal(info.minQty);
    }
    if (info.maxQty > 0 && quantity > info.maxQty)
    {
        return "quantity " + DecimalConverter::formatDecimal(quantity) + " is above maxQty " +
               DecimalConverter::formatDecimal(info.maxQty);
    }

    const bool hasNotionalRule = info.minNotional > 0 || info.maxNotional > 0;
    if (hasNotionalRule && price <= 0)
    {
        return "price is required for notional validation";
    }

    if (price > 0)
    {
        const Decimal notional = quantity * price;
        if (info.minNotional > 0 && notional < info.minNotional)
        {
            return "notional " + DecimalConverter::formatDecimal(notional) + " is below minNotional " +
                   DecimalConverter::formatDecimal(info.minNotional);
        }
        if (info.maxNotional > 0 && notional > info.maxNotional)
        {
            return "notional " + DecimalConverter::formatDecimal(notional) + " is above maxNotional " +
                   DecimalConverter::formatDecimal(info.maxNotional);
        }
    }

    return nullopt;
}

OrderInfo BinanceDealService::buyCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity)
{
    PlaceOrderRequest request;
    request.symbol = baseAsset + quoteAsset;
    request.side = OrderOperation::BUY;
    request.type = OrderType::MARKET;
    request.quantity = quantity;

    return placeOrder(request);
}

OrderInfo BinanceDealService::sellCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity)
{
    PlaceOrderRequest request;
    request.symbol = baseAsset + quoteAsset;
    request.side = OrderOperation::SELL;
    request.type = OrderType::MARKET;
    request.quantity = quantity;

    return placeOrder(request);
}

void BinanceDealService::validatePlaceOrderRequest(const PlaceOrderRequest &request) const
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(request.quantity <= 0, "Quantity must be greater than 0");
    if (request.type == OrderType::LIMIT)
    {
        throwIf(!request.price.has_value() || request.price.value() <= 0, "Price must be > 0 for LIMIT orders");
        throwIf(!request.timeInForce.has_value() || request.timeInForce->empty(),
                "TimeInForce required for LIMIT orders");
    }
}

void BinanceDealService::validatePlaceOcoRequest(const PlaceOcoRequest &request) const
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(request.quantity <= 0, "Quantity must be > 0");
    throwIf(request.price <= 0, "Price must be > 0");
    throwIf(request.stopPrice <= 0, "Stop Price must be > 0");

    if (request.stopLimitPrice.has_value() && request.stopLimitPrice.value() > 0)
    {
        throwIf(!request.stopLimitTimeInForce.has_value() || request.stopLimitTimeInForce->empty(),
                "stopLimitTimeInForce required if stopLimitPrice is set");
    }
}

void BinanceDealService::checkBalance(const PlaceOrderRequest &request, const SymbolInfo &info, Decimal validationPrice)
{
    if (request.side == OrderOperation::BUY)
    {
        const auto quoteBalance = getBalance(info.quoteAsset);
        const Decimal quoteFree = quoteBalance.has_value() ? quoteBalance.value().free : Decimal{0};
        const Decimal requiredQuoteAmount = request.quantity * validationPrice;

        throwIf(requiredQuoteAmount > 0 && quoteFree < requiredQuoteAmount,
                "Insufficient balance: need " + DecimalConverter::formatDecimal(requiredQuoteAmount) + " " +
                    info.quoteAsset + ", have " + DecimalConverter::formatDecimal(quoteFree));
        throwIf(quoteFree <= 0, "Insufficient balance: no free " + info.quoteAsset);
    }
    else
    {
        const auto baseBalance = getBalance(info.baseAsset);
        const Decimal baseFree = baseBalance.has_value() ? baseBalance.value().free : Decimal{0};

        throwIf(baseFree < request.quantity,
                "Insufficient balance: need " + DecimalConverter::formatDecimal(request.quantity) + " " +
                    info.baseAsset + ", have " + DecimalConverter::formatDecimal(baseFree));
    }
}

void BinanceDealService::checkBalance(const PlaceOcoRequest &request, const SymbolInfo &info)
{
    if (request.side == OrderOperation::BUY)
    {
        const auto quoteBalance = getBalance(info.quoteAsset);
        const Decimal quoteFree = quoteBalance.has_value() ? quoteBalance.value().free : Decimal{0};
        const Decimal belowPrice =
            request.stopLimitPrice.has_value() ? request.stopLimitPrice.value() : request.stopPrice;
        const Decimal requiredPrice = request.price > belowPrice ? request.price : belowPrice;
        const Decimal requiredQuoteAmount = request.quantity * requiredPrice;

        throwIf(requiredQuoteAmount > 0 && quoteFree < requiredQuoteAmount,
                "Insufficient balance: need " + DecimalConverter::formatDecimal(requiredQuoteAmount) + " " +
                    info.quoteAsset + ", have " + DecimalConverter::formatDecimal(quoteFree));
        throwIf(quoteFree <= 0, "Insufficient balance: no free " + info.quoteAsset);
    }
    else
    {
        const auto baseBalance = getBalance(info.baseAsset);
        const Decimal baseFree = baseBalance.has_value() ? baseBalance.value().free : Decimal{0};

        throwIf(baseFree < request.quantity,
                "Insufficient balance: need " + DecimalConverter::formatDecimal(request.quantity) + " " +
                    info.baseAsset + ", have " + DecimalConverter::formatDecimal(baseFree));
    }
}

OrderInfo BinanceDealService::placeOrder(const PlaceOrderRequest &request)
{
    validatePlaceOrderRequest(request);

    SymbolInfo info = getSymbolInfo(request.symbol);
    const Decimal validationPrice = request.type == OrderType::LIMIT && request.price.has_value()
                                        ? request.price.value()
                                        : getTickerPrice(request.symbol);
    optional<string> quantityError = validateQuantity(request.quantity, validationPrice, info);
    throwIf(quantityError.has_value(), "Binance placeOrder: " + quantityError.value_or(""));
    checkBalance(request, info, validationPrice);

    boost::urls::url requestUrl;
    requestUrl.set_path("/api/v3/order");
    setRequestParameters(requestUrl, request, info);
    signUrl(requestUrl);
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(createHeaders(apiKey));

    string response = httpsPost(context);

    json::value jsonValue = parseAndValidate(response);

    return createOrderInfo(json::value_to<OrderDto>(jsonValue));
}

OrderInfo BinanceDealService::cancelOrder(const OrderQuery &request)
{
    boost::urls::url requestUrl;
    requestUrl.set_path("/api/v3/order");
    setRequestParameters(requestUrl, request);
    signUrl(requestUrl);
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::delete_);
    context.setRequestHeaders(createHeaders(apiKey));

    string response = httpsPost(context);

    json::value jsonValue = parseAndValidate(response);

    return createOrderInfo(json::value_to<OrderDto>(jsonValue));
}

OrderInfo BinanceDealService::getOrder(const OrderQuery &request)
{
    boost::urls::url requestUrl;
    requestUrl.set_path("/api/v3/order");
    setRequestParameters(requestUrl, request);
    signUrl(requestUrl);
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);
    context.setRequestHeaders(createHeaders(apiKey));

    string response = httpsPost(context);

    json::value jsonValue = parseAndValidate(response);

    return createOrderInfo(json::value_to<OrderDto>(jsonValue));
}

SymbolInfo BinanceDealService::getSymbolInfo(const string &symbol, OrderCategory)
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

    boost::urls::url requestUrl;
    requestUrl.set_path("/api/v3/exchangeInfo");
    setRequestParameters(requestUrl, symbol, false);
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    string response = httpsPost(context);

    json::value jsonValue = parseAndValidate(response);

    ExchangeInfoDto exchangeInfo = json::value_to<ExchangeInfoDto>(jsonValue);

    throwIf(exchangeInfo.symbols.empty(), "Binance symbol not found: " + symbol);

    SymbolInfo info = createSymbolInfo(exchangeInfo.symbols[0]);
    {
        lock_guard<mutex> lock(symbolInfoMutex);
        symbolInfoCache[symbol] = info;
    }
    return info;
}

Decimal BinanceDealService::ceilQuantityToStep(const string &symbol, Decimal quantity, OrderCategory)
{
    const SymbolInfo info = getSymbolInfo(symbol);
    return DecimalConverter::ceilToStep(quantity, info.stepSize);
}

OcoInfo BinanceDealService::placeOco(const PlaceOcoRequest &request)
{
    validatePlaceOcoRequest(request);

    const SymbolInfo info = getSymbolInfo(request.symbol);
    checkBalance(request, info);

    boost::urls::url requestUrl;
    requestUrl.set_path("/api/v3/orderList/oco");
    setRequestParameters(requestUrl, request);
    signUrl(requestUrl);

    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(createHeaders(apiKey));

    string response = httpsPost(context);

    json::value jsonValue = parseAndValidate(response);

    return createOcoInfo(json::value_to<OcoDto>(jsonValue));
}

OcoInfo BinanceDealService::cancelOco(const OrderListQuery &request)
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(!request.orderListId.has_value() && !request.listClientOrderId.has_value(),
            "Either orderListId or listClientOrderId must be provided");

    boost::urls::url requestUrl;
    requestUrl.set_path("/api/v3/orderList");
    setRequestParameters(requestUrl, request);
    signUrl(requestUrl);

    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::delete_);
    context.setRequestHeaders(createHeaders(apiKey));

    string response = httpsPost(context);

    json::value jsonValue = parseAndValidate(response);

    return createOcoInfo(json::value_to<OcoDto>(jsonValue));
}

OcoInfo BinanceDealService::createOcoInfo(const OcoDto &oco)
{
    OcoInfo info;

    throwIf(!oco.orderListId.has_value(), "Missing required string field: orderListId");
    info.orderListId = to_string(oco.orderListId.value());
    info.listClientOrderId = oco.listClientOrderId.value_or("");
    info.transactTimeMs = oco.transactionTime.value_or(0);

    for (const OrderDto &report : oco.orderReports)
    {
        info.orders.push_back(createOrderInfo(report));
    }

    return info;
}

SymbolInfo BinanceDealService::createSymbolInfo(const SymbolDto &symbol)
{
    SymbolInfo info;
    info.symbol = symbol.symbol;
    info.status = symbol.status;
    info.baseAsset = symbol.baseAsset;
    info.quoteAsset = symbol.quoteAsset;
    info.qtyPrecision = parseToInt(symbol.baseAssetPrecision);
    info.pricePrecision = parseToInt(symbol.quotePrecision);

    for (const FilterDto &filter : symbol.filters)
    {
        processSymbolFilters(info, filter);
    }

    throwIf(info.tickSize <= 0, "Invalid tickSize");
    throwIf(info.stepSize <= 0, "Invalid stepSize");
    throwIf(info.minQty <= 0, "Invalid minQty");

    return info;
}

void BinanceDealService::processSymbolFilters(SymbolInfo &info, const FilterDto &filter)
{
    if (!filter.filterType.has_value())
    {
        return;
    }

    const optional<FilterType> type = EnumStringConverter<FilterType>::parseString(filter.filterType.value());

    if (!type.has_value())
    {
        return;
    }

    switch (type.value())
    {
    case FilterType::PRICE_FILTER:
        info.minPrice = parseToDecimal(filter.minPrice);
        info.maxPrice = parseToDecimal(filter.maxPrice);
        info.tickSize = parseToDecimal(filter.tickSize);
        break;
    case FilterType::LOT_SIZE:
        info.minQty = parseToDecimal(filter.minQty);
        info.maxQty = parseToDecimal(filter.maxQty);
        info.stepSize = parseToDecimal(filter.stepSize);
        break;
    case FilterType::MIN_NOTIONAL:
        info.minNotional = parseToDecimal(filter.minNotional);
        break;
    case FilterType::NOTIONAL:
        info.minNotional = parseToDecimal(filter.minNotional);
        info.maxNotional = parseToDecimal(filter.maxNotional);
        break;
    }
}

OrderInfo BinanceDealService::createOrderInfo(const OrderDto &order)
{
    OrderInfo info;
    throwIf(!order.symbol.has_value(), "Missing required string field: symbol");
    throwIf(!order.orderId.has_value(), "Missing required string field: orderId");
    throwIf(!order.side.has_value(), "Missing required string field: side");
    throwIf(!order.type.has_value(), "Missing required string field: type");
    throwIf(!order.status.has_value(), "Missing required string field: status");

    info.symbol = order.symbol.value();
    info.orderId = to_string(order.orderId.value());
    info.clientOrderId = order.clientOrderId.value_or("");
    info.side = order.side.value();
    info.type = order.type.value();
    info.status = order.status.value();
    info.timeInForce = order.timeInForce.value_or("");

    info.price = parseToDecimal(order.price);
    info.origQty = parseToDecimal(order.origQty);
    info.executedQty = parseToDecimal(order.executedQty);

    if (order.cummulativeQuoteQty.has_value())
    {
        info.cumQuoteQty = parseToDecimal(order.cummulativeQuoteQty);
    }
    else if (order.cumulativeQuoteQty.has_value())
    {
        info.cumQuoteQty = parseToDecimal(order.cumulativeQuoteQty);
    }

    info.createdTimeMs = order.transactTime.value_or(0);

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

void BinanceDealService::cancelAllOpenOrders(const string &symbol, OrderCategory)
{
    throwIf(symbol.empty(), "Binance cancelAllOpenOrders: symbol cannot be empty");

    boost::urls::url requestUrl;
    requestUrl.set_path("/api/v3/openOrders");
    setRequestParameters(requestUrl, symbol, true);
    signUrl(requestUrl);
    const string target = getTarget(requestUrl);

    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::delete_);
    context.setRequestHeaders(createHeaders(apiKey));

    const string response = httpsPost(context);

    checkCancelAllOpenOrdersResult(response);
}

flat_map<string, AssetBalance> BinanceDealService::getBalancesRest()
{
    boost::urls::url requestUrl;
    requestUrl.set_path("/api/v3/account");
    setRequestParameters(requestUrl);
    signUrl(requestUrl);
    const string target = getTarget(requestUrl);

    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);
    context.setRequestHeaders(createHeaders(apiKey));

    const string response = httpsPost(context);

    json::value jsonValue = parseAndValidate(response);

    const AccountDto account = json::value_to<AccountDto>(jsonValue);

    updateCache(account.balances);

    return getBalances();
}

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
