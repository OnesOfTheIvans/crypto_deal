#include "ApplicationConfigurationUtil.hpp"
#include "binance/BinanceDealService.hpp"
#include "bybit/BybitDealService.hpp"

#include <exception>
#include <iostream>
#include <memory>

int main()
{
    try
    {
        const ApplicationConfiguration configuration = loadApplicationConfiguration(CONFIG_FILE);
        const auto binanceDealService = std::make_shared<BinanceDealService>(configuration.binance.host,
                                                                             configuration.binance.apiKey,
                                                                             configuration.binance.secretKey,
                                                                             configuration.binance.websocketHost);
        const auto bybitDealService = std::make_shared<BybitDealService>(configuration.bybit.host,
                                                                         configuration.bybit.apiKey,
                                                                         configuration.bybit.secretKey,
                                                                         configuration.bybit.websocketHost);
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Application startup failed: " << exception.what() << std::endl;
        return 1;
    }

    return 0;
}
