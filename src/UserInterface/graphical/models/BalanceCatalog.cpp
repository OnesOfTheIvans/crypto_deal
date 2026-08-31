#include "BalanceCatalog.hpp"

#include "DealService.hpp"
#include "common/StreamStatus.hpp"
#include "common/UserStreamEventHandlers.hpp"
#include "common/exception_handling.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"

#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <stop_token>
#include <utility>

using namespace exception_handling;
using namespace std;

namespace {
    constexpr array<int, 5> LIVE_RETRY_DELAYS_MS = {1000, 2000, 5000, 10000, 30000};
}

BalanceCatalog::BalanceCatalog(AsyncTaskExecutor &taskExecutor,
                               shared_ptr<DealService> binanceDealService,
                               shared_ptr<DealService> bybitDealService,
                               QObject *parent)
    : QObject(parent), taskExecutor(taskExecutor), binanceDealService(move(binanceDealService)),
      bybitDealService(move(bybitDealService)), liveCallbackState(make_shared<LiveCallbackState>()),
      liveUpdatesRequested(make_shared<atomic<bool>>(false))
{
    throwIf(this->binanceDealService == nullptr, "Balance catalog requires a Binance deal service");
    throwIf(this->bybitDealService == nullptr, "Balance catalog requires a Bybit deal service");

    liveCallbackState->catalog = this;
    createLiveRetryTimer(ExchangerType::BINANCE);
    createLiveRetryTimer(ExchangerType::BYBIT);
}

BalanceCatalog::~BalanceCatalog()
{
    stopLiveUpdates();
    lock_guard<mutex> lock(liveCallbackState->mutex);
    liveCallbackState->catalog = nullptr;
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

void BalanceCatalog::startLiveUpdates()
{
    if (liveUpdatesRequested->exchange(true))
    {
        return;
    }

    attachUserStreamEventHandlers(ExchangerType::BINANCE);
    attachUserStreamEventHandlers(ExchangerType::BYBIT);
    startLiveUpdate(ExchangerType::BINANCE);
    startLiveUpdate(ExchangerType::BYBIT);
}

void BalanceCatalog::stopLiveUpdates()
{
    if (!liveUpdatesRequested->exchange(false))
    {
        return;
    }

    binanceEntry.liveRetryTimer->stop();
    bybitEntry.liveRetryTimer->stop();
    binanceDealService->clearUserStreamEventHandlers();
    bybitDealService->clearUserStreamEventHandlers();
    binanceDealService->stopUserStream();
    bybitDealService->stopUserStream();
    setLiveUpdateStatus(ExchangerType::BINANCE, LiveUpdateStatus::STOPPED);
    setLiveUpdateStatus(ExchangerType::BYBIT, LiveUpdateStatus::STOPPED);
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

BalanceCatalog::LiveUpdateStatus BalanceCatalog::getLiveUpdateStatus(ExchangerType exchangerType) const
{
    return getEntry(exchangerType).liveUpdateStatus;
}

const QString &BalanceCatalog::getLiveUpdateError(ExchangerType exchangerType) const
{
    return getEntry(exchangerType).liveUpdateError;
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
        [this, exchangerType](BalanceSnapshot)
        { storeBalances(exchangerType, getDealService(exchangerType)->getBalances()); });
}

void BalanceCatalog::storeBalances(ExchangerType exchangerType, BalanceSnapshot balances)
{
    BalanceEntry &entry = getEntry(exchangerType);
    entry.balances = move(balances);
    entry.hasSuccessfulSnapshot = true;
    emit balancesChanged(exchangerType);
}

void BalanceCatalog::createLiveRetryTimer(ExchangerType exchangerType)
{
    BalanceEntry &entry = getEntry(exchangerType);
    entry.liveRetryTimer = new QTimer(this);
    entry.liveRetryTimer->setSingleShot(true);
    connect(entry.liveRetryTimer, &QTimer::timeout, this, [this, exchangerType]() { startLiveUpdate(exchangerType); });
}

void BalanceCatalog::attachUserStreamEventHandlers(ExchangerType exchangerType)
{
    const shared_ptr<LiveCallbackState> callbackState = liveCallbackState;
    getDealService(exchangerType)
        ->setUserStreamEventHandlers(
            {[callbackState, exchangerType]()
             {
                 lock_guard<mutex> lock(callbackState->mutex);
                 BalanceCatalog *catalog = callbackState->catalog;
                 if (catalog != nullptr)
                 {
                     QMetaObject::invokeMethod(
                         catalog,
                         [catalog, exchangerType]() { catalog->handleBalanceCacheChanged(exchangerType); },
                         Qt::QueuedConnection);
                 }
             },
             [callbackState, exchangerType]()
             {
                 lock_guard<mutex> lock(callbackState->mutex);
                 BalanceCatalog *catalog = callbackState->catalog;
                 if (catalog != nullptr)
                 {
                     QMetaObject::invokeMethod(
                         catalog,
                         [catalog, exchangerType]() { catalog->handleUserStreamStatusChanged(exchangerType); },
                         Qt::QueuedConnection);
                 }
             }});
}

void BalanceCatalog::startLiveUpdate(ExchangerType exchangerType)
{
    if (!liveUpdatesRequested->load())
    {
        return;
    }

    BalanceEntry &entry = getEntry(exchangerType);
    if (entry.liveStartState.isLoading())
    {
        return;
    }

    entry.liveRetryTimer->stop();
    setLiveUpdateStatus(exchangerType, LiveUpdateStatus::STARTING, entry.liveUpdateError);
    shared_ptr<DealService> dealService = getDealService(exchangerType);
    const shared_ptr<atomic<bool>> requested = liveUpdatesRequested;
    taskExecutor.startTask(
        entry.liveStartState,
        *this,
        [dealService, requested](stop_token stopToken)
        {
            if (stopToken.stop_requested() || !requested->load())
            {
                return false;
            }

            dealService->startUserStream();
            if (stopToken.stop_requested() || !requested->load())
            {
                dealService->stopUserStream();
                return false;
            }
            return true;
        },
        [this, exchangerType](bool started)
        {
            if (started && liveUpdatesRequested->load())
            {
                handleUserStreamStatusChanged(exchangerType);
            }
        },
        [this, exchangerType](const QString &error) { handleLiveStartFailure(exchangerType, error); });
}

void BalanceCatalog::handleLiveStartFailure(ExchangerType exchangerType, const QString &error)
{
    if (liveUpdatesRequested->load())
    {
        scheduleLiveRetry(exchangerType, error);
    }
}

void BalanceCatalog::handleBalanceCacheChanged(ExchangerType exchangerType)
{
    if (!liveUpdatesRequested->load() || !hasSuccessfulSnapshot(exchangerType))
    {
        return;
    }

    storeBalances(exchangerType, getDealService(exchangerType)->getBalances());
}

void BalanceCatalog::handleUserStreamStatusChanged(ExchangerType exchangerType)
{
    if (!liveUpdatesRequested->load())
    {
        return;
    }

    BalanceEntry &entry = getEntry(exchangerType);
    const shared_ptr<DealService> dealService = getDealService(exchangerType);
    switch (dealService->getUserStreamStatus())
    {
    case StreamStatus::CONNECTING:
        entry.liveRetryTimer->stop();
        setLiveUpdateStatus(exchangerType, LiveUpdateStatus::CONNECTING);
        break;
    case StreamStatus::CONNECTED:
        entry.liveRetryTimer->stop();
        entry.liveRetryAttempt = 0;
        setLiveUpdateStatus(exchangerType, LiveUpdateStatus::CONNECTED);
        break;
    case StreamStatus::ERROR:
        scheduleLiveRetry(exchangerType, QString::fromStdString(dealService->getUserStreamLastError()));
        break;
    case StreamStatus::STOPPED:
        scheduleLiveRetry(exchangerType, QString::fromStdString(dealService->getUserStreamLastError()));
        break;
    }
}

void BalanceCatalog::scheduleLiveRetry(ExchangerType exchangerType, const QString &error)
{
    if (!liveUpdatesRequested->load())
    {
        return;
    }

    BalanceEntry &entry = getEntry(exchangerType);
    if (!entry.liveRetryTimer->isActive())
    {
        const size_t delayIndex = min(entry.liveRetryAttempt, LIVE_RETRY_DELAYS_MS.size() - 1);
        entry.liveRetryTimer->start(LIVE_RETRY_DELAYS_MS[delayIndex]);
        ++entry.liveRetryAttempt;
    }
    setLiveUpdateStatus(exchangerType, LiveUpdateStatus::RETRY_WAITING, error);
}

void BalanceCatalog::setLiveUpdateStatus(ExchangerType exchangerType, LiveUpdateStatus status, const QString &error)
{
    BalanceEntry &entry = getEntry(exchangerType);
    if (entry.liveUpdateStatus == status && entry.liveUpdateError == error)
    {
        return;
    }

    entry.liveUpdateStatus = status;
    entry.liveUpdateError = error;
    emit liveUpdateStatusChanged(exchangerType);
}
