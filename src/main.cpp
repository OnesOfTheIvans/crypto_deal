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

int main()
{
    try
    {
        initConfigVariables();
    }
    catch (const exception &e)
    {
        cerr << "Unhandled exception in main: " << e.what() << endl;
        return 1;
    }

    return 0;
}
