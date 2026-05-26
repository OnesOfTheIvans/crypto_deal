#include "BybitDealService.hpp"
#include "EnumStringConverter.hpp"
#include "common/exception_handling.hpp"
#include "common/http_request.hpp"
#include "domain/AuthResponseDto.hpp"
#include "domain/CancelAllOpenOrdersRequestDto.hpp"
#include "domain/CancelOrderRequestDto.hpp"
#include "domain/CreateOrderRequestDto.hpp"
#include "domain/InstrumentInfoResponseDto.hpp"
#include "domain/OrderResponseDto.hpp"
#include "domain/RealtimeOrderResponseDto.hpp"
#include "domain/ResponseDto.hpp"
#include "domain/ServerTimeResponseDto.hpp"
#include "domain/TickerResponseDto.hpp"
#include "domain/WalletBalanceResponseDto.hpp"
#include <cstdlib>

#include <algorithm>
#include <sstream>
// DEBUG
#include <iostream>

#include "common/DecimalConverter.hpp"
#include "common/type_aliasing.hpp"

using namespace std;
using namespace bybit;
using namespace exception_handling;

namespace {
    template <typename ResponseDtoType> string getErrorMessage(const ResponseDtoType &response)
    {
        return "Bybit Error " + to_string(response.retCode) + ": " + response.retMsg.value_or("Unknown Error");
    }

    template <typename ResponseDtoType> ResponseDtoType parseResponseToDto(const json::value &value)
    {
        const ResponseDtoType response = json::value_to<ResponseDtoType>(value);
        throwIf(response.retCode != 0, getErrorMessage(response));

        return response;
    }

    Decimal parseToDecimal(const optional<string> &value)
    {
        return value.has_value() && !value->empty() ? DecimalConverter::parseDecimal(value.value()) : Decimal{};
    }

    long long parseTimestamp(const optional<string> &value)
    {
        return value.has_value() && !value->empty() ? stoll(value.value()) : 0;
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

StreamStatus BybitDealService::getUserStreamStatus() const
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
    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/market/time");
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);
    string response = httpsPost(context);

    beast::error_code ec;
    json::value val = json::parse(response, ec);
    if (!ec && val.is_object())
    {
        const ServerTimeResponseDto responseDto = parseResponseToDto<ServerTimeResponseDto>(val);
        throwIf(!responseDto.result.has_value(), "Missing result object");
        long long serverTime = stoll(responseDto.result.value().timeSecond) * 1000;
        long long localTime =
            chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
        serverTimeOffset = serverTime - localTime;
        timeSynced = true;
        cout << "Bybit time synced. Offset: " << serverTimeOffset << "ms" << endl;
    }
}

long long BybitDealService::getServerTimestamp()
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
    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/account/wallet-balance");
    setRequestParameters(requestUrl, accountType, coinFilter);
    const string queryString = getQuery(requestUrl);

    const msec timestamp = getServerTimestamp();
    const string signature = getSignature(queryString, timestamp);
    const auto headers = createHeaders(apiKey, signature, timestamp);

    const string target = getTarget(requestUrl);
    HttpRequestContext requestContext(ioc, ctx, host, target);
    requestContext.prepareRequest(http::verb::get);
    requestContext.setRequestHeaders(headers);

    const string response = httpsPost(requestContext);

    optional<string> errorMessage = isResponseStatusOk(response);
    throwIf(errorMessage.has_value(), "Bybit wallet-balance failed: " + errorMessage.value_or(""));

    beast::error_code jsonError;
    json::value parsedValue = json::parse(response, jsonError);
    throwIf(jsonError || !parsedValue.is_object(), "Bybit wallet-balance JSON parse error");

    const WalletBalanceResponseDto responseDto = parseResponseToDto<WalletBalanceResponseDto>(parsedValue);
    if (!responseDto.result.has_value() || !responseDto.result.value().list.has_value())
    {
        return;
    }

    for (const WalletAccountDto &account : responseDto.result.value().list.value())
    {
        if (!account.coin.has_value())
        {
            continue;
        }

        for (const CoinBalanceDto &coin : account.coin.value())
        {
            const Decimal walletBalance = parseToDecimal(coin.walletBalance);

            Decimal freeAmount = parseToDecimal(coin.availableToWithdraw);
            if (freeAmount <= 0)
            {
                freeAmount = parseToDecimal(coin.availableToTrade);
            }

            Decimal lockedAmount = parseToDecimal(coin.locked);

            if (freeAmount <= 0 && walletBalance > 0)
            {
                freeAmount = walletBalance - lockedAmount;
            }
            if (lockedAmount <= 0 && walletBalance > 0 && freeAmount > 0)
            {
                lockedAmount = max(Decimal{0}, walletBalance - freeAmount);
            }

            updateBalanceCache(coin.coin, freeAmount, lockedAmount);
        }
    }
}

bool BybitDealService::isQuantityStepValid(Decimal quantity, Decimal stepSize) const
{
    if (stepSize <= 0)
    {
        return true;
    }

    const Decimal units = quantity / stepSize;
    return units == boost::decimal::floor(units);
}

optional<string>
BybitDealService::validateBaseQuantity(Decimal quantity, Decimal price, const SymbolInfo &symbolInfo) const
{
    if (!isQuantityStepValid(quantity, symbolInfo.stepSize))
    {
        return "quantity is not valid for step size";
    }
    if (symbolInfo.minQty > 0 && quantity < symbolInfo.minQty)
    {
        return "quantity " + DecimalConverter::formatDecimal(quantity) + " is below minQty " +
               DecimalConverter::formatDecimal(symbolInfo.minQty);
    }
    if (symbolInfo.maxQty > 0 && quantity > symbolInfo.maxQty)
    {
        return "quantity " + DecimalConverter::formatDecimal(quantity) + " is above maxQty " +
               DecimalConverter::formatDecimal(symbolInfo.maxQty);
    }

    const bool hasNotionalRule = symbolInfo.minNotional > 0 || symbolInfo.maxNotional > 0;
    if (hasNotionalRule && price <= 0)
    {
        return "price is required for notional validation";
    }

    if (price > 0)
    {
        const Decimal notional = quantity * price;
        if (symbolInfo.minNotional > 0 && notional < symbolInfo.minNotional)
        {
            return "notional " + DecimalConverter::formatDecimal(notional) + " is below minOrderAmt " +
                   DecimalConverter::formatDecimal(symbolInfo.minNotional);
        }
        if (symbolInfo.maxNotional > 0 && notional > symbolInfo.maxNotional)
        {
            return "notional " + DecimalConverter::formatDecimal(notional) + " is above maxOrderAmt " +
                   DecimalConverter::formatDecimal(symbolInfo.maxNotional);
        }
    }

    return nullopt;
}

optional<string> BybitDealService::validateQuoteQuantity(Decimal quantity, const SymbolInfo &symbolInfo) const
{
    if (symbolInfo.minNotional > 0 && quantity < symbolInfo.minNotional)
    {
        return "quote quantity " + DecimalConverter::formatDecimal(quantity) + " is below minOrderAmt " +
               DecimalConverter::formatDecimal(symbolInfo.minNotional);
    }
    if (symbolInfo.maxNotional > 0 && quantity > symbolInfo.maxNotional)
    {
        return "quote quantity " + DecimalConverter::formatDecimal(quantity) + " is above maxOrderAmt " +
               DecimalConverter::formatDecimal(symbolInfo.maxNotional);
    }

    return nullopt;
}

shared_ptr<BybitDealService::WebsocketStream> BybitDealService::prepareUserWebsocketStream()
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

    const long long expires = getServerTimestamp() + 5000;
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
            const AuthResponseDto authResponse = json::value_to<AuthResponseDto>(val);
            if (authResponse.op.has_value() && authResponse.op.value() == "auth")
            {
                throwIf(!authResponse.success.has_value() || !authResponse.success.value(),
                        "Bybit stream: auth failed: " + msg);
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

    return sharedWebsocketStream;
}

void BybitDealService::prepareUserStreamThread()
{
    runner = thread(
        [this]()
        {
            try
            {
                auto websocketStreamCopy = prepareUserWebsocketStream();

                setStreamStatus(StreamStatus::CONNECTED);
                try
                {
                    getBalancesRest();
                }
                catch (const exception &exception)
                {
                    cerr << "Bybit: initial balance refresh failed: " << exception.what() << endl;
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

    prepareUserStreamThread();
}

string BybitDealService::createBody(const string &baseAsset,
                                    const string &quoteAsset,
                                    const OrderCategory &category,
                                    const OrderOperation &operation,
                                    const OrderType &type,
                                    Decimal quantity,
                                    Decimal stepSize)
{
    const string qtyStr = DecimalConverter::formatByStep(quantity, stepSize);
    CreateOrderRequestDto request{EnumStringConverter<OrderCategory>::toString(category),
                                  baseAsset + quoteAsset,
                                  EnumStringConverter<OrderOperation>::toString(operation),
                                  EnumStringConverter<OrderType>::toString(type),
                                  qtyStr};

    // IMPORTANT: For spot MARKET BUY, force qty to be interpreted as baseCoin amount
    if (type == OrderType::MARKET && operation == OrderOperation::BUY)
    {
        request.marketUnit = "baseCoin";
    }

    return json::serialize(json::value_from(request));
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

void BybitDealService::updateBalanceCache(const string &asset, Decimal free, Decimal locked)
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

AssetBalance BybitDealService::parseBalance(const CoinBalanceDto &coin)
{
    const Decimal walletBalance = parseToDecimal(coin.walletBalance);
    const Decimal locked = parseToDecimal(coin.locked);
    const Decimal free = walletBalance - locked;

    return AssetBalance{coin.coin, free, locked};
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

optional<string> BybitDealService::isResponseStatusOk(const string &response)
{
    beast::error_code ec;
    json::value val = json::parse(response, ec);
    if (ec)
    {
        return "JSON parse error: " + ec.message();
    }
    if (!val.is_object())
    {
        return "Response is not a JSON object";
    }

    const json::object &object = val.as_object();
    if (!object.contains("retCode") || !object.at("retCode").is_number())
    {
        return "Missing retCode in response";
    }

    ResponseDto responseDto;
    try
    {
        responseDto = json::value_to<ResponseDto>(val);
    }
    catch (const exception &)
    {
        return "Missing retCode in response";
    }

    if (responseDto.retCode != 0)
    {
        return getErrorMessage(responseDto);
    }

    return nullopt;
}

void BybitDealService::setRequestParameters(boost::urls::url &url,
                                            const string &accountType,
                                            const optional<string> &coinFilter)
{
    map<string, string> parameterMap;
    parameterMap["accountType"] = accountType;
    if (coinFilter.has_value() && !coinFilter->empty())
    {
        parameterMap["coin"] = coinFilter.value();
    }
    setUrlParameters(url, parameterMap);
}

void BybitDealService::setRequestParameters(boost::urls::url &url, const string &symbol, const string &category)
{
    map<string, string> parameterMap;
    parameterMap["category"] = category;
    parameterMap["symbol"] = symbol;
    setUrlParameters(url, parameterMap);
}

void BybitDealService::setRequestParameters(boost::urls::url &url, const OrderQuery &request, const string &category)
{
    map<string, string> parameterMap;
    parameterMap["category"] = category;
    parameterMap["symbol"] = request.symbol;
    setParameterIfPresent(parameterMap, "orderId", request.orderId);
    setParameterIfPresent(parameterMap, "orderLinkId", request.clientOrderId);
    setUrlParameters(url, parameterMap);
}

string BybitDealService::sendOrder(const string &body, const flat_map<string, string> &headers)
{
    cout << "Sending order..." << endl;
    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/order/create");
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);
    context.setRequestBody(body);
    string response = httpsPost(context);
    cout << "Order response: " << response << endl;

    optional<string> errorOutput = isResponseStatusOk(response);
    throwIf(errorOutput.has_value(), "Order failed: " + errorOutput.value_or(""));
    return response;
}

Decimal BybitDealService::getTickerPrice(const string &symbol)
{
    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/market/tickers");
    setRequestParameters(requestUrl, symbol, string("spot"));
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    string response = httpsPost(context);

    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    if (!errorCode && jsonValue.is_object())
    {
        const TickerResponseDto responseDto = json::value_to<TickerResponseDto>(jsonValue);
        if (responseDto.result.has_value() && !responseDto.result.value().list.empty())
        {
            return DecimalConverter::parseDecimal(responseDto.result.value().list[0].lastPrice);
        }
    }
    return Decimal{};
}

OrderInfo BybitDealService::buyCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity)
{
    const string symbol = baseAsset + quoteAsset;

    SymbolInfo symbolInfo = getSymbolInfo(symbol);
    const Decimal lastPrice = getTickerPrice(symbol);
    optional<string> quantityError = validateBaseQuantity(quantity, lastPrice, symbolInfo);
    throwIf(quantityError.has_value(), "Bybit buyCrypto: " + quantityError.value_or(""));

    try
    {
        const auto quoteBalance = getBalance(quoteAsset);
        const Decimal quoteFree = quoteBalance.has_value() ? quoteBalance->free : Decimal{0};
        const Decimal requiredQuoteAmount = quantity * lastPrice;
        throwIf(quoteFree < requiredQuoteAmount,
                "Bybit BUY aborted: insufficient balance: need " +
                    DecimalConverter::formatDecimal(requiredQuoteAmount) + " " + quoteAsset + ", have " +
                    DecimalConverter::formatDecimal(quoteFree));
    }
    catch (const exception &exception)
    {
        throw runtime_error(string("Bybit buyCrypto: ") + exception.what());
    }

    cout << "Bybit Requested Qty: " << DecimalConverter::formatByStep(quantity, symbolInfo.stepSize)
         << " (Req: " << DecimalConverter::formatDecimal(quantity)
         << ", Price: " << DecimalConverter::formatDecimal(lastPrice)
         << ", MinOrderAmt: " << DecimalConverter::formatDecimal(symbolInfo.minNotional)
         << ", Step: " << DecimalConverter::formatDecimal(symbolInfo.stepSize) << ")" << endl;

    const msec timestamp = getServerTimestamp();
    const string body = createBody(baseAsset,
                                   quoteAsset,
                                   OrderCategory::SPOT,
                                   OrderOperation::BUY,
                                   OrderType::MARKET,
                                   quantity,
                                   symbolInfo.stepSize);

    const string signature = getSignature(body, timestamp);
    const flat_map<string, string> headers = createHeaders(apiKey, signature, timestamp);

    string response = sendOrder(body, headers);

    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode || !jsonValue.is_object(), "Failed to parse buyCrypto response");

    const OrderResponseDto responseDto = parseResponseToDto<OrderResponseDto>(jsonValue);
    throwIf(!responseDto.result.has_value(), "Missing result in buyCrypto response");

    OrderInfo info;
    info.symbol = symbol;
    info.category = "spot";
    info.side = "Buy";
    info.type = "Market";
    info.status = "New";
    info.origQty = quantity;
    info.leavesQty = quantity;

    info.orderId = responseDto.result.value().orderId.value_or("");
    info.clientOrderId = responseDto.result.value().orderLinkId.value_or("");

    info.createdTimeMs = timestamp;
    info.updatedTimeMs = timestamp;

    return info;
}

OrderInfo BybitDealService::sellCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity)
{
    const string symbol = baseAsset + quoteAsset;

    SymbolInfo symbolInfo = getSymbolInfo(symbol);
    const Decimal lastPrice = getTickerPrice(symbol);
    optional<string> quantityError = validateBaseQuantity(quantity, lastPrice, symbolInfo);
    throwIf(quantityError.has_value(), "Bybit sellCrypto: " + quantityError.value_or(""));

    try
    {
        const auto baseBalance = getBalance(baseAsset);
        const Decimal baseFree = baseBalance.has_value() ? baseBalance->free : Decimal{0};
        throwIf(baseFree < quantity,
                "Bybit SELL aborted: insufficient balance: need " + DecimalConverter::formatDecimal(quantity) + " " +
                    baseAsset + ", have " + DecimalConverter::formatDecimal(baseFree));
    }
    catch (const exception &exception)
    {
        throw runtime_error(string("Bybit sellCrypto: ") + exception.what());
    }

    cout << "Bybit Requested Qty: " << DecimalConverter::formatByStep(quantity, symbolInfo.stepSize)
         << " (Req: " << DecimalConverter::formatDecimal(quantity)
         << ", Price: " << DecimalConverter::formatDecimal(lastPrice)
         << ", MinOrderAmt: " << DecimalConverter::formatDecimal(symbolInfo.minNotional)
         << ", Step: " << DecimalConverter::formatDecimal(symbolInfo.stepSize) << ")" << endl;

    const msec timestamp = getServerTimestamp();
    const string body = createBody(baseAsset,
                                   quoteAsset,
                                   OrderCategory::SPOT,
                                   OrderOperation::SELL,
                                   OrderType::MARKET,
                                   quantity,
                                   symbolInfo.stepSize);

    const string signature = getSignature(body, timestamp);
    const flat_map<string, string> headers = createHeaders(apiKey, signature, timestamp);

    string response = sendOrder(body, headers);

    beast::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode || !jsonValue.is_object(), "Failed to parse sellCrypto response");

    const OrderResponseDto responseDto = parseResponseToDto<OrderResponseDto>(jsonValue);
    throwIf(!responseDto.result.has_value(), "Missing result in sellCrypto response");

    OrderInfo info;
    info.symbol = symbol;
    info.category = "spot";
    info.side = "Sell";
    info.type = "Market";
    info.status = "New";
    info.origQty = quantity;
    info.leavesQty = quantity;

    info.orderId = responseDto.result.value().orderId.value_or("");
    info.clientOrderId = responseDto.result.value().orderLinkId.value_or("");

    info.createdTimeMs = timestamp;
    info.updatedTimeMs = timestamp;

    return info;
}

void BybitDealService::validatePlaceOrderRequest(const PlaceOrderRequest &request) const
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(!request.side.has_value(), "Side is required");
    throwIf(!request.type.has_value(), "Type is required");
    throwIf(request.quantity <= 0, "Quantity must be greater than 0");
    if (request.type.value() == OrderType::LIMIT)
    {
        throwIf(!request.price.has_value() || request.price.value() <= 0, "Price must be > 0 for LIMIT orders");
        throwIf(!request.timeInForce.has_value() || request.timeInForce->empty(),
                "TimeInForce required for LIMIT orders");
    }
}

OrderInfo BybitDealService::placeOrder(const PlaceOrderRequest &request)
{
    validatePlaceOrderRequest(request);

    const OrderOperation requestSide = request.side.value();
    const OrderType requestType = request.type.value();
    string side = EnumStringConverter<OrderOperation>::toString(requestSide);
    string type = EnumStringConverter<OrderType>::toString(requestType);
    string category = request.category;
    if (category.empty())
    {
        category = "spot";
    }

    // Fetch symbol info for precision
    SymbolInfo info = getSymbolInfo(request.symbol, category);
    const bool isQuoteMarketBuy = requestType == OrderType::MARKET && requestSide == OrderOperation::BUY &&
                                  request.marketUnit.has_value() && request.marketUnit.value() == "quoteCoin";
    Decimal marketLastPrice{};
    if (requestType == OrderType::LIMIT && request.price.has_value())
    {
        optional<string> quantityError = validateBaseQuantity(request.quantity, request.price.value(), info);
        throwIf(quantityError.has_value(), "Bybit placeOrder: " + quantityError.value_or(""));
    }
    else if (isQuoteMarketBuy)
    {
        optional<string> quantityError = validateQuoteQuantity(request.quantity, info);
        throwIf(quantityError.has_value(), "Bybit placeOrder: " + quantityError.value_or(""));
    }
    else
    {
        marketLastPrice = getTickerPrice(request.symbol);
        optional<string> quantityError = validateBaseQuantity(request.quantity, marketLastPrice, info);
        throwIf(quantityError.has_value(), "Bybit placeOrder: " + quantityError.value_or(""));
    }

    try
    {
        const bool isBuyOrder = requestSide == OrderOperation::BUY;
        const string &baseAsset = info.baseAsset;
        const string &quoteAsset = info.quoteAsset;

        if (isBuyOrder)
        {
            const auto quoteBalance = getBalance(quoteAsset);
            const Decimal quoteFree = quoteBalance.has_value() ? quoteBalance->free : Decimal{0};

            Decimal requiredQuoteAmount{};

            if (requestType == OrderType::LIMIT && request.price.has_value())
            {
                requiredQuoteAmount = request.quantity * request.price.value();
            }
            else
            {
                if (isQuoteMarketBuy)
                {
                    requiredQuoteAmount = request.quantity;
                }
                else
                {
                    if (marketLastPrice > 0)
                    {
                        requiredQuoteAmount = request.quantity * marketLastPrice;
                    }
                }
            }

            if (requiredQuoteAmount > 0)
            {
                requiredQuoteAmount *= DecimalConverter::parseDecimal("1.01");
                throwIf(quoteFree < requiredQuoteAmount,
                        "Insufficient balance: need ~" + DecimalConverter::formatDecimal(requiredQuoteAmount) + " " +
                            quoteAsset + ", have " + DecimalConverter::formatDecimal(quoteFree));
            }
            else
            {
                throwIf(quoteFree <= 0, "Insufficient balance: no free " + quoteAsset);
            }
        }
        else
        {
            const auto baseBalance = getBalance(baseAsset);
            const Decimal baseFree = baseBalance.has_value() ? baseBalance->free : Decimal{0};

            throwIf(baseFree < request.quantity,
                    "Insufficient balance: need " + DecimalConverter::formatDecimal(request.quantity) + " " +
                        baseAsset + ", have " + DecimalConverter::formatDecimal(baseFree));
        }
    }
    catch (const exception &exception)
    {
        throw runtime_error(string("Bybit placeOrder: ") + exception.what());
    }

    // Use formatByStep for qty and price to avoid scientific notation and ensure correct precision
    const string qty = DecimalConverter::formatByStep(request.quantity, info.stepSize);

    optional<string> price;
    optional<string> timeInForce;
    if (requestType == OrderType::LIMIT)
    {
        price = DecimalConverter::formatByStep(request.price.value(), info.tickSize);
        timeInForce = request.timeInForce.value();
    }

    const CreateOrderRequestDto body{category,
                                     request.symbol,
                                     side,
                                     type,
                                     qty,
                                     price,
                                     timeInForce,
                                     request.clientOrderId,
                                     request.triggerPrice,
                                     request.orderFilter,
                                     request.marketUnit};
    string bodyStr = json::serialize(json::value_from(body));

    msec timestamp = getServerTimestamp();
    string signature = getSignature(bodyStr, timestamp);
    auto headers = createHeaders(apiKey, signature, timestamp);
    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/order/create");
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);
    context.setRequestBody(bodyStr);

    string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Response is not a JSON object");

    const OrderResponseDto responseDto = parseResponseToDto<OrderResponseDto>(jsonValue);
    throwIf(!responseDto.result.has_value(), "Missing result object in response");

    return createOrderInfo(responseDto.result.value(), request, side, type, timestamp);
}

OrderInfo BybitDealService::cancelOrder(const OrderQuery &request)
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(!request.orderId.has_value() && !request.clientOrderId.has_value(),
            "Either orderId or clientOrderId must be provided");

    string category = request.category;
    if (category.empty())
    {
        category = "spot";
    }

    const CancelOrderRequestDto body{category, request.symbol, request.orderId, request.clientOrderId};
    string bodyStr = json::serialize(json::value_from(body));

    msec timestamp = getServerTimestamp();
    string signature = getSignature(bodyStr, timestamp);
    auto headers = createHeaders(apiKey, signature, timestamp);
    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/order/cancel");
    string target = getTarget(requestUrl);

    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);
    context.setRequestBody(bodyStr);

    string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "Bybit cancelOrder: JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Response is not a JSON object");

    const OrderResponseDto responseDto = parseResponseToDto<OrderResponseDto>(jsonValue);
    throwIf(!responseDto.result.has_value(), "Missing result object in response");

    return createOrderInfo(responseDto.result.value(), request, timestamp);
}

OrderInfo BybitDealService::getOrder(const OrderQuery &request)
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(!request.orderId.has_value() && !request.clientOrderId.has_value(),
            "Either orderId or clientOrderId must be provided");

    string category = request.category;
    if (category.empty())
    {
        category = "spot";
    }

    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/order/realtime");
    setRequestParameters(requestUrl, request, category);
    string queryString = getQuery(requestUrl);
    msec timestamp = getServerTimestamp();

    string signature = getSignature(queryString, timestamp);

    auto headers = createHeaders(apiKey, signature, timestamp);

    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);
    context.setRequestHeaders(headers);

    string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "Bybit getOrder: JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Response is not a JSON object");

    const RealtimeOrderResponseDto responseDto = parseResponseToDto<RealtimeOrderResponseDto>(jsonValue);
    throwIf(!responseDto.result.has_value(), "Missing result object in response");

    throwIf(responseDto.result.value().list.empty(), "Order not found (empty list)");

    return createDetailedOrderInfo(responseDto.result.value().list[0], request, category, timestamp);
}

OrderInfo BybitDealService::createOrderInfo(const OrderResultDto &result, const OrderQuery &request, msec timestamp)
{
    OrderInfo info;
    info.symbol = request.symbol;
    info.category = request.category;
    info.status = "Cancelled";
    info.updatedTimeMs = timestamp;

    if (request.orderId.has_value())
    {
        info.orderId = request.orderId.value();
    }
    else
    {
        info.orderId = result.orderId.value_or("");
    }

    if (request.clientOrderId.has_value())
    {
        info.clientOrderId = request.clientOrderId.value();
    }
    else
    {
        info.clientOrderId = result.orderLinkId.value_or("");
    }

    info.executedQty = 0;
    info.cumQuoteQty = 0;
    info.avgPrice = 0;
    info.origQty = 0;
    info.leavesQty = 0;

    return info;
}

OrderInfo BybitDealService::createOrderInfo(const OrderResultDto &result,
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

    throwIf(!result.orderId.has_value(), "Missing required string field: orderId");
    info.orderId = result.orderId.value();
    info.clientOrderId = result.orderLinkId.value_or("");

    info.side = side;
    info.type = type;

    info.status = "New";

    if (request.timeInForce.has_value())
    {
        info.timeInForce = request.timeInForce.value();
    }

    if (request.price.has_value())
    {
        info.price = request.price.value();
    }

    info.origQty = request.quantity;
    info.leavesQty = request.quantity;

    info.executedQty = 0;
    info.cumQuoteQty = 0;
    info.avgPrice = 0;

    info.createdTimeMs = timestamp;
    info.updatedTimeMs = timestamp;

    return info;
}

SymbolInfo BybitDealService::getSymbolInfo(const string &symbol, const string &category)
{
    throwIf(symbol.empty(), "Symbol cannot be empty");

    string effectiveCategory = category.empty() ? "spot" : category;

    {
        lock_guard<mutex> lock(symbolInfoMutex);
        auto it = symbolInfoCache.find(symbol);
        if (it != symbolInfoCache.end())
        {
            return it->second;
        }
    }

    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/market/instruments-info");
    setRequestParameters(requestUrl, symbol, effectiveCategory);
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "Bybit getSymbolInfo: JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Response is not a JSON object");

    const InstrumentInfoResponseDto responseDto = parseResponseToDto<InstrumentInfoResponseDto>(jsonValue);
    throwIf(!responseDto.result.has_value(), "Missing result object");

    throwIf(responseDto.result.value().list.empty(), "Symbol not found: " + symbol);

    SymbolInfo info = createSymbolInfo(responseDto.result.value().list[0], symbol);
    {
        lock_guard<mutex> lock(symbolInfoMutex);
        symbolInfoCache[symbol] = info;
    }
    return info;
}

Decimal BybitDealService::ceilQuantityToStep(const string &symbol, Decimal quantity, const string &category)
{
    const SymbolInfo info = getSymbolInfo(symbol, category);
    return DecimalConverter::ceilToStep(quantity, info.stepSize);
}

OrderInfo BybitDealService::createDetailedOrderInfo(const OrderDto &order,
                                                    const OrderQuery &request,
                                                    const string &category,
                                                    msec timestamp)
{
    OrderInfo info;
    info.symbol = order.symbol.value_or("");
    if (info.symbol.empty())
    {
        info.symbol = request.symbol;
    }

    info.category = category;

    if (order.orderId.has_value())
    {
        info.orderId = order.orderId.value();
    }
    else if (request.orderId.has_value())
    {
        info.orderId = request.orderId.value();
    }

    if (order.orderLinkId.has_value())
    {
        info.clientOrderId = order.orderLinkId.value();
    }
    else if (request.clientOrderId.has_value())
    {
        info.clientOrderId = request.clientOrderId.value();
    }

    info.side = order.side.value_or("");
    info.type = order.orderType.value_or("");
    info.timeInForce = order.timeInForce.value_or("");
    info.status = order.orderStatus.value_or("");
    info.price = parseToDecimal(order.price);
    info.origQty = parseToDecimal(order.qty);
    info.executedQty = parseToDecimal(order.cumExecQty);
    info.cumQuoteQty = parseToDecimal(order.cumExecValue);
    info.leavesQty = parseToDecimal(order.leavesQty);
    info.avgPrice = parseToDecimal(order.avgPrice);
    info.createdTimeMs = parseTimestamp(order.createdTime);
    info.updatedTimeMs = parseTimestamp(order.updatedTime);

    if (info.updatedTimeMs == 0)
    {
        info.updatedTimeMs = timestamp;
    }

    return info;
}

SymbolInfo BybitDealService::createSymbolInfo(const InstrumentDto &instrument, const string &symbol)
{
    SymbolInfo info;
    info.symbol = symbol;
    if (instrument.symbol.has_value())
    {
        info.symbol = instrument.symbol.value();
    }
    info.status = instrument.status.value_or("");
    info.baseAsset = instrument.baseCoin.value_or("");
    info.quoteAsset = instrument.quoteCoin.value_or("");

    if (instrument.priceFilter.has_value())
    {
        const PriceFilterDto &priceFilter = instrument.priceFilter.value();
        info.tickSize = parseToDecimal(priceFilter.tickSize);
        info.minPrice = parseToDecimal(priceFilter.minPrice);
        info.maxPrice = parseToDecimal(priceFilter.maxPrice);
    }

    if (instrument.lotSizeFilter.has_value())
    {
        const LotSizeFilterDto &lotSizeFilter = instrument.lotSizeFilter.value();
        info.stepSize = parseToDecimal(lotSizeFilter.qtyStep);

        if (info.stepSize <= 0)
        {
            info.stepSize = parseToDecimal(lotSizeFilter.basePrecision);
        }

        info.minQty = parseToDecimal(lotSizeFilter.minOrderQty);
        info.maxQty = parseToDecimal(lotSizeFilter.maxOrderQty);

        info.minNotional = parseToDecimal(lotSizeFilter.minOrderAmt);
        info.maxNotional = parseToDecimal(lotSizeFilter.maxOrderAmt);
    }

    throwIf(info.tickSize <= 0, "Invalid tickSize");
    throwIf(info.stepSize <= 0, "Invalid stepSize");
    throwIf(info.minQty <= 0, "Invalid minQty");

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
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(!request.side.has_value(), "Side is required");
    throwIf(request.quantity <= 0, "Quantity must be greater than 0");
    throwIf(request.price <= 0, "Price must be greater than 0");
    throwIf(request.stopPrice <= 0, "StopPrice must be greater than 0");
    throwIf(request.stopLimitPrice.has_value() && request.stopLimitPrice.value() <= 0,
            "StopLimitPrice must be greater than 0 if set");

    string groupId;
    if (request.listClientOrderId.has_value() && !request.listClientOrderId->empty())
    {
        groupId = request.listClientOrderId.value();
    }
    else
    {
        msec now = getServerTimestamp();
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
    takeProfitRequest.type = OrderType::LIMIT;
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
    stopLossRequest.triggerPrice = DecimalConverter::formatDecimal(request.stopPrice);

    if (request.stopLimitPrice.has_value())
    {
        stopLossRequest.type = OrderType::LIMIT;
        stopLossRequest.price = request.stopLimitPrice.value();
        stopLossRequest.timeInForce =
            request.stopLimitTimeInForce.has_value() ? request.stopLimitTimeInForce.value() : "GTC";
    }
    else
    {
        stopLossRequest.type = OrderType::MARKET;
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
        groupId = request.orderListId.value();
    }
    else if (request.listClientOrderId.has_value())
    {
        groupId = request.listClientOrderId.value();
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

    throwIf(!found, "Bybit cancelOco: unknown group params");

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
                takeProfitCancelInfo.orderId = ocoGroup.takeProfit.orderId.value();
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
                stopLossCancelInfo.orderId = ocoGroup.stopLeg.orderId.value();
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

        const StreamMessageDto message = json::value_to<StreamMessageDto>(jsonValue);
        if (!message.topic.has_value())
        {
            return;
        }

        const string &topic = message.topic.value();

        if (topic == "wallet")
        {
            handleWalletUpdate(message);
        }
        else if (topic == "order")
        {
            handleOrderUpdate(json::value_to<StreamOrderMessageDto>(jsonValue));
        }
    }
    catch (const exception &e)
    {
        cerr << "Bybit stream internal error: " << e.what() << endl;
    }
}

void BybitDealService::handleWalletUpdate(const StreamMessageDto &message)
{
    if (!message.data.has_value())
    {
        return;
    }

    for (const WalletAccountDto &item : message.data.value())
    {
        if (item.coin.has_value())
        {
            for (const CoinBalanceDto &coin : item.coin.value())
            {
                try
                {
                    AssetBalance balance = parseBalance(coin);
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

void BybitDealService::handleOrderUpdate(const StreamOrderMessageDto &message)
{
    if (!message.data.has_value())
    {
        return;
    }

    for (const StreamOrderDto &order : message.data.value())
    {
        const auto &status = order.orderStatus;
        if (status.has_value() && status.value() == "Filled")
        {
            const auto &orderLinkId = order.orderLinkId;
            if (orderLinkId.has_value())
            {
                processOcoUpdate(orderLinkId.value());
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

void BybitDealService::cancelAllOpenOrders(const string &symbol, const string &category)
{
    const string effectiveCategory = category.empty() ? "spot" : category;

    const CancelAllOpenOrdersRequestDto request{effectiveCategory, symbol};
    const string bodyStr = json::serialize(json::value_from(request));

    const msec timestamp = getServerTimestamp();
    const string signature = getSignature(bodyStr, timestamp);
    const auto headers = createHeaders(apiKey, signature, timestamp);

    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/order/cancel-all");
    const string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::post);
    context.setRequestHeaders(headers);
    context.setRequestBody(bodyStr);

    const string response = httpsPost(context);

    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "Bybit cancelAllOpenOrders: JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Bybit cancelAllOpenOrders: Response is not a JSON object");

    parseResponseToDto<ResponseDto>(jsonValue);
}

flat_map<string, AssetBalance> BybitDealService::getBalancesRest()
{
    try
    {
        refreshBalancesFromRest("UNIFIED", nullopt);
    }
    catch (const exception &e)
    {
        cerr << "Bybit balance REST refresh (UNIFIED) failed: " << e.what() << endl;
    }

    try
    {
        refreshBalancesFromRest("SPOT", nullopt);
    }
    catch (const exception &e)
    {
        cerr << "Bybit balance REST refresh (SPOT) failed: " << e.what() << endl;
    }

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
            if (orderInfo.status == "Filled" || orderInfo.status == "Cancelled" || orderInfo.status == "Rejected" ||
                orderInfo.status == "Deactivated" || orderInfo.status == "Triggered")
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
