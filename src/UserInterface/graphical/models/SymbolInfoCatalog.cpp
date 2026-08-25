#include "SymbolInfoCatalog.hpp"

#include "DealService.hpp"
#include "common/exception_handling.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"
#include "graphical/async/UiTaskState.hpp"

#include <utility>

using namespace exception_handling;
using namespace std;

SymbolInfoCatalog::SymbolInfoCatalog(AsyncTaskExecutor &taskExecutor,
                                     shared_ptr<DealService> binanceDealService,
                                     shared_ptr<DealService> bybitDealService,
                                     QObject *parent)
    : QObject(parent), taskExecutor(taskExecutor), binanceDealService(move(binanceDealService)),
      bybitDealService(move(bybitDealService))
{
    throwIf(this->binanceDealService == nullptr, "Symbol info catalog requires a Binance deal service");
    throwIf(this->bybitDealService == nullptr, "Symbol info catalog requires a Bybit deal service");
}

SymbolInfoCatalog::~SymbolInfoCatalog() = default;

void SymbolInfoCatalog::loadSymbolInfo(ExchangerType exchangerType, const string &symbol)
{
    throwIf(symbol.empty(), "Symbol info catalog requires a symbol");

    SymbolEntry &entry = getOrCreateEntry(exchangerType, symbol);
    if (entry.loadState->getStatus() == UiTaskState::Status::IDLE)
    {
        startLoad(exchangerType, symbol, entry);
    }
}

void SymbolInfoCatalog::retrySymbolInfo(ExchangerType exchangerType, const string &symbol)
{
    throwIf(symbol.empty(), "Symbol info catalog requires a symbol");

    SymbolEntry &entry = getOrCreateEntry(exchangerType, symbol);
    if (entry.loadState->getStatus() == UiTaskState::Status::FAILED)
    {
        startLoad(exchangerType, symbol, entry);
    }
}

const SymbolInfo *SymbolInfoCatalog::getSymbolInfo(ExchangerType exchangerType, const string &symbol) const
{
    const SymbolEntry *entry = getEntry(exchangerType, symbol);
    if (entry == nullptr || !entry->symbolInfo.has_value())
    {
        return nullptr;
    }
    return &entry->symbolInfo.value();
}

const UiTaskState *SymbolInfoCatalog::getLoadState(ExchangerType exchangerType, const string &symbol) const
{
    const SymbolEntry *entry = getEntry(exchangerType, symbol);
    return entry == nullptr ? nullptr : entry->loadState.get();
}

SymbolInfoCatalog::SymbolEntry &SymbolInfoCatalog::getOrCreateEntry(ExchangerType exchangerType, const string &symbol)
{
    const SymbolKey key(exchangerType, symbol);
    const auto existingEntry = entries.find(key);
    if (existingEntry != entries.end())
    {
        return *existingEntry->second;
    }

    auto entry = make_unique<SymbolEntry>();
    entry->loadState = make_unique<UiTaskState>();
    connect(entry->loadState.get(),
            &UiTaskState::statusChanged,
            this,
            [this, exchangerType, symbol](UiTaskState::Status)
            { emit symbolInfoChanged(exchangerType, QString::fromStdString(symbol)); });
    auto [entryIterator, inserted] = entries.emplace(key, move(entry));
    Q_ASSERT(inserted);
    static_cast<void>(inserted);
    return *entryIterator->second;
}

const SymbolInfoCatalog::SymbolEntry *SymbolInfoCatalog::getEntry(ExchangerType exchangerType,
                                                                  const string &symbol) const
{
    const auto entry = entries.find(SymbolKey(exchangerType, symbol));
    return entry == entries.end() ? nullptr : entry->second.get();
}

shared_ptr<DealService> SymbolInfoCatalog::getDealService(ExchangerType exchangerType) const
{
    throwIf(exchangerType != ExchangerType::BINANCE && exchangerType != ExchangerType::BYBIT,
            "Unsupported symbol info exchange");
    return exchangerType == ExchangerType::BINANCE ? binanceDealService : bybitDealService;
}

void SymbolInfoCatalog::startLoad(ExchangerType exchangerType, const string &symbol, SymbolEntry &entry)
{
    shared_ptr<DealService> dealService = getDealService(exchangerType);
    taskExecutor.startTask(
        *entry.loadState,
        *this,
        [dealService = move(dealService), symbol]() { return dealService->getSymbolInfo(symbol); },
        [this, exchangerType, symbol](SymbolInfo symbolInfo)
        { storeSymbolInfo(exchangerType, symbol, move(symbolInfo)); });
}

void SymbolInfoCatalog::storeSymbolInfo(ExchangerType exchangerType, const string &symbol, SymbolInfo symbolInfo)
{
    getOrCreateEntry(exchangerType, symbol).symbolInfo = move(symbolInfo);
}
