#ifndef BALANCE_CATALOG_H
#define BALANCE_CATALOG_H

#include "ExchangerType.hpp"
#include "common/domain/AssetBalance.hpp"
#include "graphical/async/UiTaskState.hpp"

#include <QObject>
#include <QString>

#include <boost/container/flat_map.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

class AsyncTaskExecutor;
class DealService;
class QTimer;

class BalanceCatalog final : public QObject
{
    Q_OBJECT

  public:
    using BalanceSnapshot = boost::container::flat_map<std::string, AssetBalance>;

    enum class LiveUpdateStatus
    {
        IDLE,
        STARTING,
        CONNECTING,
        CONNECTED,
        RETRY_WAITING,
        STOPPED
    };
    Q_ENUM(LiveUpdateStatus)

  private:
    struct BalanceEntry
    {
        BalanceSnapshot balances;
        UiTaskState loadState;
        bool hasSuccessfulSnapshot = false;
        UiTaskState liveStartState;
        LiveUpdateStatus liveUpdateStatus = LiveUpdateStatus::IDLE;
        QString liveUpdateError;
        QTimer *liveRetryTimer = nullptr;
        std::size_t liveRetryAttempt = 0;
    };

    struct LiveCallbackState
    {
        std::mutex mutex;
        BalanceCatalog *catalog = nullptr;
    };

    AsyncTaskExecutor &taskExecutor;
    std::shared_ptr<DealService> binanceDealService;
    std::shared_ptr<DealService> bybitDealService;
    BalanceEntry binanceEntry;
    BalanceEntry bybitEntry;
    std::shared_ptr<LiveCallbackState> liveCallbackState;
    std::shared_ptr<std::atomic<bool>> liveUpdatesRequested;

    BalanceEntry &getEntry(ExchangerType exchangerType);

    const BalanceEntry &getEntry(ExchangerType exchangerType) const;

    std::shared_ptr<DealService> getDealService(ExchangerType exchangerType) const;

    void startLoad(ExchangerType exchangerType);

    void storeBalances(ExchangerType exchangerType, BalanceSnapshot balances);

    void createLiveRetryTimer(ExchangerType exchangerType);

    void attachUserStreamEventHandlers(ExchangerType exchangerType);

    void startLiveUpdate(ExchangerType exchangerType);

    void handleLiveStartFailure(ExchangerType exchangerType, const QString &error);

    void handleBalanceCacheChanged(ExchangerType exchangerType);

    void handleUserStreamStatusChanged(ExchangerType exchangerType);

    void scheduleLiveRetry(ExchangerType exchangerType, const QString &error);

    void setLiveUpdateStatus(ExchangerType exchangerType, LiveUpdateStatus status, const QString &error = {});

  public:
    BalanceCatalog(AsyncTaskExecutor &taskExecutor,
                   std::shared_ptr<DealService> binanceDealService,
                   std::shared_ptr<DealService> bybitDealService,
                   QObject *parent = nullptr);

    ~BalanceCatalog() override;

    void loadBalances();

    void startLiveUpdates();

    void stopLiveUpdates();

    void retryBalances(ExchangerType exchangerType);

    void refreshBalances(ExchangerType exchangerType);

    const BalanceSnapshot &getBalances(ExchangerType exchangerType) const;

    const UiTaskState &getLoadState(ExchangerType exchangerType) const;

    bool hasSuccessfulSnapshot(ExchangerType exchangerType) const;

    LiveUpdateStatus getLiveUpdateStatus(ExchangerType exchangerType) const;

    const QString &getLiveUpdateError(ExchangerType exchangerType) const;

  signals:
    void balancesChanged(ExchangerType exchangerType);

    void liveUpdateStatusChanged(ExchangerType exchangerType);
};

#endif
