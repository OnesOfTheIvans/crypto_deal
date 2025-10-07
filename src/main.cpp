#include "DealService.hpp"

//Boost.PropertyTree
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include <iostream>
#include <string>

std::string host;
std::string apiKey;
std::string secretKey;

void initConfigVariables() {
    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(CONFIG_FILE, pt);
    host = pt.get<std::string>("API.BINANCE_HOST");
    apiKey = pt.get<std::string>("API.BINANCE_API_KEY");
    secretKey = pt.get<std::string>("API.BINANCE_SECRET_KEY");
}

int main() {
    initConfigVariables();
    DealService dealService(host, apiKey, secretKey);

    std::cout << "-------------------------------------------------------\n" << "BUY\n"
        << "-------------------------------------------------------" << std::endl;

    std::string baseAsset = "USDC";
    std::string quoteAsset = "USDT";
    int quantity = 100;
    bool success = dealService.buyCrypto(baseAsset, quoteAsset, quantity);
    if (success) {
        std::cout << "Order succeed" << std::endl;
    } else {
        std::cout << "Order failed" << std::endl;
    }

    std::cout << "-------------------------------------------------------\n" << "SELL\n"
        << "-------------------------------------------------------" << std::endl;

    baseAsset = "USDC";
    quoteAsset = "USDT";
    quantity = 10;
    success = dealService.sellCrypto(baseAsset, quoteAsset, quantity);
    if (success) {
        std::cout << "Order succeed" << std::endl;
    } else {
        std::cout << "Order failed" << std::endl;
    }

    return 0;
}
