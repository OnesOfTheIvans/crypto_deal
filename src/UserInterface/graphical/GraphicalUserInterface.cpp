#include "GraphicalUserInterface.hpp"

#include "CryptoDealWindow.hpp"
#include "DealService.hpp"
#include "OperationChainRunManager.hpp"
#include "async/AsyncTaskExecutor.hpp"
#include "common/exception_handling.hpp"
#include "models/BalanceCatalog.hpp"
#include "models/OperationChainRunModel.hpp"
#include "models/OrderSessionModel.hpp"
#include "models/PairCatalog.hpp"
#include "models/SymbolInfoCatalog.hpp"

#include <QApplication>

#include <utility>

using namespace std;
using namespace exception_handling;

GraphicalUserInterface::GraphicalUserInterface(shared_ptr<DealService> binanceDealService,
                                               shared_ptr<DealService> bybitDealService,
                                               vector<OperationChainDefinition> operationChainDefinitions)
    : binanceDealService(move(binanceDealService)), bybitDealService(move(bybitDealService)),
      operationChainDefinitions(move(operationChainDefinitions))
{
    throwIf(this->binanceDealService == nullptr, "Graphical user interface requires a Binance deal service");
    throwIf(this->bybitDealService == nullptr, "Graphical user interface requires a Bybit deal service");
}

int GraphicalUserInterface::run()
{
    throwIf(QApplication::instance() == nullptr, "Graphical user interface requires a Qt application");

    AsyncTaskExecutor taskExecutor;
    PairCatalog pairCatalog;
    BalanceCatalog balanceCatalog(taskExecutor, binanceDealService, bybitDealService);
    SymbolInfoCatalog symbolInfoCatalog(taskExecutor, binanceDealService, bybitDealService);
    OrderSessionModel orderSessionModel(taskExecutor, binanceDealService, bybitDealService);
    OperationChainRunManager chainRunManager(operationChainDefinitions, binanceDealService, bybitDealService);
    OperationChainRunModel chainRunModel(chainRunManager);
    CryptoDealWindow mainWindow(pairCatalog, balanceCatalog, symbolInfoCatalog, orderSessionModel, chainRunModel);
    mainWindow.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceDealService, bybitDealService);
    balanceCatalog.loadBalances();
    balanceCatalog.startLiveUpdates();

    const int exitCode = QApplication::exec();
    chainRunManager.requestStop();
    taskExecutor.requestStop();
    balanceCatalog.stopLiveUpdates();
    chainRunManager.stopAndWait();
    taskExecutor.stopAndWait();
    return exitCode;
}
