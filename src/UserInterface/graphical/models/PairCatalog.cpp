#include "PairCatalog.hpp"

#include "DealService.hpp"
#include "common/exception_handling.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"

#include <utility>

using namespace std;
using namespace exception_handling;

PairCatalog::PairCatalog(QObject *parent) : QObject(parent) {}

void PairCatalog::loadCatalogs(AsyncTaskExecutor &taskExecutor,
                               shared_ptr<DealService> binanceDealService,
                               shared_ptr<DealService> bybitDealService)
{
    throwIf(binanceDealService == nullptr, "Pair catalog requires a Binance deal service");
    throwIf(bybitDealService == nullptr, "Pair catalog requires a Bybit deal service");

    loadCatalog(taskExecutor, move(binanceDealService), ExchangerType::BINANCE);
    loadCatalog(taskExecutor, move(bybitDealService), ExchangerType::BYBIT);
}

const vector<TradablePair> &PairCatalog::getPairs(ExchangerType exchangerType) const
{
    return getCatalog(exchangerType).pairs;
}

const UiTaskState &PairCatalog::getLoadState(ExchangerType exchangerType) const
{
    return getCatalog(exchangerType).loadState;
}

PairCatalog::CatalogEntry &PairCatalog::getCatalog(ExchangerType exchangerType)
{
    throwIf(exchangerType != ExchangerType::BINANCE && exchangerType != ExchangerType::BYBIT,
            "Unsupported pair catalog exchange");
    return exchangerType == ExchangerType::BINANCE ? binanceCatalog : bybitCatalog;
}

const PairCatalog::CatalogEntry &PairCatalog::getCatalog(ExchangerType exchangerType) const
{
    throwIf(exchangerType != ExchangerType::BINANCE && exchangerType != ExchangerType::BYBIT,
            "Unsupported pair catalog exchange");
    return exchangerType == ExchangerType::BINANCE ? binanceCatalog : bybitCatalog;
}

void PairCatalog::loadCatalog(AsyncTaskExecutor &taskExecutor,
                              shared_ptr<DealService> dealService,
                              ExchangerType exchangerType)
{
    CatalogEntry &catalog = getCatalog(exchangerType);
    if (catalog.loadStarted)
    {
        return;
    }

    catalog.loadStarted = taskExecutor.startTask(
        catalog.loadState,
        *this,
        [dealService = move(dealService)]() { return dealService->getTradablePairs(); },
        [this, exchangerType](vector<TradablePair> pairs) { storePairs(exchangerType, move(pairs)); });
}

void PairCatalog::storePairs(ExchangerType exchangerType, vector<TradablePair> pairs)
{
    getCatalog(exchangerType).pairs = move(pairs);
    emit pairsChanged(exchangerType);
}
