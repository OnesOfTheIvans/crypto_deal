#include "GraphicalUserInterface.hpp"

#include "CryptoDealWindow.hpp"
#include "async/AsyncTaskExecutor.hpp"
#include "common/exception_handling.hpp"

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
    CryptoDealWindow mainWindow;
    mainWindow.show();

    const int exitCode = QApplication::exec();
    taskExecutor.stopAndWait();
    return exitCode;
}
