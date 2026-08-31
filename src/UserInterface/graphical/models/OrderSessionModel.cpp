#include "OrderSessionModel.hpp"

#include "DealService.hpp"
#include "common/OrderStatusUtil.hpp"
#include "common/domain/OrderCategory.hpp"
#include "common/domain/OrderListQuery.hpp"
#include "common/domain/PlaceOcoRequest.hpp"
#include "common/domain/PlaceOrderRequest.hpp"
#include "common/exception_handling.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"

#include <QDateTime>
#include <QMetaObject>

#include <algorithm>
#include <exception>
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

    QString getRefreshFailure(const optional<string> &failure, const QString &childName)
    {
        return failure.has_value() ? childName + " refresh failed: " + QString::fromStdString(failure.value())
                                   : QString{};
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

bool OrderSessionModel::refreshOrder(EntryId entryId)
{
    if (!canRefreshOrder(entryId))
    {
        return false;
    }

    Entry *entry = findEntry(entryId);
    const optional<EntryRefreshRequest> request = createEntryRefreshRequest(*entry);
    if (!request.has_value())
    {
        return false;
    }

    const shared_ptr<DealService> dealService = getDealService(getEntryExchangerType(*entry));
    const EntryRefreshRequest refreshRequest = request.value();
    const bool started = taskExecutor.startTask(
        getEntryTaskStates(entryId).actionState,
        *this,
        [dealService, refreshRequest]() { return getEntryRefreshResult(*dealService, refreshRequest); },
        [this](EntryRefreshResult result)
        { finishEntryRefresh(move(result), Action::REFRESHING, "Exchange status refreshed"); },
        [this, entryId, actionRevision = refreshRequest.revision](const QString &failure)
        { failEntryAction(entryId, actionRevision, Action::REFRESHING, "Refresh failed: ", failure); });
    if (!started)
    {
        return false;
    }

    entry = findEntry(entryId);
    entry->action = Action::REFRESHING;
    entry->actionMessage.clear();
    entry->actionError.clear();
    publishEntryActionChange(entryId);
    return true;
}

bool OrderSessionModel::cancelOrder(EntryId entryId)
{
    if (!canCancelOrder(entryId))
    {
        return false;
    }

    Entry *entry = findEntry(entryId);
    const optional<EntryRefreshRequest> request = createEntryRefreshRequest(*entry);
    if (!request.has_value())
    {
        return false;
    }

    const shared_ptr<DealService> dealService = getDealService(getEntryExchangerType(*entry));
    const EntryRefreshRequest refreshRequest = request.value();
    bool started = false;
    if (entry->type == EntryType::BASIC_ORDER)
    {
        started = taskExecutor.startTask(
            getEntryTaskStates(entryId).actionState,
            *this,
            [dealService, refreshRequest]()
            {
                EntryRefreshResult result;
                result.request = refreshRequest;
                result.basicOrder = dealService->cancelOrder(refreshRequest.basicOrder.value());
                return result;
            },
            [this](EntryRefreshResult result)
            { finishEntryRefresh(move(result), Action::CANCELLING, "Order cancellation accepted by the exchange"); },
            [this, entryId, actionRevision = refreshRequest.revision](const QString &failure)
            { failEntryAction(entryId, actionRevision, Action::CANCELLING, "Cancellation failed: ", failure); });
    }
    else
    {
        const OcoInfo ocoInfo = entry->acceptedOco.value();
        started = taskExecutor.startTask(
            getEntryTaskStates(entryId).actionState,
            *this,
            [dealService, refreshRequest, ocoInfo]()
            {
                OrderListQuery query;
                query.symbol = ocoInfo.takeProfitOrder.symbol;
                if (!ocoInfo.orderListId.empty())
                {
                    query.orderListId = ocoInfo.orderListId;
                }
                if (!ocoInfo.listClientOrderId.empty())
                {
                    query.listClientOrderId = ocoInfo.listClientOrderId;
                }
                dealService->cancelOco(query);
                return getEntryRefreshResult(*dealService, refreshRequest);
            },
            [this](EntryRefreshResult result) { finishOcoCancellation(move(result)); },
            [this, entryId, actionRevision = refreshRequest.revision](const QString &failure)
            { failEntryAction(entryId, actionRevision, Action::CANCELLING, "OCO cancellation failed: ", failure); });
    }

    if (!started)
    {
        return false;
    }

    entry = findEntry(entryId);
    entry->action = Action::CANCELLING;
    entry->actionMessage.clear();
    entry->actionError.clear();
    publishEntryActionChange(entryId);
    return true;
}

bool OrderSessionModel::cancelAllOpenOrders(EntryId entryId)
{
    if (!canCancelAllOpenOrders(entryId))
    {
        return false;
    }

    Entry *entry = findEntry(entryId);
    const ExchangerType exchangerType = getEntryExchangerType(*entry);
    const string symbol = getEntryPair(*entry).symbol;
    const vector<EntryRefreshRequest> requests = createPairRefreshRequests(exchangerType, symbol);
    const shared_ptr<DealService> dealService = getDealService(exchangerType);
    const bool started = taskExecutor.startTask(
        getEntryTaskStates(entryId).actionState,
        *this,
        [dealService, symbol, requests]()
        {
            dealService->cancelAllOpenOrders(symbol, OrderCategory::SPOT);
            CancelAllResult result;
            result.entryResults.reserve(requests.size());
            for (const EntryRefreshRequest &request : requests)
            {
                result.entryResults.push_back(getEntryRefreshResult(*dealService, request));
            }
            return result;
        },
        [this, exchangerType, symbol, requests](CancelAllResult result)
        { finishCancelAll(exchangerType, symbol, requests, move(result)); },
        [this, exchangerType, symbol, requests](const QString &failure)
        { failCancelAll(exchangerType, symbol, requests, failure); });
    if (!started)
    {
        return false;
    }

    startPairAction(exchangerType, symbol, requests);
    return true;
}

const vector<OrderSessionModel::Entry> &OrderSessionModel::getEntries() const
{
    return entries;
}

const OrderSessionModel::Entry *OrderSessionModel::findEntryById(EntryId entryId) const
{
    return findEntry(entryId);
}

bool OrderSessionModel::canRefreshOrder(EntryId entryId) const
{
    const Entry *entry = findEntry(entryId);
    return entry != nullptr && entry->action == Action::NONE && createEntryRefreshRequest(*entry).has_value();
}

bool OrderSessionModel::canCancelOrder(EntryId entryId) const
{
    const Entry *entry = findEntry(entryId);
    if (entry == nullptr || entry->action != Action::NONE ||
        (entry->status != Status::ACCEPTED && entry->status != Status::WAITING &&
         entry->status != Status::MONITORING_FAILED))
    {
        return false;
    }

    return createEntryRefreshRequest(*entry).has_value();
}

bool OrderSessionModel::canCancelAllOpenOrders(EntryId entryId) const
{
    const Entry *contextEntry = findEntry(entryId);
    if (contextEntry == nullptr)
    {
        return false;
    }

    const ExchangerType exchangerType = getEntryExchangerType(*contextEntry);
    const string &symbol = getEntryPair(*contextEntry).symbol;
    if (symbol.empty())
    {
        return false;
    }

    return ranges::none_of(entries,
                           [exchangerType, &symbol](const Entry &entry)
                           {
                               return getEntryExchangerType(entry) == exchangerType &&
                                      getEntryPair(entry).symbol == symbol &&
                                      (entry.status == Status::SUBMITTING || entry.action != Action::NONE);
                           });
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

optional<OrderSessionModel::EntryRefreshRequest> OrderSessionModel::createEntryRefreshRequest(const Entry &entry) const
{
    EntryRefreshRequest request;
    request.entryId = entry.id;
    request.revision = entry.revision;
    request.type = entry.type;

    if (entry.type == EntryType::BASIC_ORDER)
    {
        const OrderInfo *orderInfo = nullptr;
        if (entry.terminalOrder.has_value())
        {
            orderInfo = &entry.terminalOrder.value();
        }
        else if (entry.acceptedOrder.has_value())
        {
            orderInfo = &entry.acceptedOrder.value();
        }
        if (orderInfo == nullptr || orderInfo->symbol.empty() ||
            (orderInfo->orderId.empty() && orderInfo->clientOrderId.empty()))
        {
            return nullopt;
        }

        request.basicOrder = createOrderQuery(*orderInfo);
        return request;
    }

    if (!entry.acceptedOco.has_value())
    {
        return nullopt;
    }

    const OcoInfo &ocoInfo = entry.acceptedOco.value();
    if (ocoInfo.takeProfitOrder.symbol.empty() ||
        (ocoInfo.takeProfitOrder.orderId.empty() && ocoInfo.takeProfitOrder.clientOrderId.empty()) ||
        ocoInfo.stopLossOrder.symbol.empty() ||
        (ocoInfo.stopLossOrder.orderId.empty() && ocoInfo.stopLossOrder.clientOrderId.empty()))
    {
        return nullopt;
    }

    request.takeProfitOrder = createOrderQuery(ocoInfo.takeProfitOrder);
    request.stopLossOrder = createOrderQuery(ocoInfo.stopLossOrder);
    return request;
}

vector<OrderSessionModel::EntryRefreshRequest> OrderSessionModel::createPairRefreshRequests(ExchangerType exchangerType,
                                                                                            const string &symbol) const
{
    vector<EntryRefreshRequest> requests;
    for (const Entry &entry : entries)
    {
        if (getEntryExchangerType(entry) != exchangerType || getEntryPair(entry).symbol != symbol)
        {
            continue;
        }

        const optional<EntryRefreshRequest> identifiableRequest = createEntryRefreshRequest(entry);
        if (identifiableRequest.has_value())
        {
            requests.push_back(identifiableRequest.value());
            continue;
        }

        EntryRefreshRequest request;
        request.entryId = entry.id;
        request.revision = entry.revision;
        request.type = entry.type;
        requests.push_back(move(request));
    }
    return requests;
}

OrderSessionModel::EntryRefreshResult OrderSessionModel::getEntryRefreshResult(DealService &dealService,
                                                                               const EntryRefreshRequest &request)
{
    EntryRefreshResult result;
    result.request = request;
    if (request.basicOrder.has_value())
    {
        try
        {
            result.basicOrder = dealService.getOrder(request.basicOrder.value());
        }
        catch (const exception &error)
        {
            result.basicError = error.what();
        }
        catch (...)
        {
            result.basicError = "Non-standard exception";
        }
        return result;
    }

    if (request.takeProfitOrder.has_value())
    {
        try
        {
            result.takeProfitOrder = dealService.getOrder(request.takeProfitOrder.value());
        }
        catch (const exception &error)
        {
            result.takeProfitError = error.what();
        }
        catch (...)
        {
            result.takeProfitError = "Non-standard exception";
        }
    }
    if (request.stopLossOrder.has_value())
    {
        try
        {
            result.stopLossOrder = dealService.getOrder(request.stopLossOrder.value());
        }
        catch (const exception &error)
        {
            result.stopLossError = error.what();
        }
        catch (...)
        {
            result.stopLossError = "Non-standard exception";
        }
    }
    return result;
}

OrderQuery OrderSessionModel::createOrderQuery(const OrderInfo &orderInfo)
{
    OrderQuery query;
    query.symbol = orderInfo.symbol;
    if (!orderInfo.orderId.empty())
    {
        query.orderId = orderInfo.orderId;
    }
    else if (!orderInfo.clientOrderId.empty())
    {
        query.clientOrderId = orderInfo.clientOrderId;
    }
    query.category = OrderCategory::SPOT;
    return query;
}

QString OrderSessionModel::getOcoRefreshError(const EntryRefreshResult &result)
{
    const QString takeProfitFailure = getRefreshFailure(result.takeProfitError, "Take-profit child");
    const QString stopLossFailure = getRefreshFailure(result.stopLossError, "Stop-loss child");
    if (takeProfitFailure.isEmpty())
    {
        return stopLossFailure;
    }
    if (stopLossFailure.isEmpty())
    {
        return takeProfitFailure;
    }
    return takeProfitFailure + "\n" + stopLossFailure;
}

QString OrderSessionModel::getEntryRefreshError(const EntryRefreshResult &result)
{
    if (result.basicError.has_value())
    {
        return "Order refresh failed: " + QString::fromStdString(result.basicError.value());
    }
    return getOcoRefreshError(result);
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
    if (entry->action == Action::CANCELLING || entry->action == Action::CANCELLING_ALL)
    {
        publishEntryActionChange(entryId);
    }
    else
    {
        publishEntryChange(entryId);
    }
}

void OrderSessionModel::finishEntryRefresh(EntryRefreshResult result, Action action, const QString &successMessage)
{
    Entry *entry = findEntry(result.request.entryId);
    if (entry == nullptr || !isEntryActionCurrent(*entry, result.request.revision, action))
    {
        if (entry != nullptr && entry->action == action)
        {
            entry->action = Action::NONE;
            publishEntryActionChange(entry->id);
        }
        return;
    }

    const bool becameTerminal = applyEntryRefreshResult(*entry, result);
    QString message = successMessage;
    QString actionError = getEntryRefreshError(result);
    if (entry->type == EntryType::OCO_GROUP && !actionError.isEmpty())
    {
        const bool hasAvailableChildStatus = result.takeProfitOrder.has_value() || result.stopLossOrder.has_value();
        message = hasAvailableChildStatus ? "Available OCO child status refreshed" : QString{};
    }
    else if (entry->type == EntryType::BASIC_ORDER && !actionError.isEmpty())
    {
        message.clear();
    }
    if (action == Action::CANCELLING)
    {
        message = entry->status == Status::TERMINAL ? "Order cancellation confirmed"
                  : entry->status == Status::FILLED ? "Order filled before cancellation completed"
                                                    : "Order cancellation accepted; terminal confirmation pending";
    }

    finishEntryAction(*entry, result.request.revision, action, message, actionError);
    if (becameTerminal)
    {
        publishEntryChange(entry->id);
    }
    else
    {
        publishEntryActionChange(entry->id);
    }
}

void OrderSessionModel::finishOcoCancellation(EntryRefreshResult result)
{
    Entry *entry = findEntry(result.request.entryId);
    if (entry == nullptr || !isEntryActionCurrent(*entry, result.request.revision, Action::CANCELLING))
    {
        if (entry != nullptr && entry->action == Action::CANCELLING)
        {
            entry->action = Action::NONE;
            publishEntryActionChange(entry->id);
        }
        return;
    }

    const bool becameTerminal = applyEntryRefreshResult(*entry, result);
    const QString refreshError = getOcoRefreshError(result);
    const QString message = entry->status == Status::TERMINAL ? "OCO cancellation confirmed"
                            : entry->status == Status::FILLED
                                ? "OCO reached a confirmed fill while cancellation was in progress"
                                : "OCO cancellation accepted; terminal confirmation pending";
    const QString actionError = refreshError.isEmpty() ? QString{} : "Cancellation accepted; " + refreshError;
    finishEntryAction(*entry, result.request.revision, Action::CANCELLING, message, actionError);
    if (becameTerminal)
    {
        publishEntryChange(entry->id);
    }
    else
    {
        publishEntryActionChange(entry->id);
    }
}

void OrderSessionModel::failEntryAction(EntryId entryId,
                                        uint64_t actionRevision,
                                        Action action,
                                        const QString &failurePrefix,
                                        const QString &failure)
{
    Entry *entry = findEntry(entryId);
    if (entry == nullptr || entry->action != action)
    {
        return;
    }

    if (entry->revision == actionRevision)
    {
        finishEntryAction(*entry, actionRevision, action, {}, failurePrefix + failure);
    }
    else
    {
        entry->action = Action::NONE;
    }
    publishEntryActionChange(entryId);
}

void OrderSessionModel::finishCancelAll(ExchangerType exchangerType,
                                        const string &symbol,
                                        const vector<EntryRefreshRequest> &requests,
                                        CancelAllResult result)
{
    for (EntryRefreshResult &entryResult : result.entryResults)
    {
        Entry *entry = findEntry(entryResult.request.entryId);
        if (entry == nullptr || !isEntryActionCurrent(*entry, entryResult.request.revision, Action::CANCELLING_ALL))
        {
            continue;
        }

        const bool hasRefreshResult = entryResult.basicOrder.has_value() || entryResult.takeProfitOrder.has_value() ||
                                      entryResult.stopLossOrder.has_value();
        const bool becameTerminal = applyEntryRefreshResult(*entry, entryResult);
        const QString refreshError = getEntryRefreshError(entryResult);
        const QString message = !hasRefreshResult
                                    ? "Cancel All accepted; this session row has no exchange status to refresh"
                                : entry->status == Status::FILLED || entry->status == Status::TERMINAL
                                    ? "Cancel All accepted; terminal state confirmed"
                                    : "Cancel All accepted; terminal confirmation pending";
        const QString actionError = refreshError.isEmpty() ? QString{} : "Cancel All accepted; " + refreshError;
        finishEntryAction(*entry, entryResult.request.revision, Action::CANCELLING_ALL, message, actionError);
        if (becameTerminal)
        {
            publishEntryChange(entry->id);
        }
        else
        {
            publishEntryActionChange(entry->id);
        }
    }

    finishPairAction(exchangerType, symbol, requests, "Cancel All accepted", {});
}

void OrderSessionModel::failCancelAll(ExchangerType exchangerType,
                                      const string &symbol,
                                      const vector<EntryRefreshRequest> &requests,
                                      const QString &failure)
{
    finishPairAction(exchangerType, symbol, requests, {}, "Cancel All failed: " + failure);
}

bool OrderSessionModel::applyEntryRefreshResult(Entry &entry, const EntryRefreshResult &result)
{
    if (entry.type == EntryType::BASIC_ORDER && result.basicOrder.has_value())
    {
        return applyBasicOrderRefresh(entry, result.basicOrder.value());
    }
    if (entry.type == EntryType::OCO_GROUP)
    {
        return applyOcoRefresh(entry, result);
    }
    return false;
}

bool OrderSessionModel::applyBasicOrderRefresh(Entry &entry, const OrderInfo &orderInfo)
{
    const ExchangerType exchangerType = getEntryExchangerType(entry);
    const OrderStatusState state = OrderStatusUtil::classifyOrderStatus(exchangerType, orderInfo.status);
    if ((entry.status == Status::FILLED || entry.status == Status::TERMINAL) &&
        !OrderStatusUtil::isOrderTerminal(exchangerType, orderInfo.status))
    {
        return false;
    }

    entry.acceptedOrder = orderInfo;
    if (state == OrderStatusState::FILLED)
    {
        const bool becameTerminal = entry.status != Status::FILLED;
        entry.terminalOrder = orderInfo;
        entry.status = Status::FILLED;
        entry.error.clear();
        return becameTerminal;
    }
    if (state == OrderStatusState::CANCELLED || state == OrderStatusState::OTHER_TERMINAL)
    {
        const bool becameTerminal = entry.status != Status::TERMINAL;
        entry.terminalOrder = orderInfo;
        entry.status = Status::TERMINAL;
        entry.error.clear();
        return becameTerminal;
    }
    return false;
}

bool OrderSessionModel::applyOcoRefresh(Entry &entry, const EntryRefreshResult &result)
{
    if (!entry.acceptedOco.has_value())
    {
        return false;
    }

    OcoInfo &ocoInfo = entry.acceptedOco.value();
    const ExchangerType exchangerType = getEntryExchangerType(entry);
    updateOcoChildAfterRefresh(ocoInfo.takeProfitOrder, result.takeProfitOrder, exchangerType);
    updateOcoChildAfterRefresh(ocoInfo.stopLossOrder, result.stopLossOrder, exchangerType);
    const bool takeProfitFilled = OrderStatusUtil::isOrderFilled(exchangerType, ocoInfo.takeProfitOrder.status);
    const bool stopLossFilled = OrderStatusUtil::isOrderFilled(exchangerType, ocoInfo.stopLossOrder.status);
    const bool takeProfitTerminal = OrderStatusUtil::isOrderTerminal(exchangerType, ocoInfo.takeProfitOrder.status);
    const bool stopLossTerminal = OrderStatusUtil::isOrderTerminal(exchangerType, ocoInfo.stopLossOrder.status);
    if ((takeProfitFilled || stopLossFilled) && takeProfitTerminal && stopLossTerminal)
    {
        const bool becameTerminal = entry.status != Status::FILLED;
        entry.terminalOrder = takeProfitFilled ? ocoInfo.takeProfitOrder : ocoInfo.stopLossOrder;
        entry.status = Status::FILLED;
        entry.error.clear();
        return becameTerminal;
    }
    if (takeProfitTerminal && stopLossTerminal)
    {
        const bool becameTerminal = entry.status != Status::TERMINAL;
        entry.terminalOrder.reset();
        entry.status = Status::TERMINAL;
        entry.error.clear();
        return becameTerminal;
    }
    return false;
}

void OrderSessionModel::updateOcoChildAfterRefresh(OrderInfo &childOrder,
                                                   const optional<OrderInfo> &refreshedOrder,
                                                   ExchangerType exchangerType)
{
    if (!refreshedOrder.has_value() || refreshedOrder->orderId != childOrder.orderId)
    {
        return;
    }
    if (OrderStatusUtil::isOrderTerminal(exchangerType, childOrder.status) &&
        !OrderStatusUtil::isOrderTerminal(exchangerType, refreshedOrder->status))
    {
        return;
    }

    childOrder = refreshedOrder.value();
}

void OrderSessionModel::finishEntryAction(Entry &entry,
                                          uint64_t actionRevision,
                                          Action action,
                                          const QString &message,
                                          const QString &error)
{
    if (!isEntryActionCurrent(entry, actionRevision, action))
    {
        return;
    }

    entry.action = Action::NONE;
    entry.actionMessage = message;
    entry.actionError = error;
}

void OrderSessionModel::startPairAction(ExchangerType, const string &, const vector<EntryRefreshRequest> &requests)
{
    for (const EntryRefreshRequest &request : requests)
    {
        Entry *entry = findEntry(request.entryId);
        if (entry == nullptr || entry->revision != request.revision)
        {
            continue;
        }
        entry->action = Action::CANCELLING_ALL;
        entry->actionMessage.clear();
        entry->actionError.clear();
        publishEntryActionChange(entry->id);
    }
}

void OrderSessionModel::finishPairAction(ExchangerType,
                                         const string &,
                                         const vector<EntryRefreshRequest> &requests,
                                         const QString &message,
                                         const QString &error)
{
    for (const EntryRefreshRequest &request : requests)
    {
        Entry *entry = findEntry(request.entryId);
        if (entry == nullptr || entry->action != Action::CANCELLING_ALL)
        {
            continue;
        }

        if (entry->revision == request.revision)
        {
            finishEntryAction(*entry, request.revision, Action::CANCELLING_ALL, message, error);
        }
        else
        {
            entry->action = Action::NONE;
        }
        publishEntryActionChange(entry->id);
    }
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

void OrderSessionModel::publishEntryActionChange(EntryId entryId)
{
    Entry *entry = findEntry(entryId);
    if (entry == nullptr)
    {
        return;
    }

    entry->updatedAtMs = QDateTime::currentMSecsSinceEpoch();
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
           OrderStatusUtil::isOrderFilled(entry.basicDraft->exchangerType, entry.acceptedOrder->status);
}

bool OrderSessionModel::isEntryActionCurrent(const Entry &entry, uint64_t actionRevision, Action action) const
{
    return entry.revision == actionRevision && entry.action == action;
}

ExchangerType OrderSessionModel::getEntryExchangerType(const Entry &entry)
{
    return entry.type == EntryType::BASIC_ORDER ? entry.basicDraft->exchangerType : entry.ocoDraft->exchangerType;
}

const TradablePair &OrderSessionModel::getEntryPair(const Entry &entry)
{
    return entry.type == EntryType::BASIC_ORDER ? entry.basicDraft->pair : entry.ocoDraft->pair;
}
