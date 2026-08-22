#include "ApplicationConfigurationUtil.hpp"
#include "binance/BinanceDealService.hpp"
#include "bybit/BybitDealService.hpp"
#include "graphical/GraphicalUserInterface.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QString>

#include <exception>
#include <iostream>
#include <memory>
#include <string>

using namespace std;

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);

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

        GraphicalUserInterface userInterface(binanceDealService, bybitDealService);
        return userInterface.run();
    }
    catch (const std::exception &exception)
    {
        const string errorMessage = "Application startup failed: " + string(exception.what());
        cerr << errorMessage << endl;
        QMessageBox::critical(nullptr, "CryptoDeal startup failed", QString::fromStdString(errorMessage));
        return 1;
    }
}
