#include "OperationCancellationCoordinator.hpp"

#include "OperationCancellationRequested.hpp"
#include "common/OrderStatusUtil.hpp"
#include "common/OrderWaitInterrupted.hpp"
#include "common/exception_handling.hpp"

#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

using namespace exception_handling;
using namespace std;

namespace {
    string describeTerminalStatus(const string &description, const OrderInfo &orderInfo)
    {
        string message = description + " reached terminal status " + orderInfo.status;
        if (!orderInfo.statusReason.empty())
        {
            message += ": " + orderInfo.statusReason;
        }
        return message;
    }
}

void OperationCancellationCoordinator::requestCancellation()
{
    {
        lock_guard<mutex> lock(stateMutex);
        cancellationRequested = true;
    }
    stateChanged.notify_all();
}

void OperationCancellationCoordinator::failCancellation(const string &error)
{
    recordCancellationFailure(error);
}

bool OperationCancellationCoordinator::isCancellationRequested() const
{
    lock_guard<mutex> lock(stateMutex);
    return cancellationRequested;
}

void OperationCancellationCoordinator::throwIfCancellationRequested() const
{
    if (isCancellationRequested())
    {
        throw OperationCancellationRequested("Operation-chain cancellation was requested");
    }
}

void OperationCancellationCoordinator::startOperation(bool canRegisterAction)
{
    {
        lock_guard<mutex> lock(stateMutex);
        throwIf(operationActive, "Operation-chain cancellation coordinator already has an active operation");
        operationActive = true;
        actionMayBeRegistered = canRegisterAction;
        actionCompletedNaturally = false;
        currentAction = monostate{};
        waitStopSource = stop_source{};
        result = Result::NONE;
        failure.clear();
    }
    stateChanged.notify_all();
}

void OperationCancellationCoordinator::registerOrdinaryOrder(const Exchanger &service, const OrderInfo &orderInfo)
{
    {
        lock_guard<mutex> lock(stateMutex);
        throwIf(!operationActive || !actionMayBeRegistered,
                "Cannot register an ordinary order outside a cancellable chain operation");
        throwIf(service == nullptr, "Cannot register an ordinary order without an exchange service");
        throwIf(!holds_alternative<monostate>(currentAction),
                "Operation-chain cancellation coordinator already has a registered action");
        currentAction = OrdinaryOrderAction{service, orderInfo};
        actionMayBeRegistered = false;
    }
    stateChanged.notify_all();
}

void OperationCancellationCoordinator::registerOcoOrder(const Exchanger &service, const OcoInfo &ocoInfo)
{
    {
        lock_guard<mutex> lock(stateMutex);
        throwIf(!operationActive || !actionMayBeRegistered,
                "Cannot register an OCO order outside a cancellable chain operation");
        throwIf(service == nullptr, "Cannot register an OCO order without an exchange service");
        throwIf(!holds_alternative<monostate>(currentAction),
                "Operation-chain cancellation coordinator already has a registered action");
        currentAction = OcoOrderAction{service, ocoInfo};
        actionMayBeRegistered = false;
    }
    stateChanged.notify_all();
}

stop_token OperationCancellationCoordinator::getWaitStopToken() const
{
    lock_guard<mutex> lock(stateMutex);
    return waitStopSource.get_token();
}

void OperationCancellationCoordinator::completeActionNaturally()
{
    {
        lock_guard<mutex> lock(stateMutex);
        if (!operationActive)
        {
            return;
        }
        actionCompletedNaturally = true;
        actionMayBeRegistered = false;
        currentAction = monostate{};
        if (cancellationRequested)
        {
            result = Result::NATURAL_COMPLETION;
            failure.clear();
        }
    }
    stateChanged.notify_all();
}

void OperationCancellationCoordinator::finishOperation()
{
    {
        lock_guard<mutex> lock(stateMutex);
        operationActive = false;
        actionMayBeRegistered = false;
        currentAction = monostate{};
    }
    stateChanged.notify_all();
}

void OperationCancellationCoordinator::finishChain()
{
    {
        lock_guard<mutex> lock(stateMutex);
        chainFinished = true;
        operationActive = false;
        actionMayBeRegistered = false;
        currentAction = monostate{};
    }
    stateChanged.notify_all();
}

void OperationCancellationCoordinator::recordConfirmedCancellation()
{
    stop_source stopSource;
    {
        lock_guard<mutex> lock(stateMutex);
        if (actionCompletedNaturally || result == Result::NATURAL_COMPLETION)
        {
            return;
        }
        result = Result::CONFIRMED_CANCELLED;
        failure.clear();
        stopSource = waitStopSource;
    }
    stopSource.request_stop();
    stateChanged.notify_all();
}

void OperationCancellationCoordinator::recordNaturalCompletion()
{
    {
        lock_guard<mutex> lock(stateMutex);
        result = Result::NATURAL_COMPLETION;
        failure.clear();
    }
    stateChanged.notify_all();
}

void OperationCancellationCoordinator::recordCancellationFailure(const string &error)
{
    stop_source stopSource;
    {
        lock_guard<mutex> lock(stateMutex);
        if (actionCompletedNaturally || result == Result::NATURAL_COMPLETION)
        {
            return;
        }
        result = Result::FAILED;
        failure = error;
        stopSource = waitStopSource;
    }
    stopSource.request_stop();
    stateChanged.notify_all();
}

void OperationCancellationCoordinator::finishCancellationWorker()
{
    {
        lock_guard<mutex> lock(stateMutex);
        cancellationWorkerRunning = false;
    }
    stateChanged.notify_all();
}

void OperationCancellationCoordinator::confirmOrdinaryCancellation(const OrdinaryOrderAction &action,
                                                                   stop_token stopToken)
{
    OrderQuery query;
    query.symbol = action.orderInfo.symbol;
    query.orderId = action.orderInfo.orderId;
    const OrderInfo terminalOrder = action.service->cancelOrderAndWaitUntilTerminal(query, stopToken);
    const OrderStatusState status =
        OrderStatusUtil::classifyOrderStatus(action.service->getExchangerType(), terminalOrder.status);
    if (status == OrderStatusState::FILLED)
    {
        recordNaturalCompletion();
        return;
    }
    if (status == OrderStatusState::CANCELLED)
    {
        recordConfirmedCancellation();
        return;
    }

    recordCancellationFailure(describeTerminalStatus("Order cancellation", terminalOrder));
}

void OperationCancellationCoordinator::confirmOcoCancellation(const OcoOrderAction &action, stop_token stopToken)
{
    const OcoInfo terminalOcoInfo = action.service->cancelOcoAndWaitUntilTerminal(action.ocoInfo, stopToken);
    const ExchangerType exchangerType = action.service->getExchangerType();
    const OrderStatusState takeProfitStatus =
        OrderStatusUtil::classifyOrderStatus(exchangerType, terminalOcoInfo.takeProfitOrder.status);
    const OrderStatusState stopLossStatus =
        OrderStatusUtil::classifyOrderStatus(exchangerType, terminalOcoInfo.stopLossOrder.status);
    if (takeProfitStatus == OrderStatusState::FILLED || stopLossStatus == OrderStatusState::FILLED)
    {
        recordNaturalCompletion();
        return;
    }
    if (takeProfitStatus == OrderStatusState::CANCELLED && stopLossStatus == OrderStatusState::CANCELLED)
    {
        recordConfirmedCancellation();
        return;
    }

    recordCancellationFailure(describeTerminalStatus("OCO take-profit cancellation", terminalOcoInfo.takeProfitOrder) +
                              "; " +
                              describeTerminalStatus("OCO stop-loss cancellation", terminalOcoInfo.stopLossOrder));
}

void OperationCancellationCoordinator::cancelCurrentOperation(stop_token stopToken)
{
    CurrentAction action;
    bool hasAction = false;
    {
        unique_lock<mutex> lock(stateMutex);
        cancellationWorkerRunning = true;
        const bool ready = stateChanged.wait(
            lock,
            stopToken,
            [this]() { return chainFinished || !operationActive || !holds_alternative<monostate>(currentAction); });
        if (ready && !chainFinished && operationActive)
        {
            action = currentAction;
            hasAction = !holds_alternative<monostate>(action);
        }
    }
    if (!hasAction)
    {
        finishCancellationWorker();
        return;
    }

    try
    {
        if (holds_alternative<OrdinaryOrderAction>(action))
        {
            confirmOrdinaryCancellation(get<OrdinaryOrderAction>(action), stopToken);
        }
        else if (holds_alternative<OcoOrderAction>(action))
        {
            confirmOcoCancellation(get<OcoOrderAction>(action), stopToken);
        }
    }
    catch (const OrderWaitInterrupted &)
    {
        if (!stopToken.stop_requested())
        {
            recordCancellationFailure("Operation-chain cancellation confirmation was interrupted");
        }
    }
    catch (const exception &exception)
    {
        recordCancellationFailure(exception.what());
    }
    catch (...)
    {
        recordCancellationFailure("Operation-chain cancellation failed with a non-standard exception");
    }
    finishCancellationWorker();
}

void OperationCancellationCoordinator::waitForCancellationResolution()
{
    unique_lock<mutex> lock(stateMutex);
    if (!cancellationRequested)
    {
        return;
    }
    stateChanged.wait(lock, [this]() { return result != Result::NONE || !cancellationWorkerRunning; });
}

bool OperationCancellationCoordinator::isCancellationConfirmed() const
{
    lock_guard<mutex> lock(stateMutex);
    return result == Result::CONFIRMED_CANCELLED;
}

bool OperationCancellationCoordinator::hasCancellationFailed() const
{
    lock_guard<mutex> lock(stateMutex);
    return result == Result::FAILED;
}

string OperationCancellationCoordinator::getCancellationFailure() const
{
    lock_guard<mutex> lock(stateMutex);
    return failure;
}
