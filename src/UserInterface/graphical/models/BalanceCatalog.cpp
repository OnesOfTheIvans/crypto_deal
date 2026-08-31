#include "BalanceCatalog.hpp"

#include "DealService.hpp"
#include "common/exception_handling.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"

#include <utility>

using namespace exception_handling;
using namespace std;

BalanceCatalog::BalanceCatalog(AsyncTaskExecutor &taskExecutor,
                               shared_ptr<DealService> binanceDealService,
                               shared_ptr<DealService> bybitDealService,
                               QObject *parent)
    : QObject(parent), taskExecutor(taskExecutor), binanceDealService(move(binanceDealService)),
      bybitDealService(move(bybitDealService))
{
    throwIf(this->binanceDealService == nullptr, "Balance catalog requires a Binance deal service");
    throwIf(this->bybitDealService == nullptr, "Balance catalog requires a Bybit deal service");
}

void BalanceCatalog::loadBalances()
{
    if (binanceEntry.loadState.getStatus() == UiTaskState::Status::IDLE)
    {
        startLoad(ExchangerType::BINANCE);
    }
    if (bybitEntry.loadState.getStatus() == UiTaskState::Status::IDLE)
    {
        startLoad(ExchangerType::BYBIT);
    }
}

void BalanceCatalog::retryBalances(ExchangerType exchangerType)
{
    if (getEntry(exchangerType).loadState.getStatus() == UiTaskState::Status::FAILED)
    {
        startLoad(exchangerType);
    }
}

void BalanceCatalog::refreshBalances(ExchangerType exchangerType)
{
    if (!getEntry(exchangerType).loadState.isLoading())
    {
        startLoad(exchangerType);
    }
}

const BalanceCatalog::BalanceSnapshot &BalanceCatalog::getBalances(ExchangerType exchangerType) const
{
    return getEntry(exchangerType).balances;
}

const UiTaskState &BalanceCatalog::getLoadState(ExchangerType exchangerType) const
{
    return getEntry(exchangerType).loadState;
}

bool BalanceCatalog::hasSuccessfulSnapshot(ExchangerType exchangerType) const
{
    return getEntry(exchangerType).hasSuccessfulSnapshot;
}

BalanceCatalog::BalanceEntry &BalanceCatalog::getEntry(ExchangerType exchangerType)
{
    throwIf(exchangerType != ExchangerType::BINANCE && exchangerType != ExchangerType::BYBIT,
            "Unsupported balance catalog exchange");
    return exchangerType == ExchangerType::BINANCE ? binanceEntry : bybitEntry;
}

const BalanceCatalog::BalanceEntry &BalanceCatalog::getEntry(ExchangerType exchangerType) const
{
    throwIf(exchangerType != ExchangerType::BINANCE && exchangerType != ExchangerType::BYBIT,
            "Unsupported balance catalog exchange");
    return exchangerType == ExchangerType::BINANCE ? binanceEntry : bybitEntry;
}

shared_ptr<DealService> BalanceCatalog::getDealService(ExchangerType exchangerType) const
{
    throwIf(exchangerType != ExchangerType::BINANCE && exchangerType != ExchangerType::BYBIT,
            "Unsupported balance catalog exchange");
    return exchangerType == ExchangerType::BINANCE ? binanceDealService : bybitDealService;
}

void BalanceCatalog::startLoad(ExchangerType exchangerType)
{
    BalanceEntry &entry = getEntry(exchangerType);
    shared_ptr<DealService> dealService = getDealService(exchangerType);
    taskExecutor.startTask(
        entry.loadState,
        *this,
        [dealService = move(dealService)]() { return dealService->getBalancesRest(); },
        [this, exchangerType](BalanceSnapshot balances) { storeBalances(exchangerType, move(balances)); });
}

void BalanceCatalog::storeBalances(ExchangerType exchangerType, BalanceSnapshot balances)
{
    BalanceEntry &entry = getEntry(exchangerType);
    entry.balances = move(balances);
    entry.hasSuccessfulSnapshot = true;
    emit balancesChanged(exchangerType);
}
