#include "OrderPlacementModel.hpp"

#include "DealService.hpp"
#include "common/domain/OrderCategory.hpp"
#include "common/domain/PlaceOrderRequest.hpp"
#include "common/exception_handling.hpp"
#include "graphical/async/AsyncTaskExecutor.hpp"

#include <QMetaObject>

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
}

OrderPlacementModel::OrderPlacementModel(AsyncTaskExecutor &taskExecutor,
                                         shared_ptr<DealService> binanceDealService,
                                         shared_ptr<DealService> bybitDealService,
                                         QObject *parent)
    : QObject(parent), taskExecutor(taskExecutor), binanceDealService(move(binanceDealService)),
      bybitDealService(move(bybitDealService)), submissionState(this), waitState(this), status(Status::IDLE)
{
    throwIf(this->binanceDealService == nullptr, "Order placement requires a Binance deal service");
    throwIf(this->bybitDealService == nullptr, "Order placement requires a Bybit deal service");
}

bool OrderPlacementModel::placeOrder(const BasicOrderDraft &draft)
{
    if (hasActivePlacement() || !isBasicOrderOperation(draft.operation))
    {
        return false;
    }

    currentDraft = draft;
    acceptedOrder.reset();
    terminalOrder.reset();
    error.clear();
    updateStatus(Status::SUBMITTING);

    const shared_ptr<DealService> dealService = getDealService(draft.exchangerType);
    const bool started = taskExecutor.startTask(
        submissionState,
        *this,
        [dealService, draft]() { return placeBasicOrder(*dealService, draft); },
        [this](OrderInfo orderInfo) { acceptOrder(move(orderInfo)); },
        [this](const QString &failure) { failSubmission(failure); });
    if (!started)
    {
        failSubmission("Order submission could not be started");
    }
    return started;
}

OrderPlacementModel::Status OrderPlacementModel::getStatus() const
{
    return status;
}

const optional<BasicOrderDraft> &OrderPlacementModel::getCurrentDraft() const
{
    return currentDraft;
}

const optional<OrderInfo> &OrderPlacementModel::getAcceptedOrder() const
{
    return acceptedOrder;
}

const optional<OrderInfo> &OrderPlacementModel::getTerminalOrder() const
{
    return terminalOrder;
}

const QString &OrderPlacementModel::getError() const
{
    return error;
}

bool OrderPlacementModel::hasActivePlacement() const
{
    return status == Status::SUBMITTING || status == Status::ACCEPTED || status == Status::WAITING;
}

shared_ptr<DealService> OrderPlacementModel::getDealService(ExchangerType exchangerType) const
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

void OrderPlacementModel::acceptOrder(OrderInfo orderInfo)
{
    acceptedOrder = move(orderInfo);
    updateStatus(Status::ACCEPTED);
    QMetaObject::invokeMethod(this, [this]() { continueAfterAcceptance(); }, Qt::QueuedConnection);
}

void OrderPlacementModel::continueAfterAcceptance()
{
    if (status != Status::ACCEPTED || !acceptedOrder.has_value())
    {
        return;
    }
    if (isAcceptedOrderFilled())
    {
        terminalOrder = acceptedOrder;
        updateStatus(Status::FILLED);
        return;
    }
    startOrderWait();
}

void OrderPlacementModel::startOrderWait()
{
    const OrderInfo &orderInfo = acceptedOrder.value();
    if (orderInfo.symbol.empty() || orderInfo.orderId.empty())
    {
        failOrderWait("The accepted order response does not contain a symbol and order ID required for monitoring");
        return;
    }

    updateStatus(Status::WAITING);
    const shared_ptr<DealService> dealService = getDealService(currentDraft.value().exchangerType);
    const string symbol = orderInfo.symbol;
    const string orderId = orderInfo.orderId;
    const bool started = taskExecutor.startTask(
        waitState,
        *this,
        [dealService, symbol, orderId](stop_token stopToken)
        {
            throwIf(stopToken.stop_requested(), "Order status monitoring stopped before it started");
            return dealService->waitUntilOrderFilled(symbol, orderId);
        },
        [this](OrderInfo filledOrder) { finishOrderWait(move(filledOrder)); },
        [this](const QString &failure) { failOrderWait(failure); });
    if (!started)
    {
        failOrderWait("Order status monitoring could not be started");
    }
}

void OrderPlacementModel::finishOrderWait(OrderInfo orderInfo)
{
    terminalOrder = move(orderInfo);
    updateStatus(Status::FILLED);
}

void OrderPlacementModel::failSubmission(const QString &failure)
{
    error = failure;
    updateStatus(Status::SUBMISSION_FAILED);
}

void OrderPlacementModel::failOrderWait(const QString &failure)
{
    error = failure;
    updateStatus(Status::WAIT_FAILED);
}

void OrderPlacementModel::updateStatus(Status newStatus)
{
    status = newStatus;
    emit statusChanged(status);
}

bool OrderPlacementModel::isAcceptedOrderFilled() const
{
    if (!acceptedOrder.has_value() || !currentDraft.has_value())
    {
        return false;
    }
    const string &exchangeStatus = acceptedOrder->status;
    return currentDraft->exchangerType == ExchangerType::BINANCE ? exchangeStatus == "FILLED"
                                                                 : exchangeStatus == "Filled";
}
