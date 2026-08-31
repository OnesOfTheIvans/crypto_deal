#include "GraphicalUserInterface.hpp"

#include "CryptoDealWindow.hpp"
#include "DealService.hpp"
#include "async/AsyncTaskExecutor.hpp"
#include "common/exception_handling.hpp"
#include "models/BalanceCatalog.hpp"
#include "models/OrderSessionModel.hpp"
#include "models/PairCatalog.hpp"
#include "models/SymbolInfoCatalog.hpp"

#include <QApplication>

#include <utility>

using namespace std;
using namespace exception_handling;

GraphicalUserInterface::GraphicalUserInterface(shared_ptr<DealService> binanceDealService,
                                               shared_ptr<DealService> bybitDealService)
    : binanceDealService(move(binanceDealService)), bybitDealService(move(bybitDealService))
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
    CryptoDealWindow mainWindow(pairCatalog, balanceCatalog, symbolInfoCatalog, orderSessionModel);
    mainWindow.show();
    pairCatalog.loadCatalogs(taskExecutor, binanceDealService, bybitDealService);
    balanceCatalog.loadBalances();

    const int exitCode = QApplication::exec();
    taskExecutor.requestStop();
    binanceDealService->stopUserStream();
    bybitDealService->stopUserStream();
    taskExecutor.stopAndWait();
    return exitCode;
}
