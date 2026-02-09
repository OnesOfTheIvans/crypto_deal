#include "DealService.hpp"
#include "binance/BinanceDealService.hpp"
#include "bybit/BybitDealService.hpp"

// Boost.PropertyTree
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <cctype>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace std;

string binanceHost;
string binanceApiKey;
string binanceSecretKey;
string binanceWebsocketHost;
string bybitHost;
string bybitApiKey;
string bybitSecretKey;
string bybitWebsocketHost;

void initConfigVariables()
{
    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(CONFIG_FILE, pt);

    binanceHost = pt.get<string>("API.BINANCE_HOST");
    binanceApiKey = pt.get<string>("API.BINANCE_API_KEY");
    binanceSecretKey = pt.get<string>("API.BINANCE_SECRET_KEY");
    binanceWebsocketHost = pt.get<string>("API.BINANCE_WEBSOCKET_HOST");

    bybitHost = pt.get<string>("API.BYBIT_HOST");
    bybitApiKey = pt.get<string>("API.BYBIT_API_KEY");
    bybitSecretKey = pt.get<string>("API.BYBIT_SECRET_KEY");
    bybitWebsocketHost = pt.get<string>("API.BYBIT_WEBSOCKET_HOST");
}

static void printTitle(const string &title)
{
    cout << "-------------------------------------------------------\n"
         << title << "\n"
         << "-------------------------------------------------------\n";
}

static void printBalances(DealService &dealService, const string &assetToPrint)
{
    auto balances = dealService.getBalances();
    cout << "Balances cache size: " << balances.size() << "\n";

    auto b = dealService.getBalance(assetToPrint);
    if (b.has_value())
    {
        cout << "Balance " << assetToPrint << ": free=" << b->free << ", locked=" << b->locked << "\n";
    }
    else
    {
        cout << "Balance " << assetToPrint << ": <not in cache>\n";
    }
}

static string normalizeOrderStatus(const string &status)
{
    string normalized;
    normalized.reserve(status.size());

    for (unsigned char character : status)
    {
        if (std::isalnum(character))
        {
            normalized.push_back(static_cast<char>(std::tolower(character)));
        }
    }

    return normalized;
}

static double getRemainingQuantity(const OrderInfo &orderInfo)
{
    if (orderInfo.leavesQty > 0.0)
    {
        return orderInfo.leavesQty;
    }

    const double remainingQuantity = orderInfo.origQty - orderInfo.executedQty;
    return (remainingQuantity > 0.0) ? remainingQuantity : 0.0;
}

static bool isOrderOpenForCancel(const OrderInfo &orderInfo)
{
    const string normalizedStatus = normalizeOrderStatus(orderInfo.status);
    const bool statusAllowsCancel =
        (normalizedStatus == "new") || (normalizedStatus == "partiallyfilled") || (normalizedStatus == "untriggered");

    return statusAllowsCancel && (getRemainingQuantity(orderInfo) > 0.0);
}

static void cancelAllBeforeTests(DealService &dealService, const std::string &symbol, const std::string &category)
{
    printTitle("CANCEL ALL OPEN ORDERS (PRE-TEST)");
    try
    {
        dealService.cancelAllOpenOrders(symbol, category);
        std::cout << "cancelAllOpenOrders ok\n";
    }
    catch (const std::exception &e)
    {
        std::cout << "cancelAllOpenOrders failed (continuing): " << e.what() << "\n";
    }
}

static void testSymbolInfo(DealService &dealService, const string &symbol)
{
    printTitle("SYMBOL INFO");
    try
    {
        SymbolInfo info = dealService.getSymbolInfo(symbol, "spot");
        cout << "SymbolInfo: symbol=" << info.symbol << " base=" << info.baseAsset << " quote=" << info.quoteAsset
             << " tickSize=" << info.tickSize << " stepSize=" << info.stepSize << " minQty=" << info.minQty << "\n";
    }
    catch (const exception &e)
    {
        cout << "getSymbolInfo failed: " << e.what() << "\n";
    }
}

static void testPlaceGetCancelOrder(DealService &dealService, const string &symbol)
{
    printTitle("PLACE / GET / CANCEL ORDER");

    PlaceOrderRequest placeOrderRequest;
    placeOrderRequest.symbol = symbol;
    placeOrderRequest.side = "BUY";
    placeOrderRequest.type = "LIMIT";
    placeOrderRequest.quantity = 0.0002;
    placeOrderRequest.price = 65000.0;
    placeOrderRequest.timeInForce = string("GTC");
    placeOrderRequest.clientOrderId = string("TEST_ORDER_1_") + to_string(time(nullptr));
    placeOrderRequest.category = "spot";

    try
    {
        OrderInfo placedOrder = dealService.placeOrder(placeOrderRequest);
        cout << "placeOrder ok: orderId=" << placedOrder.orderId << " clientOrderId=" << placedOrder.clientOrderId
             << " status=" << placedOrder.status << "\n";

        OrderQuery orderQuery;
        orderQuery.symbol = symbol;
        orderQuery.category = "spot";

        if (!placedOrder.orderId.empty())
        {
            orderQuery.orderId = placedOrder.orderId;
        }
        else if (!placedOrder.clientOrderId.empty())
        {
            orderQuery.clientOrderId = placedOrder.clientOrderId;
        }

        bool shouldCancelOrder = true;

        try
        {
            OrderInfo retrievedOrder = dealService.getOrder(orderQuery);
            cout << "getOrder ok: status=" << retrievedOrder.status << " executedQty=" << retrievedOrder.executedQty
                 << " leavesQty=" << retrievedOrder.leavesQty << "\n";

            shouldCancelOrder = isOrderOpenForCancel(retrievedOrder);
            if (!shouldCancelOrder)
            {
                cout << "cancelOrder skipped: status=" << retrievedOrder.status << "\n";
            }
        }
        catch (const exception &e)
        {
            cout << "getOrder failed: " << e.what() << "\n";
        }

        if (shouldCancelOrder)
        {
            try
            {
                OrderInfo cancelledOrder = dealService.cancelOrder(orderQuery);
                cout << "cancelOrder ok: status=" << cancelledOrder.status
                     << " executedQty=" << cancelledOrder.executedQty << " leavesQty=" << cancelledOrder.leavesQty
                     << "\n";
            }
            catch (const exception &e)
            {
                cout << "cancelOrder failed: " << e.what() << "\n";
            }
        }
    }
    catch (const exception &e)
    {
        cout << "placeOrder failed: " << e.what() << "\n";
    }
}

static void testPlaceCancelOco(DealService &dealService, const string &symbol)
{
    printTitle("PLACE / CANCEL OCO");
    PlaceOcoRequest ocoRequest;
    ocoRequest.symbol = symbol;
    ocoRequest.side = "SELL";
    ocoRequest.quantity = 0.0001;

    ocoRequest.price = 200000.0;
    ocoRequest.stopPrice = 150000.0;

    ocoRequest.listClientOrderId = string("TEST_OCO_GROUP_1_") + to_string(time(nullptr));

    try
    {
        OcoInfo placed = dealService.placeOco(ocoRequest);
        cout << "placeOco ok: orderListId=" << placed.orderListId << " orders=" << placed.orders.size() << "\n";

        OrderListQuery orderListQuery;
        orderListQuery.symbol = symbol;
        orderListQuery.category = "spot";
        orderListQuery.listClientOrderId = placed.listClientOrderId;

        try
        {
            OcoInfo cancelled = dealService.cancelOco(orderListQuery);
            cout << "cancelOco ok: orders=" << cancelled.orders.size() << "\n";
        }
        catch (const exception &e)
        {
            cout << "cancelOco failed: " << e.what() << "\n";
        }
    }
    catch (const exception &e)
    {
        cout << "placeOco failed: " << e.what() << "\n";
    }
}

static double roundUpToStep(double value, double stepSize)
{
    if (stepSize == 0.0)
    {
        return value;
    }
    return ceil(value / stepSize) * stepSize;
}

static void testSimpleBuySellOperations(DealService &dealService)
{
    printTitle("BUY");

    const std::string baseAsset = "BTC";
    const std::string quoteAsset = "USDT";
    const std::string symbol = baseAsset + quoteAsset;

    // Use only SymbolInfo fields (no getLastPrice(), no extra API assumptions).
    // Conservative fallback price for local minNotional -> qty conversion.
    constexpr double kFallbackPrice = 65000.0;

    double quantity = 0.0002; // default fallback

    try
    {
        const SymbolInfo info = dealService.getSymbolInfo(symbol, "spot");

        const double minNotional = (info.minNotional > 0.0) ? info.minNotional : 5.0;
        const double minQty = (info.minQty > 0.0) ? info.minQty : 0.00001;

        // Ensure minNotional satisfied using fallback price + safety margin.
        const double requiredQtyByNotional = (minNotional * 1.10) / kFallbackPrice;

        double targetQty = std::max(minQty, requiredQtyByNotional);

        // Round UP to step size to avoid "LOT_SIZE" rejections due to precision.
        if (info.stepSize > 0.0)
        {
            quantity = roundUpToStep(targetQty, info.stepSize);
        }
        else
        {
            quantity = targetQty;
        }

        // Hard clamp (defensive) to exchange limits if provided.
        if (info.maxQty > 0.0 && quantity > info.maxQty)
        {
            quantity = info.maxQty;
        }

        // If rounding pushed below minQty somehow, bump again.
        if (info.minQty > 0.0 && quantity < info.minQty)
        {
            targetQty = info.minQty;
            quantity = (info.stepSize > 0.0) ? roundUpToStep(targetQty, info.stepSize) : targetQty;
        }

        std::cout << "Calculated safe Market Qty: " << quantity << " (minNotional=" << minNotional
                  << ", fallbackPrice=" << kFallbackPrice << ", step=" << info.stepSize << ", minQty=" << minQty
                  << ")\n";
    }
    catch (const std::exception &e)
    {
        std::cout << "Warning: getSymbolInfo failed; using default qty=" << quantity << ". Reason: " << e.what()
                  << "\n";
    }

    try
    {
        const bool ok = dealService.buyCrypto(baseAsset, quoteAsset, quantity);
        std::cout << (ok ? "Order succeed\n" : "Order failed\n");
    }
    catch (const std::exception &e)
    {
        std::cout << "buyCrypto exception: " << e.what() << "\n";
    }

    printTitle("SELL");

    try
    {
        const bool ok = dealService.sellCrypto(baseAsset, quoteAsset, quantity);
        if (ok)
        {
            std::cout << "Order succeed\n";
        }
        else
        {
            std::cout << "Order failed (Expected if insufficient balance)\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cout << "sellCrypto exception: " << e.what() << "\n";
    }
}

static void waitForStreamConnection(DealService &dealService)
{
    cout << "Waiting for user stream connection...\n";
    for (int i = 0; i < 50; ++i)
    {
        if (dealService.getUserStreamStatus() == DealService::StreamStatus::CONNECTED)
        {
            cout << "Stream connected.\n";
            return;
        }
        if (dealService.getUserStreamStatus() == DealService::StreamStatus::ERROR)
        {
            cout << "Stream connection error: " << dealService.getUserStreamLastError() << "\n";
            return;
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    cout << "Stream failed to connect within timeout.\n";
}

static void waitForBalances(DealService &dealService)
{
    cout << "Waiting for balance updates...\n";
    for (int i = 0; i < 300; ++i) // 30 seconds
    {
        if (dealService.getBalances().size() > 0)
        {
            return;
        }
        if (dealService.getUserStreamStatus() != DealService::StreamStatus::CONNECTED)
        {
            cout << "Stream disconnected while waiting for balances.\n";
            return;
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    cout << "No balance updates received within timeout.\n";
}

static void testAllOperations(DealService &dealService, const string &title)
{
    cout << "<------------------------------------------------------\n"
         << title << "\n------------------------------------------------------>\n";

    const string symbol = "BTCUSDT";
    const string category = "spot";

    try
    {
        cancelAllBeforeTests(dealService, symbol, category);
        cout << "cancelAllOpenOrders ok\n";
    }
    catch (const std::exception &e)
    {
        cout << "cancelAllOpenOrders failed: " << e.what() << "\n";
    }

    cout << "Starting user stream...\n";
    dealService.startUserStream();
    waitForStreamConnection(dealService);

    testSimpleBuySellOperations(dealService);

    waitForBalances(dealService);

    printTitle("BALANCES");
    printBalances(dealService, "USDT");
    printBalances(dealService, "BTC");

    dealService.stopUserStream();
    cout << "User stream stopped.\n";

    testSymbolInfo(dealService, symbol);
    testPlaceGetCancelOrder(dealService, symbol);
    // testPlaceCancelOco removed as per plan.
}

int main()
{
    try
    {
        initConfigVariables();

        cout << "Initializing Binance Service...\n";
        unique_ptr<DealService> binanceService =
            make_unique<BinanceDealService>(binanceHost, binanceApiKey, binanceSecretKey, binanceWebsocketHost);
        testAllOperations(*binanceService, "BINANCE");

        cout << "Initializing Bybit Service...\n";
        unique_ptr<DealService> bybitService =
            make_unique<BybitDealService>(bybitHost, bybitApiKey, bybitSecretKey, bybitWebsocketHost);
        testAllOperations(*bybitService, "BYBIT");
    }
    catch (const exception &e)
    {
        cerr << "Unhandled exception in main: " << e.what() << endl;
        return 1;
    }

    return 0;
}
