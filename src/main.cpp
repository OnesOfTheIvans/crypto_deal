#include "DealService.hpp"
#include "binance/BinanceDealService.hpp"
#include "bybit/BybitDealService.hpp"

// Boost.PropertyTree
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <iostream>
#include <string>

std::string binanceHost;
std::string binanceApiKey;
std::string binanceSecretKey;
std::string binanceWebsocketHost;
std::string bybitHost;
std::string bybitApiKey;
std::string bybitSecretKey;
std::string bybitWebsocketHost;

void initConfigVariables()
{
    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(CONFIG_FILE, pt);
    binanceHost = pt.get<std::string>("API.BINANCE_HOST");
    binanceApiKey = pt.get<std::string>("API.BINANCE_API_KEY");
    binanceSecretKey = pt.get<std::string>("API.BINANCE_SECRET_KEY");
    binanceWebsocketHost = pt.get<std::string>("API.BINANCE_WEBSOCKET_HOST");
    bybitHost = pt.get<std::string>("API.BYBIT_HOST");
    bybitApiKey = pt.get<std::string>("API.BYBIT_API_KEY");
    bybitSecretKey = pt.get<std::string>("API.BYBIT_SECRET_KEY");
    bybitWebsocketHost = pt.get<std::string>("API.BYBIT_WEBSOCKET_HOST");
}

void testOperations(DealService *dealService, const std::string &title)
{
    std::cout << "<------------------------------------------------------\n"
              << title << "\n------------------------------------------------------>" << std::endl;
    std::cout << "-------------------------------------------------------\n"
              << "BUY\n"
              << "-------------------------------------------------------" << std::endl;

    std::string baseAsset = "USDC";
    std::string quoteAsset = "USDT";
    double quantity = 100;
    bool success = dealService->buyCrypto(baseAsset, quoteAsset, quantity);
    if (success)
    {
        std::cout << "Order succeed" << std::endl;
    }
    else
    {
        std::cout << "Order failed" << std::endl;
    }

    std::cout << "-------------------------------------------------------\n"
              << "SELL\n"
              << "-------------------------------------------------------" << std::endl;

    baseAsset = "USDC";
    quoteAsset = "USDT";
    quantity = 10;
    success = dealService->sellCrypto(baseAsset, quoteAsset, quantity);
    if (success)
    {
        std::cout << "Order succeed" << std::endl;
    }
    else
    {
        std::cout << "Order failed" << std::endl;
    }
}

int main()
{
    initConfigVariables();
    DealService *dealService;
    dealService = new BinanceDealService(binanceHost, binanceApiKey, binanceSecretKey, binanceWebsocketHost);
    testOperations(dealService, "BINANCE");

    delete dealService;

    // Passing empty string for WS host for Bybit as it's not configured yet
    dealService = new BybitDealService(bybitHost, bybitApiKey, bybitSecretKey, bybitWebsocketHost);
    testOperations(dealService, "BYBIT");

    delete dealService;

    return 0;
}
