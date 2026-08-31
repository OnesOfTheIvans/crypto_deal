#ifndef BALANCE_CATALOG_H
#define BALANCE_CATALOG_H

#include "ExchangerType.hpp"
#include "common/domain/AssetBalance.hpp"
#include "graphical/async/UiTaskState.hpp"

#include <QObject>

#include <boost/container/flat_map.hpp>

#include <memory>
#include <string>

class AsyncTaskExecutor;
class DealService;

class BalanceCatalog final : public QObject
{
    Q_OBJECT

  public:
    using BalanceSnapshot = boost::container::flat_map<std::string, AssetBalance>;

  private:
    struct BalanceEntry
    {
        BalanceSnapshot balances;
        UiTaskState loadState;
        bool hasSuccessfulSnapshot = false;
    };

    AsyncTaskExecutor &taskExecutor;
    std::shared_ptr<DealService> binanceDealService;
    std::shared_ptr<DealService> bybitDealService;
    BalanceEntry binanceEntry;
    BalanceEntry bybitEntry;

    BalanceEntry &getEntry(ExchangerType exchangerType);

    const BalanceEntry &getEntry(ExchangerType exchangerType) const;

    std::shared_ptr<DealService> getDealService(ExchangerType exchangerType) const;

    void startLoad(ExchangerType exchangerType);

    void storeBalances(ExchangerType exchangerType, BalanceSnapshot balances);

  public:
    BalanceCatalog(AsyncTaskExecutor &taskExecutor,
                   std::shared_ptr<DealService> binanceDealService,
                   std::shared_ptr<DealService> bybitDealService,
                   QObject *parent = nullptr);

    void loadBalances();

    void retryBalances(ExchangerType exchangerType);

    void refreshBalances(ExchangerType exchangerType);

    const BalanceSnapshot &getBalances(ExchangerType exchangerType) const;

    const UiTaskState &getLoadState(ExchangerType exchangerType) const;

    bool hasSuccessfulSnapshot(ExchangerType exchangerType) const;

  signals:
    void balancesChanged(ExchangerType exchangerType);
};

#endif
