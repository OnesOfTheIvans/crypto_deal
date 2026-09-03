#ifndef SYMBOL_INFO_CATALOG_H
#define SYMBOL_INFO_CATALOG_H

#include "ExchangerType.hpp"
#include "common/domain/SymbolInfo.hpp"

#include <QObject>
#include <QString>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

class AsyncTaskExecutor;
class DealService;
class UiTaskState;

class SymbolInfoCatalog final : public QObject
{
    Q_OBJECT

  private:
    using SymbolKey = std::pair<ExchangerType, std::string>;

    struct SymbolEntry
    {
        std::optional<SymbolInfo> symbolInfo;
        std::unique_ptr<UiTaskState> loadState;
    };

    AsyncTaskExecutor &taskExecutor;
    std::shared_ptr<DealService> binanceDealService;
    std::shared_ptr<DealService> bybitDealService;
    std::map<SymbolKey, std::unique_ptr<SymbolEntry>> entries;

    SymbolEntry &getOrCreateEntry(ExchangerType exchangerType, const std::string &symbol);

    const SymbolEntry *getEntry(ExchangerType exchangerType, const std::string &symbol) const;

    std::shared_ptr<DealService> getDealService(ExchangerType exchangerType) const;

    void startLoad(ExchangerType exchangerType, const std::string &symbol, SymbolEntry &entry);

    void storeSymbolInfo(ExchangerType exchangerType, const std::string &symbol, SymbolInfo symbolInfo);

  public:
    SymbolInfoCatalog(AsyncTaskExecutor &taskExecutor,
                      std::shared_ptr<DealService> binanceDealService,
                      std::shared_ptr<DealService> bybitDealService,
                      QObject *parent = nullptr);

    ~SymbolInfoCatalog() override;

    void loadSymbolInfo(ExchangerType exchangerType, const std::string &symbol);

    void retrySymbolInfo(ExchangerType exchangerType, const std::string &symbol);

    const SymbolInfo *getSymbolInfo(ExchangerType exchangerType, const std::string &symbol) const;

    const UiTaskState *getLoadState(ExchangerType exchangerType, const std::string &symbol) const;

  signals:
    void symbolInfoChanged(ExchangerType exchangerType, const QString &symbol);
};

#endif
