#include "BybitDealService.hpp"
#include "EnumStringConverter.hpp"
#include "common/exception_handling.hpp"
#include "common/http_request.hpp"
#include "domain/AuthRequestDto.hpp"
#include "domain/AuthResponseDto.hpp"
#include "domain/CancelAllOpenOrdersRequestDto.hpp"
#include "domain/CancelOrderRequestDto.hpp"
#include "domain/CreateOrderRequestDto.hpp"
#include "domain/InstrumentInfoResponseDto.hpp"
#include "domain/OrderPriceLimitResponseDto.hpp"
#include "domain/OrderResponseDto.hpp"
#include "domain/RealtimeOrderResponseDto.hpp"
#include "domain/ResponseDto.hpp"
#include "domain/ServerTimeResponseDto.hpp"
#include "domain/SubscribeRequestDto.hpp"
#include "domain/TickerResponseDto.hpp"
#include "domain/WalletBalanceResponseDto.hpp"

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
    constexpr size_t BYBIT_OCO_GROUP_ID_MAX_LENGTH = 33;

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

    bool isTerminalStatus(const string &status)
    {
        return status == "Filled" || status == "Cancelled" || status == "Rejected" || status == "Deactivated" ||
               status == "Triggered";
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

bool BybitDealService::isTimeSyncRecent() const
{
    long long currentMonoMs = chrono::steady_clock::now().time_since_epoch().count() / 1000000;
    long long lastMonoMs = lastSyncMonoMs.load();
    return currentMonoMs - lastMonoMs < 10000;
}

void BybitDealService::syncTime()
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

    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/market/time");
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);
    string response = httpsPost(context);

    beast::error_code ec;
    json::value val = json::parse(response, ec);
    throwIf(ec || !val.is_object(), ec.what());

    const ServerTimeResponseDto responseDto = parseResponseToDto<ServerTimeResponseDto>(val);
    throwIf(!responseDto.result.has_value(), "Missing result object");
    long long serverTime = stoll(responseDto.result.value().timeSecond) * 1000;
    long long localTime =
        chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    serverTimeOffset = serverTime - localTime;
    long long currentMonoMs = chrono::steady_clock::now().time_since_epoch().count() / 1000000;
    lastSyncMonoMs.store(currentMonoMs);
    cout << "Bybit time synced. Offset: " << serverTimeOffset << "ms" << endl;
}

long long BybitDealService::getServerTimestamp()
{
    syncTime();
    long long localTime =
        chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    return localTime + serverTimeOffset;
}

void BybitDealService::refreshBalancesCache(const string &accountType, const optional<string> &coinFilter)
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
    processCoins(responseDto);
}

void BybitDealService::tryRefreshBalancesCache(const string &accountType, const optional<string> &coinFilter)
{
    try
    {
        refreshBalancesCache(accountType, coinFilter);
    }
    catch (const exception &e)
    {
        cerr << "Bybit balance REST refresh (" << accountType << ") failed: " << e.what() << endl;
    }
}

void BybitDealService::processCoins(const WalletBalanceResponseDto &responseDto)
{
    if (!responseDto.result.has_value() || !responseDto.result.value().list.has_value())
    {
        return;
    }

    for (const WalletAccountDto &account : responseDto.result.value().list.value())
    {
        for (const CoinBalanceDto &coin : account.coin.value_or(vector<CoinBalanceDto>{}))
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

optional<string> BybitDealService::validateQuantity(Decimal quantity, Decimal price, const SymbolInfo &symbolInfo) const
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
        return validateNotional(notional, symbolInfo);
    }

    return nullopt;
}

optional<string> BybitDealService::validateNotional(Decimal notional, const SymbolInfo &symbolInfo) const
{
    if (symbolInfo.minNotional > 0 && notional < symbolInfo.minNotional)
    {
        return DecimalConverter::formatDecimal(notional) + " is below minOrderAmt " +
               DecimalConverter::formatDecimal(symbolInfo.minNotional);
    }
    if (symbolInfo.maxNotional > 0 && notional > symbolInfo.maxNotional)
    {
        return DecimalConverter::formatDecimal(notional) + " is above maxOrderAmt " +
               DecimalConverter::formatDecimal(symbolInfo.maxNotional);
    }

    return nullopt;
}

void BybitDealService::authenticateUserWebsocketStream(WebsocketStream &websocketStream)
{
    const long long expires = getServerTimestamp() + 5000;
    const string payload = "GET/realtime" + to_string(expires);
    const string sig = hmac_sha256(secretKey, payload);

    const AuthRequestDto authRequest{"auth", apiKey, expires, sig};
    const string auth = json::serialize(json::value_from(authRequest));
    websocketStream.write(net::buffer(auth));

    beast::flat_buffer buffer;
    websocketStream.read(buffer);
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

void BybitDealService::subscribeUserWebsocketStream(WebsocketStream &websocketStream)
{
    const SubscribeRequestDto subscribeRequest{"subscribe", {"wallet", "order"}};
    const string sub = json::serialize(json::value_from(subscribeRequest));
    websocketStream.write(net::buffer(sub));

    beast::flat_buffer buffer;
    websocketStream.read(buffer);
    string msg = beast::buffers_to_string(buffer.data());
    cout << "Bybit stream subscription response: " << msg << endl;
}

shared_ptr<WebsocketStream> BybitDealService::prepareUserWebsocketStream()
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

    authenticateUserWebsocketStream(*sharedWebsocketStream);
    subscribeUserWebsocketStream(*sharedWebsocketStream);

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

                    if (!errorCode)
                    {
                        const string message = beast::buffers_to_string(readBuffer.data());
                        handleUserStreamMessage(message);
                    }
                    else
                    {
                        if (!userStream || errorCode == net::error::operation_aborted || errorCode == net::error::eof ||
                            errorCode == ssl::error::stream_truncated || errorCode == ws::error::closed)
                        {
                            break;
                        }
                    }
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

json::value BybitDealService::parseAndValidate(const string &response) const
{
    boost::system::error_code errorCode;
    json::value jsonValue = json::parse(response, errorCode);
    throwIf(errorCode.failed(), "JSON parse error: " + errorCode.message());
    throwIf(!jsonValue.is_object(), "Response is not a JSON object");

    return jsonValue;
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

void BybitDealService::setRequestParameters(boost::urls::url &url, const string &symbol, OrderCategory category)
{
    map<string, string> parameterMap;
    parameterMap["category"] = EnumStringConverter<OrderCategory>::toString(category);
    parameterMap["symbol"] = symbol;
    setUrlParameters(url, parameterMap);
}

void BybitDealService::setRequestParameters(boost::urls::url &url, const OrderQuery &request, OrderCategory category)
{
    map<string, string> parameterMap;
    parameterMap["category"] = EnumStringConverter<OrderCategory>::toString(category);
    parameterMap["symbol"] = request.symbol;
    setParameterIfPresent(parameterMap, "orderId", request.orderId);
    setParameterIfPresent(parameterMap, "orderLinkId", request.clientOrderId);
    setUrlParameters(url, parameterMap);
}

OrderPriceLimitDto BybitDealService::getOrderPriceLimit(const string &symbol, OrderCategory category)
{
    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/market/price-limit");
    setRequestParameters(requestUrl, symbol, category);
    string target = getTarget(requestUrl);

    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    string response = httpsPost(context);

    const json::value jsonValue = parseAndValidate(response);
    const OrderPriceLimitResponseDto responseDto = parseResponseToDto<OrderPriceLimitResponseDto>(jsonValue);
    throwIf(!responseDto.result.has_value(), "Missing result object in response");

    return responseDto.result.value();
}

Decimal BybitDealService::getTickerPrice(const string &symbol)
{
    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/market/tickers");
    setRequestParameters(requestUrl, symbol, OrderCategory::SPOT);
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
    PlaceOrderRequest request;
    request.symbol = baseAsset + quoteAsset;
    request.side = OrderOperation::BUY;
    request.type = OrderType::MARKET;
    request.quantity = quantity;
    request.marketUnit = "baseCoin";

    return placeOrder(request);
}

OrderInfo BybitDealService::sellCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity)
{
    PlaceOrderRequest request;
    request.symbol = baseAsset + quoteAsset;
    request.side = OrderOperation::SELL;
    request.type = OrderType::MARKET;
    request.quantity = quantity;

    return placeOrder(request);
}

void BybitDealService::validatePlaceOrderRequest(const PlaceOrderRequest &request) const
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

void BybitDealService::validatePlaceOcoRequest(const PlaceOcoRequest &request) const
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(request.quantity <= 0, "Quantity must be greater than 0");
    throwIf(request.price <= 0, "Price must be greater than 0");
    throwIf(request.stopPrice <= 0, "StopPrice must be greater than 0");
    throwIf(request.stopLimitPrice.has_value() && request.stopLimitPrice.value() <= 0,
            "StopLimitPrice must be greater than 0 if set");
}

void BybitDealService::validateOrderPriceLimit(const PlaceOrderRequest &request)
{
    if (request.type != OrderType::LIMIT)
    {
        return;
    }

    const OrderPriceLimitDto priceLimit = getOrderPriceLimit(request.symbol, request.category);
    const Decimal price = request.price.value();

    if (request.side == OrderOperation::BUY)
    {
        throwIf(!priceLimit.buyLmt.has_value() || priceLimit.buyLmt->empty(),
                "Bybit placeOrder: missing buyLmt in price limit response");

        const Decimal buyLimit = DecimalConverter::parseDecimal(priceLimit.buyLmt.value());
        throwIf(price > buyLimit,
                "Bybit placeOrder: price " + DecimalConverter::formatDecimal(price) + " is above buyLmt " +
                    DecimalConverter::formatDecimal(buyLimit));
    }
    else
    {
        throwIf(!priceLimit.sellLmt.has_value() || priceLimit.sellLmt->empty(),
                "Bybit placeOrder: missing sellLmt in price limit response");

        const Decimal sellLimit = DecimalConverter::parseDecimal(priceLimit.sellLmt.value());
        throwIf(price < sellLimit,
                "Bybit placeOrder: price " + DecimalConverter::formatDecimal(price) + " is below sellLmt " +
                    DecimalConverter::formatDecimal(sellLimit));
    }
}

void BybitDealService::checkBalance(const PlaceOrderRequest &request, const SymbolInfo &info)
{
    if (request.side == OrderOperation::BUY)
    {
        const auto quoteBalance = getBalance(info.quoteAsset);
        const Decimal quoteFree = quoteBalance.has_value() ? quoteBalance->free : Decimal{0};

        Decimal requiredQuoteAmount{};

        if (request.type == OrderType::LIMIT && request.price.has_value())
        {
            optional<string> quantityError = validateQuantity(request.quantity, request.price.value(), info);
            throwIf(quantityError.has_value(), "Bybit placeOrder: notional " + quantityError.value_or(""));
            requiredQuoteAmount = request.quantity * request.price.value();
        }
        else if (request.type == OrderType::MARKET && request.side == OrderOperation::BUY &&
                 request.marketUnit.has_value() && request.marketUnit.value() == "quoteCoin")
        {
            optional<string> quantityError = validateNotional(request.quantity, info);
            throwIf(quantityError.has_value(), "Bybit placeOrder: quote quantity " + quantityError.value_or(""));
            requiredQuoteAmount = request.quantity;
        }
        else
        {
            Decimal marketLastPrice = getTickerPrice(request.symbol);
            optional<string> quantityError = validateQuantity(request.quantity, marketLastPrice, info);
            throwIf(quantityError.has_value(), "Bybit placeOrder: notional " + quantityError.value_or(""));
            if (marketLastPrice > 0)
            {
                requiredQuoteAmount = request.quantity * marketLastPrice;
            }
        }

        throwIf(requiredQuoteAmount > 0 && quoteFree < requiredQuoteAmount,
                "Insufficient balance: need " + DecimalConverter::formatDecimal(requiredQuoteAmount) + " " +
                    info.quoteAsset + ", have " + DecimalConverter::formatDecimal(quoteFree));
        throwIf(quoteFree <= 0, "Insufficient balance: no free " + info.quoteAsset);
    }
    else
    {
        const auto baseBalance = getBalance(info.baseAsset);
        const Decimal baseFree = baseBalance.has_value() ? baseBalance->free : Decimal{0};

        throwIf(baseFree < request.quantity,
                "Insufficient balance: need " + DecimalConverter::formatDecimal(request.quantity) + " " +
                    info.baseAsset + ", have " + DecimalConverter::formatDecimal(baseFree));
    }
}

string BybitDealService::createRequestBody(const PlaceOrderRequest &request, const SymbolInfo &info) const
{
    const string category = EnumStringConverter<OrderCategory>::toString(request.category);
    const string side = EnumStringConverter<OrderOperation>::toString(request.side);
    const string type = EnumStringConverter<OrderType>::toString(request.type);
    const string qty = DecimalConverter::formatByStep(request.quantity, info.stepSize);

    optional<string> price;
    optional<string> timeInForce;
    if (request.type == OrderType::LIMIT)
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

    return json::serialize(json::value_from(body));
}

string BybitDealService::createRequestBody(const OrderQuery &request) const
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(!request.orderId.has_value() && !request.clientOrderId.has_value(),
            "Either orderId or clientOrderId must be provided");

    string category = EnumStringConverter<OrderCategory>::toString(request.category);

    const CancelOrderRequestDto body{category, request.symbol, request.orderId, request.clientOrderId};
    return json::serialize(json::value_from(body));
}

OrderInfo BybitDealService::placeOrder(const PlaceOrderRequest &request)
{
    validatePlaceOrderRequest(request);
    validateOrderPriceLimit(request);

    SymbolInfo info = getSymbolInfo(request.symbol, request.category);
    checkBalance(request, info);

    string bodyStr = createRequestBody(request, info);

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

    const json::value jsonValue = parseAndValidate(response);
    const OrderResponseDto responseDto = parseResponseToDto<OrderResponseDto>(jsonValue);
    throwIf(!responseDto.result.has_value(), "Missing result object in response");

    return createOrderInfo(responseDto.result.value(), request, timestamp);
}

OrderInfo BybitDealService::cancelOrder(const OrderQuery &request)
{
    string bodyStr = createRequestBody(request);

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

    const json::value jsonValue = parseAndValidate(response);

    const OrderResponseDto responseDto = parseResponseToDto<OrderResponseDto>(jsonValue);
    throwIf(!responseDto.result.has_value(), "Missing result object in response");

    return createOrderInfo(responseDto.result.value(), request, timestamp);
}

OrderInfo BybitDealService::getOrder(const OrderQuery &request)
{
    throwIf(request.symbol.empty(), "Symbol cannot be empty");
    throwIf(!request.orderId.has_value() && !request.clientOrderId.has_value(),
            "Either orderId or clientOrderId must be provided");

    boost::urls::url requestUrl;
    requestUrl.set_path("/v5/order/realtime");
    setRequestParameters(requestUrl, request, request.category);
    string queryString = getQuery(requestUrl);
    msec timestamp = getServerTimestamp();

    string signature = getSignature(queryString, timestamp);

    auto headers = createHeaders(apiKey, signature, timestamp);

    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);
    context.setRequestHeaders(headers);

    string response = httpsPost(context);

    const json::value jsonValue = parseAndValidate(response);

    const RealtimeOrderResponseDto responseDto = parseResponseToDto<RealtimeOrderResponseDto>(jsonValue);
    throwIf(!responseDto.result.has_value(), "Missing result object in response");
    throwIf(responseDto.result.value().list.empty(), "Order not found (empty list)");

    return createOrderInfo(responseDto.result.value().list[0], request, timestamp);
}

OrderInfo BybitDealService::createOrderInfo(const OrderResultDto &result, const OrderQuery &request, msec timestamp)
{
    OrderInfo info;
    info.symbol = request.symbol;
    info.category = EnumStringConverter<OrderCategory>::toString(request.category);
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

OrderInfo
BybitDealService::createOrderInfo(const OrderResultDto &result, const PlaceOrderRequest &request, msec timestamp)
{
    OrderInfo info;
    info.symbol = request.symbol;

    info.category = EnumStringConverter<OrderCategory>::toString(request.category);

    throwIf(!result.orderId.has_value(), "Missing required string field: orderId");
    info.orderId = result.orderId.value();
    info.clientOrderId = result.orderLinkId.value_or("");

    info.side = EnumStringConverter<OrderOperation>::toString(request.side);
    info.type = EnumStringConverter<OrderType>::toString(request.type);

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

OrderInfo BybitDealService::createOrderInfo(const OrderDto &order, const OrderQuery &request, msec timestamp)
{
    OrderInfo info;
    info.symbol = order.symbol.value_or("");
    if (info.symbol.empty())
    {
        info.symbol = request.symbol;
    }

    info.category = EnumStringConverter<OrderCategory>::toString(request.category);

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

SymbolInfo BybitDealService::getSymbolInfo(const string &symbol, OrderCategory category)
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
    requestUrl.set_path("/v5/market/instruments-info");
    setRequestParameters(requestUrl, symbol, category);
    string target = getTarget(requestUrl);
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    string response = httpsPost(context);

    const json::value jsonValue = parseAndValidate(response);

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

Decimal BybitDealService::ceilQuantityToStep(const string &symbol, Decimal quantity, OrderCategory category)
{
    const SymbolInfo info = getSymbolInfo(symbol, category);
    return DecimalConverter::ceilToStep(quantity, info.stepSize);
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

OrderInfo BybitDealService::createTakeProfitOcoRequest(const PlaceOcoRequest &request,
                                                       const string &takeProfitOrderLinkId)
{
    PlaceOrderRequest takeProfitRequest;
    takeProfitRequest.symbol = request.symbol;
    takeProfitRequest.side = request.side;
    takeProfitRequest.type = OrderType::LIMIT;
    takeProfitRequest.quantity = request.quantity;
    takeProfitRequest.price = request.price;
    takeProfitRequest.timeInForce = "GTC";
    takeProfitRequest.clientOrderId = takeProfitOrderLinkId;
    takeProfitRequest.category = OrderCategory::SPOT;

    OrderInfo takeProfitOrderInfo;
    try
    {
        takeProfitOrderInfo = placeOrder(takeProfitRequest);
    }
    catch (const exception &e)
    {
        throw runtime_error("Bybit placeOco: failed to place TP leg: " + string(e.what()));
    }

    return takeProfitOrderInfo;
}

OrderInfo BybitDealService::createStopLossOcoRequest(const PlaceOcoRequest &request,
                                                     const string &stopLossOrderLinkId,
                                                     const string &takeProfitOrderLinkId)
{
    PlaceOrderRequest stopLossRequest;
    stopLossRequest.symbol = request.symbol;
    stopLossRequest.side = request.side;
    stopLossRequest.category = OrderCategory::SPOT;
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
        const optional<string> rollbackError = rollbackOcoRequests(request, takeProfitOrderLinkId);
        throw runtime_error("Bybit placeOco: failed to place SL leg: " + string(e.what()) + rollbackError.value_or(""));
    }

    return stopLossOrderInfo;
}

optional<string> BybitDealService::rollbackOcoRequests(const PlaceOcoRequest &request,
                                                       const string &takeProfitOrderLinkId)
{
    try
    {
        OrderQuery rollbackQuery;
        rollbackQuery.symbol = request.symbol;
        rollbackQuery.clientOrderId = takeProfitOrderLinkId;
        cancelOrder(rollbackQuery);
    }
    catch (const exception &rollbackEx)
    {
        return "; Rollback failed: " + string(rollbackEx.what());
    }

    return nullopt;
}

void BybitDealService::createOcoGroup(const PlaceOcoRequest &request,
                                      const string &groupId,
                                      const string &takeProfitOrderLinkId,
                                      const string &stopLossOrderLinkId,
                                      const OrderInfo &takeProfitOrderInfo,
                                      const OrderInfo &stopLossOrderInfo)
{
    BybitOcoGroup group;
    group.groupId = groupId;

    group.takeProfit.symbol = request.symbol;
    group.takeProfit.orderId = takeProfitOrderInfo.orderId;
    group.takeProfit.clientOrderId = takeProfitOrderLinkId;
    group.takeProfit.category = OrderCategory::SPOT;

    group.stopLeg.symbol = request.symbol;
    group.stopLeg.orderId = stopLossOrderInfo.orderId;
    group.stopLeg.clientOrderId = stopLossOrderLinkId;
    group.stopLeg.category = OrderCategory::SPOT;

    group.tpOrderLinkId = takeProfitOrderLinkId;
    group.slOrderLinkId = stopLossOrderLinkId;

    ocoGroups[groupId] = group;
    ocoLegToGroup[takeProfitOrderLinkId] = groupId;
    ocoLegToGroup[stopLossOrderLinkId] = groupId;
}

OcoInfo BybitDealService::createOcoInfo(const string &groupId,
                                        const OrderInfo &takeProfitOrderInfo,
                                        const OrderInfo &stopLossOrderInfo) const
{
    OcoInfo ocoInfo;
    ocoInfo.orderListId = groupId;
    ocoInfo.listClientOrderId = groupId;
    ocoInfo.transactTimeMs = takeProfitOrderInfo.createdTimeMs;
    ocoInfo.orders.push_back(takeProfitOrderInfo);
    ocoInfo.orders.push_back(stopLossOrderInfo);

    return ocoInfo;
}

optional<string> BybitDealService::cancelOcoGroupOrder(const OrderQuery &order, OrderInfo &cancelInfo)
{
    try
    {
        cancelInfo = cancelOrder(order);
        return nullopt;
    }
    catch (const exception &e)
    {
        const string cancelError = e.what();
        const string orderId = order.orderId.value();

        try
        {
            OrderInfo check = getOrder(order);
            cancelInfo = check;
            if (isTerminalStatus(check.status))
            {
                return nullopt;
            }
        }
        catch (...)
        {
            cancelInfo.status = "ERROR";
            cancelInfo.symbol = order.symbol;
            if (order.orderId.has_value())
            {
                cancelInfo.orderId = orderId;
            }

            return orderId + "order cancel failed and status check failed: " + cancelError + "; ";
        }

        cancelInfo.status = "ERROR";
        cancelInfo.symbol = order.symbol;
        if (order.orderId.has_value())
        {
            cancelInfo.orderId = orderId;
        }

        return orderId + "order cancel failed and not terminal: " + cancelError + "; ";
    }
}

optional<string> BybitDealService::cancelOcoOtherLegFromUpdate(const OrderQuery &order)
{
    string cancelError;

    try
    {
        cancelOrder(order);
        return nullopt;
    }
    catch (const exception &e)
    {
        cancelError = e.what();
        try
        {
            OrderInfo info = getOrder(order);
            if (isTerminalStatus(info.status))
            {
                return nullopt;
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

    return cancelError;
}

OcoInfo BybitDealService::placeOco(const PlaceOcoRequest &request)
{
    validatePlaceOcoRequest(request);

    string groupId = request.listClientOrderId.value_or("");
    if (groupId.empty())
    {
        groupId = generateUniqueOcoId();
    }
    throwIf(groupId.size() > BYBIT_OCO_GROUP_ID_MAX_LENGTH,
            "Bybit placeOco: listClientOrderId must be at most " + to_string(BYBIT_OCO_GROUP_ID_MAX_LENGTH) +
                " characters");

    string takeProfitOrderLinkId = groupId + "_TP";
    string stopLossOrderLinkId = groupId + "_SL";
    const OrderInfo takeProfitOrderInfo = createTakeProfitOcoRequest(request, takeProfitOrderLinkId);
    const OrderInfo stopLossOrderInfo = createStopLossOcoRequest(request, stopLossOrderLinkId, takeProfitOrderLinkId);

    {
        lock_guard<mutex> lock(ocoMutex);
        createOcoGroup(request,
                       groupId,
                       takeProfitOrderLinkId,
                       stopLossOrderLinkId,
                       takeProfitOrderInfo,
                       stopLossOrderInfo);
    }

    return createOcoInfo(groupId, takeProfitOrderInfo, stopLossOrderInfo);
}

OcoInfo BybitDealService::cancelOco(const OrderListQuery &request)
{
    string groupId = request.orderListId.has_value() ? request.orderListId.value() : request.listClientOrderId.value();
    throwIf(groupId == "", "Bybit cancelOco: missing orderListId or listClientOrderId");

    BybitOcoGroup ocoGroup;
    {
        lock_guard<mutex> lock(ocoMutex);
        auto it = ocoGroups.find(groupId);
        throwIf(it == ocoGroups.end(), "Bybit cancelOco: unknown group params");
        ocoGroup = it->second;
    }

    OrderInfo takeProfitCancelInfo, stopLossCancelInfo;
    const optional<string> takeProfitError = cancelOcoGroupOrder(ocoGroup.takeProfit, takeProfitCancelInfo);
    const optional<string> stopLossError = cancelOcoGroupOrder(ocoGroup.stopLeg, stopLossCancelInfo);

    throwIf(takeProfitError.has_value() || stopLossError.has_value(),
            "Bybit cancelOco incomplete: " + takeProfitError.value_or("") + stopLossError.value_or(""));

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
    return createOcoInfo(groupId, takeProfitCancelInfo, stopLossCancelInfo);
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
                AssetBalance balance = parseBalance(coin);
                updateBalanceCache(balance.asset, balance.free, balance.locked);
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
        const auto &orderLinkId = order.orderLinkId;
        if (status.has_value() && status.value() == "Filled" && orderLinkId.has_value())
        {
            processOcoUpdate(orderLinkId.value());
        }
    }
}

void BybitDealService::processOcoUpdate(const string &orderLinkId)
{
    string groupId;
    OrderQuery otherLegQuery;

    {
        lock_guard<mutex> lock(ocoMutex);
        auto legIt = ocoLegToGroup.find(orderLinkId);
        if (legIt == ocoLegToGroup.end())
        {
            return;
        }

        groupId = legIt->second;
        auto groupIt = ocoGroups.find(groupId);
        if (groupIt == ocoGroups.end())
        {
            return;
        }

        BybitOcoGroup &group = groupIt->second;
        if (group.closing)
        {
            return;
        }

        group.closing = true;
        if (orderLinkId == group.tpOrderLinkId)
        {
            otherLegQuery = group.stopLeg;
        }
        else
        {
            otherLegQuery = group.takeProfit;
        }
    }

    const optional<string> cancelError = cancelOcoOtherLegFromUpdate(otherLegQuery);

    {
        lock_guard<mutex> lock(ocoMutex);
        auto groupIt = ocoGroups.find(groupId);
        if (groupIt == ocoGroups.end())
        {
            return;
        }

        if (cancelError.has_value())
        {
            groupIt->second.closing = false;
            groupIt->second.lastError = cancelError.value();
        }
        else
        {
            ocoLegToGroup.erase(groupIt->second.tpOrderLinkId);
            ocoLegToGroup.erase(groupIt->second.slOrderLinkId);
            ocoGroups.erase(groupIt);
        }
    }
}

void BybitDealService::cancelAllOpenOrders(const string &symbol, OrderCategory category)
{
    const CancelAllOpenOrdersRequestDto request{EnumStringConverter<OrderCategory>::toString(category), symbol};
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

    const json::value jsonValue = parseAndValidate(response);

    parseResponseToDto<ResponseDto>(jsonValue);
}

flat_map<string, AssetBalance> BybitDealService::getBalancesRest()
{
    tryRefreshBalancesCache("UNIFIED", nullopt);
    tryRefreshBalancesCache("SPOT", nullopt);

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
            orderQuery.category = OrderCategory::SPOT;

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
