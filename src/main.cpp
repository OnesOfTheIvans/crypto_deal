#include "ApplicationConfigurationUtil.hpp"
#include "OperationChainDefinitionLoader.hpp"
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
#include <utility>

using namespace std;

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);

    try
    {
        auto operationChainDefinitions = OperationChainDefinitionLoader::load(OPERATION_CHAINS_FILE);
        const ApplicationConfiguration configuration = loadApplicationConfiguration(CONFIG_FILE);
        const auto binanceDealService = std::make_shared<BinanceDealService>(configuration.binance.host,
                                                                             configuration.binance.apiKey,
                                                                             configuration.binance.secretKey,
                                                                             configuration.binance.websocketHost);
        const auto bybitDealService = std::make_shared<BybitDealService>(configuration.bybit.host,
                                                                         configuration.bybit.apiKey,
                                                                         configuration.bybit.secretKey,
                                                                         configuration.bybit.websocketHost);

        GraphicalUserInterface userInterface(binanceDealService, bybitDealService, move(operationChainDefinitions));
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
