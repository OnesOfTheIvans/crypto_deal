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

    boost::system::error_code ec;
    json::value jsonValue = json::parse(response, ec);
    if (ec)
    {
        throw runtime_error("Bybit cancelOrder: JSON parse error: " + ec.message());
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

    ostringstream qs;
    qs << "category=" << category << "&symbol=" << request.symbol;

    if (request.orderId.has_value())
    {
        qs << "&orderId=" << *request.orderId;
    }
    if (request.clientOrderId.has_value())
    {
        qs << "&orderLinkId=" << *request.clientOrderId;
    }

    string queryString = qs.str();
    msec timestamp = getTimestamp();

    string signature = getSignature(queryString, timestamp);

    auto headers = createHeaders(apiKey, signature, timestamp);

    string target = "/v5/order/realtime?" + queryString;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);
    context.setRequestHeaders(headers);

    string response = httpsPost(context);

    boost::system::error_code ec;
    json::value jsonValue = json::parse(response, ec);
    if (ec)
    {
        throw runtime_error("Bybit getOrder: JSON parse error: " + ec.message());
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

    if (!result.contains("list") || !result.at("list").is_array())
    {
        throw runtime_error("Missing or invalid list in response");
    }
    json::array &list = result.at("list").as_array();

    if (list.empty())
    {
        throw runtime_error("Order not found (empty list)");
    }
    if (!list[0].is_object())
    {
        throw runtime_error("Invalid order object in list");
    }
    const json::object &orderObj = list[0].as_object();

    return createDetailedOrderInfo(orderObj, request, category, timestamp);
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

SymbolInfo BybitDealService::getSymbolInfo(const std::string &symbol, const std::string &category)
{
    if (symbol.empty())
    {
        throw runtime_error("Symbol cannot be empty");
    }

    string effectiveCategory = category.empty() ? "spot" : category;

    string target = "/v5/market/instruments-info?category=" + effectiveCategory + "&symbol=" + symbol;
    HttpRequestContext context(ioc, ctx, host, target);
    context.prepareRequest(http::verb::get);

    string response = httpsPost(context);

    boost::system::error_code ec;
    json::value jsonValue = json::parse(response, ec);
    if (ec)
    {
        throw runtime_error("Bybit getSymbolInfo: JSON parse error: " + ec.message());
    }
    if (!jsonValue.is_object())
    {
        throw runtime_error("Response is not a JSON object");
    }

    json::object &root = jsonValue.as_object();

    int retCode = -1;
    if (root.contains("retCode") && root.at("retCode").is_number())
    {
        retCode = root.at("retCode").as_int64();
    }

    if (retCode != 0)
    {
        string msg = "Unknown Error";
        if (root.contains("retMsg") && root.at("retMsg").is_string())
        {
            msg = root.at("retMsg").as_string().c_str();
        }
        throw runtime_error("Bybit Error " + to_string(retCode) + ": " + msg);
    }

    if (!root.contains("result") || !root.at("result").is_object())
    {
        throw runtime_error("Missing result object");
    }

    json::object &result = root.at("result").as_object();

    if (!result.contains("list") || !result.at("list").is_array())
    {
        throw runtime_error("Missing list in result");
    }

    json::array &list = result.at("list").as_array();
    if (list.empty())
    {
        throw runtime_error("Symbol not found: " + symbol);
    }

    if (!list[0].is_object())
    {
        throw runtime_error("Invalid instrument object");
    }

    return createSymbolInfo(list[0].as_object(), symbol);
}

OrderInfo BybitDealService::createDetailedOrderInfo(const json::object &orderObj,
                                                    const OrderQuery &request,
                                                    const std::string &category,
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

SymbolInfo BybitDealService::createSymbolInfo(const json::object &instrument, const std::string &symbol)
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

OcoInfo BybitDealService::placeOco(const PlaceOcoRequest& request)
{
    throw runtime_error("Bybit placeOco: not supported via API (emulate: place two orders and cancel the other on fill via websocket)");
}

OcoInfo BybitDealService::cancelOco(const OrderListQuery& request)
{
    throw runtime_error("Bybit cancelOco: not supported via API");
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