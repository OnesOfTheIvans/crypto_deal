#include "OrderSessionModel.hpp"

#include "DealService.hpp"
#include "common/domain/OrderCategory.hpp"
#include "common/domain/PlaceOcoRequest.hpp"
#include "common/domain/PlaceOrderRequest.hpp"
#include "common/exception_handling.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"

#include <QDateTime>
#include <QMetaObject>

#include <algorithm>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>

using namespace exception_handling;
using namespace std;

namespace {
    bool isBasicOrderOperation(OperationType operation)
    {
        return operation == OperationType::BUY_CRYPTO || operation == OperationType::SELL_CRYPTO ||
               operation == OperationType::PLACE_ORDER;
    }

    OrderInfo placeCustomOrder(DealService &dealService, const BasicOrderDraft &draft)
    {
        PlaceOrderRequest request;
        request.symbol = draft.pair.symbol;
        request.side = draft.side;
        request.type = draft.type;
        request.quantity = draft.quantity;
        request.price = draft.price;
        request.timeInForce = draft.timeInForce;
        request.category = OrderCategory::SPOT;
        if (draft.exchangerType == ExchangerType::BYBIT && draft.side == OrderOperation::BUY &&
            draft.type == OrderType::MARKET)
        {
            request.marketUnit = "baseCoin";
        }
        return dealService.placeOrder(request);
    }

    OrderInfo placeBasicOrder(DealService &dealService, const BasicOrderDraft &draft)
    {
        switch (draft.operation)
        {
        case OperationType::BUY_CRYPTO:
            return dealService.buyCrypto(draft.pair.baseAsset, draft.pair.quoteAsset, draft.quantity);
        case OperationType::SELL_CRYPTO:
            return dealService.sellCrypto(draft.pair.baseAsset, draft.pair.quoteAsset, draft.quantity);
        case OperationType::PLACE_ORDER:
            return placeCustomOrder(dealService, draft);
        default:
            throw runtime_error("Unsupported basic order operation");
        }
    }

    OcoInfo placeOcoOrder(DealService &dealService, const OcoOrderDraft &draft)
    {
        PlaceOcoRequest request;
        request.symbol = draft.pair.symbol;
        request.side = draft.side;
        request.quantity = draft.quantity;
        request.price = draft.limitPrice;
        request.stopPrice = draft.stopPrice;
        request.stopLimitPrice = draft.stopLimitPrice;
        request.stopLimitTimeInForce = draft.stopLimitTimeInForce;
        return dealService.placeOco(request);
    }

    bool isOrderFilled(ExchangerType exchangerType, const OrderInfo &orderInfo)
    {
        return exchangerType == ExchangerType::BINANCE ? orderInfo.status == "FILLED" : orderInfo.status == "Filled";
    }
}

OrderSessionModel::OrderSessionModel(AsyncTaskExecutor &taskExecutor,
                                     shared_ptr<DealService> binanceDealService,
                                     shared_ptr<DealService> bybitDealService,
                                     QObject *parent)
    : QObject(parent), taskExecutor(taskExecutor), binanceDealService(move(binanceDealService)),
      bybitDealService(move(bybitDealService)), nextEntryId(1)
{
    throwIf(this->binanceDealService == nullptr, "Order session requires a Binance deal service");
    throwIf(this->bybitDealService == nullptr, "Order session requires a Bybit deal service");
}

bool OrderSessionModel::placeOrder(const BasicOrderDraft &draft)
{
    if (!isBasicOrderOperation(draft.operation))
    {
        return false;
    }

    const EntryId entryId = createBasicEntry(draft);
    const shared_ptr<DealService> dealService = getDealService(draft.exchangerType);
    const bool started = taskExecutor.startTask(
        getEntryTaskStates(entryId).submissionState,
        *this,
        [dealService, draft]() { return placeBasicOrder(*dealService, draft); },
        [this, entryId](OrderInfo orderInfo) { acceptBasicOrder(entryId, move(orderInfo)); },
        [this, entryId](const QString &failure) { failSubmission(entryId, failure); });
    if (!started)
    {
        failSubmission(entryId, "Order submission could not be started");
    }
    return started;
}

bool OrderSessionModel::placeOco(const OcoOrderDraft &draft)
{
    const EntryId entryId = createOcoEntry(draft);
    const shared_ptr<DealService> dealService = getDealService(draft.exchangerType);
    const bool started = taskExecutor.startTask(
        getEntryTaskStates(entryId).submissionState,
        *this,
        [dealService, draft]() { return placeOcoOrder(*dealService, draft); },
        [this, entryId](OcoInfo ocoInfo) { acceptOco(entryId, move(ocoInfo)); },
        [this, entryId](const QString &failure) { failSubmission(entryId, failure); });
    if (!started)
    {
        failSubmission(entryId, "OCO submission could not be started");
    }
    return started;
}

const vector<OrderSessionModel::Entry> &OrderSessionModel::getEntries() const
{
    return entries;
}

const OrderSessionModel::Entry *OrderSessionModel::findEntryById(EntryId entryId) const
{
    return findEntry(entryId);
}

bool OrderSessionModel::isActiveStatus(Status status)
{
    return status == Status::SUBMITTING || status == Status::ACCEPTED || status == Status::WAITING ||
           status == Status::MONITORING_FAILED;
}

shared_ptr<DealService> OrderSessionModel::getDealService(ExchangerType exchangerType) const
{
    switch (exchangerType)
    {
    case ExchangerType::BINANCE:
        return binanceDealService;
    case ExchangerType::BYBIT:
        return bybitDealService;
    }
    throw runtime_error("Unsupported exchange for order placement");
}

OrderSessionModel::EntryId OrderSessionModel::createBasicEntry(const BasicOrderDraft &draft)
{
    Entry entry;
    entry.id = nextEntryId++;
    entry.type = EntryType::BASIC_ORDER;
    entry.basicDraft = draft;
    entry.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    entry.updatedAtMs = entry.createdAtMs;
    const EntryId entryId = entry.id;
    entries.push_back(move(entry));
    entryTaskStates.try_emplace(entryId, make_unique<EntryTaskStates>());
    publishEntryChange(entryId);
    return entryId;
}

OrderSessionModel::EntryId OrderSessionModel::createOcoEntry(const OcoOrderDraft &draft)
{
    Entry entry;
    entry.id = nextEntryId++;
    entry.type = EntryType::OCO_GROUP;
    entry.ocoDraft = draft;
    entry.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    entry.updatedAtMs = entry.createdAtMs;
    const EntryId entryId = entry.id;
    entries.push_back(move(entry));
    entryTaskStates.try_emplace(entryId, make_unique<EntryTaskStates>());
    publishEntryChange(entryId);
    return entryId;
}

OrderSessionModel::Entry *OrderSessionModel::findEntry(EntryId entryId)
{
    const auto entry = ranges::find(entries, entryId, &Entry::id);
    return entry == entries.end() ? nullptr : &*entry;
}

const OrderSessionModel::Entry *OrderSessionModel::findEntry(EntryId entryId) const
{
    const auto entry = ranges::find(entries, entryId, &Entry::id);
    return entry == entries.end() ? nullptr : &*entry;
}

OrderSessionModel::EntryTaskStates &OrderSessionModel::getEntryTaskStates(EntryId entryId)
{
    return *entryTaskStates.at(entryId);
}

void OrderSessionModel::acceptBasicOrder(EntryId entryId, OrderInfo orderInfo)
{
    Entry *entry = findEntry(entryId);
    if (entry == nullptr || entry->status != Status::SUBMITTING || !entry->basicDraft.has_value())
    {
        return;
    }

    entry->acceptedOrder = move(orderInfo);
    entry->status = Status::ACCEPTED;
    entry->error.clear();
    publishEntryChange(entryId);
    QMetaObject::invokeMethod(this, [this, entryId]() { continueAfterBasicAcceptance(entryId); }, Qt::QueuedConnection);
}

void OrderSessionModel::continueAfterBasicAcceptance(EntryId entryId)
{
    Entry *entry = findEntry(entryId);
    if (entry == nullptr || entry->status != Status::ACCEPTED || !entry->acceptedOrder.has_value())
    {
        return;
    }

    if (isAcceptedBasicOrderFilled(*entry))
    {
        entry->terminalOrder = entry->acceptedOrder;
        entry->status = Status::FILLED;
        publishEntryChange(entryId);
        return;
    }
    startBasicOrderWait(entryId);
}

void OrderSessionModel::startBasicOrderWait(EntryId entryId)
{
    Entry *entry = findEntry(entryId);
    if (entry == nullptr || entry->status != Status::ACCEPTED || !entry->basicDraft.has_value() ||
        !entry->acceptedOrder.has_value())
    {
        return;
    }

    const OrderInfo orderInfo = entry->acceptedOrder.value();
    if (orderInfo.symbol.empty() || orderInfo.orderId.empty())
    {
        entry->status = Status::MONITORING_FAILED;
        entry->error = "The accepted order response does not contain a symbol and order ID required for monitoring";
        publishEntryChange(entryId);
        return;
    }

    entry->status = Status::WAITING;
    publishEntryChange(entryId);
    entry = findEntry(entryId);
    const uint64_t waitRevision = entry->revision;
    const shared_ptr<DealService> dealService = getDealService(entry->basicDraft->exchangerType);
    const string symbol = orderInfo.symbol;
    const string orderId = orderInfo.orderId;
    const bool started = taskExecutor.startTask(
        getEntryTaskStates(entryId).waitState,
        *this,
        [dealService, symbol, orderId](stop_token stopToken)
        {
            throwIf(stopToken.stop_requested(), "Order status monitoring stopped before it started");
            return dealService->waitUntilOrderFilled(symbol, orderId);
        },
        [this, entryId, waitRevision](OrderInfo filledOrder)
        { finishOrderWait(entryId, waitRevision, move(filledOrder)); },
        [this, entryId, waitRevision](const QString &failure) { failOrderWait(entryId, waitRevision, failure); });
    if (!started)
    {
        failOrderWait(entryId, waitRevision, "Order status monitoring could not be started");
    }
}

void OrderSessionModel::acceptOco(EntryId entryId, OcoInfo ocoInfo)
{
    Entry *entry = findEntry(entryId);
    if (entry == nullptr || entry->status != Status::SUBMITTING || !entry->ocoDraft.has_value())
    {
        return;
    }

    entry->acceptedOco = move(ocoInfo);
    entry->status = Status::ACCEPTED;
    entry->error.clear();
    publishEntryChange(entryId);
    QMetaObject::invokeMethod(this, [this, entryId]() { continueAfterOcoAcceptance(entryId); }, Qt::QueuedConnection);
}

void OrderSessionModel::continueAfterOcoAcceptance(EntryId entryId)
{
    Entry *entry = findEntry(entryId);
    if (entry == nullptr || entry->status != Status::ACCEPTED || !entry->acceptedOco.has_value())
    {
        return;
    }

    startOcoWait(entryId);
}

void OrderSessionModel::startOcoWait(EntryId entryId)
{
    Entry *entry = findEntry(entryId);
    if (entry == nullptr || entry->status != Status::ACCEPTED || !entry->ocoDraft.has_value() ||
        !entry->acceptedOco.has_value())
    {
        return;
    }

    const OcoInfo ocoInfo = entry->acceptedOco.value();
    if (ocoInfo.orderListId.empty() || ocoInfo.takeProfitOrder.symbol.empty() ||
        ocoInfo.takeProfitOrder.orderId.empty() || ocoInfo.stopLossOrder.symbol.empty() ||
        ocoInfo.stopLossOrder.orderId.empty())
    {
        entry->status = Status::MONITORING_FAILED;
        entry->error =
            "The accepted OCO response does not contain a group ID and both child orders required for monitoring";
        publishEntryChange(entryId);
        return;
    }

    entry->status = Status::WAITING;
    publishEntryChange(entryId);
    entry = findEntry(entryId);
    const uint64_t waitRevision = entry->revision;
    const shared_ptr<DealService> dealService = getDealService(entry->ocoDraft->exchangerType);
    const bool started = taskExecutor.startTask(
        getEntryTaskStates(entryId).waitState,
        *this,
        [dealService, ocoInfo](stop_token stopToken)
        {
            throwIf(stopToken.stop_requested(), "OCO status monitoring stopped before it started");
            return dealService->waitUntilOcoOrderFilled(ocoInfo);
        },
        [this, entryId, waitRevision](OcoWaitResult result) { finishOcoWait(entryId, waitRevision, move(result)); },
        [this, entryId, waitRevision](const QString &failure) { failOrderWait(entryId, waitRevision, failure); });
    if (!started)
    {
        failOrderWait(entryId, waitRevision, "OCO status monitoring could not be started");
    }
}

void OrderSessionModel::finishOrderWait(EntryId entryId, uint64_t waitRevision, OrderInfo orderInfo)
{
    if (!isCurrentWaitingRevision(entryId, waitRevision))
    {
        return;
    }

    Entry *entry = findEntry(entryId);
    entry->terminalOrder = move(orderInfo);
    entry->status = Status::FILLED;
    entry->error.clear();
    publishEntryChange(entryId);
}

void OrderSessionModel::finishOcoWait(EntryId entryId, uint64_t waitRevision, OcoWaitResult result)
{
    if (!isCurrentWaitingRevision(entryId, waitRevision))
    {
        return;
    }

    Entry *entry = findEntry(entryId);
    if (!updateOcoChildrenAfterCompletion(*entry, result))
    {
        entry->status = Status::MONITORING_FAILED;
        entry->error = "The OCO completion did not identify both accepted child orders";
        publishEntryChange(entryId);
        return;
    }

    entry->terminalOrder = move(result.filledOrder);
    entry->status = Status::FILLED;
    entry->error.clear();
    publishEntryChange(entryId);
}

void OrderSessionModel::failSubmission(EntryId entryId, const QString &failure)
{
    Entry *entry = findEntry(entryId);
    if (entry == nullptr || entry->status != Status::SUBMITTING)
    {
        return;
    }

    entry->status = Status::SUBMISSION_FAILED;
    entry->error = failure;
    publishEntryChange(entryId);
}

void OrderSessionModel::failOrderWait(EntryId entryId, uint64_t waitRevision, const QString &failure)
{
    if (!isCurrentWaitingRevision(entryId, waitRevision))
    {
        return;
    }

    Entry *entry = findEntry(entryId);
    entry->status = Status::MONITORING_FAILED;
    entry->error = failure;
    publishEntryChange(entryId);
}

bool OrderSessionModel::updateOcoChildrenAfterCompletion(Entry &entry, const OcoWaitResult &result)
{
    if (!entry.acceptedOco.has_value())
    {
        return false;
    }

    OcoInfo &ocoInfo = entry.acceptedOco.value();
    if (ocoInfo.takeProfitOrder.orderId == result.filledOrder.orderId &&
        ocoInfo.stopLossOrder.orderId == result.siblingTerminalOrder.orderId)
    {
        ocoInfo.takeProfitOrder = result.filledOrder;
        ocoInfo.stopLossOrder = result.siblingTerminalOrder;
        return true;
    }
    if (ocoInfo.stopLossOrder.orderId == result.filledOrder.orderId &&
        ocoInfo.takeProfitOrder.orderId == result.siblingTerminalOrder.orderId)
    {
        ocoInfo.stopLossOrder = result.filledOrder;
        ocoInfo.takeProfitOrder = result.siblingTerminalOrder;
        return true;
    }

    return false;
}

void OrderSessionModel::publishEntryChange(EntryId entryId)
{
    Entry *entry = findEntry(entryId);
    if (entry == nullptr)
    {
        return;
    }

    entry->updatedAtMs = QDateTime::currentMSecsSinceEpoch();
    ++entry->revision;
    sortEntries();
    emit entriesChanged();
}

void OrderSessionModel::sortEntries()
{
    ranges::sort(entries,
                 [](const Entry &left, const Entry &right)
                 {
                     if (left.updatedAtMs != right.updatedAtMs)
                     {
                         return left.updatedAtMs > right.updatedAtMs;
                     }
                     return left.id > right.id;
                 });
}

bool OrderSessionModel::isCurrentWaitingRevision(EntryId entryId, uint64_t waitRevision) const
{
    const Entry *entry = findEntry(entryId);
    return entry != nullptr && entry->status == Status::WAITING && entry->revision == waitRevision;
}

bool OrderSessionModel::isAcceptedBasicOrderFilled(const Entry &entry) const
{
    return entry.basicDraft.has_value() && entry.acceptedOrder.has_value() &&
           isOrderFilled(entry.basicDraft->exchangerType, entry.acceptedOrder.value());
}
