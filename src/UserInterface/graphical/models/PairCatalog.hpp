#ifndef PAIR_CATALOG_H
#define PAIR_CATALOG_H

#include "ExchangerType.hpp"
#include "common/domain/TradablePair.hpp"
#include "graphical/async/UiTaskState.hpp"

#include <QObject>

#include <memory>
#include <vector>

class AsyncTaskExecutor;
class DealService;

class PairCatalog final : public QObject
{
    Q_OBJECT

  private:
    struct CatalogEntry
    {
        std::vector<TradablePair> pairs;
        UiTaskState loadState;
        bool loadStarted = false;
    };

    CatalogEntry binanceCatalog;
    CatalogEntry bybitCatalog;

    CatalogEntry &getCatalog(ExchangerType exchangerType);

    const CatalogEntry &getCatalog(ExchangerType exchangerType) const;

    void
    loadCatalog(AsyncTaskExecutor &taskExecutor, std::shared_ptr<DealService> dealService, ExchangerType exchangerType);

    void storePairs(ExchangerType exchangerType, std::vector<TradablePair> pairs);

  public:
    explicit PairCatalog(QObject *parent = nullptr);

    void loadCatalogs(AsyncTaskExecutor &taskExecutor,
                      std::shared_ptr<DealService> binanceDealService,
                      std::shared_ptr<DealService> bybitDealService);

    const std::vector<TradablePair> &getPairs(ExchangerType exchangerType) const;

    const UiTaskState &getLoadState(ExchangerType exchangerType) const;

  signals:
    void pairsChanged(ExchangerType exchangerType);
};

#endif
