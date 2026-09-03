#ifndef OPERATION_CANCELLATION_COORDINATOR_H
#define OPERATION_CANCELLATION_COORDINATOR_H

#include "ExchangerPull.hpp"
#include "common/domain/OcoInfo.hpp"
#include "common/domain/OrderInfo.hpp"

#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <string>
#include <variant>

class OperationCancellationCoordinator
{
  private:
    struct OrdinaryOrderAction
    {
        Exchanger service;
        OrderInfo orderInfo;
    };

    struct OcoOrderAction
    {
        Exchanger service;
        OcoInfo ocoInfo;
    };

    enum class Result
    {
        NONE,
        CONFIRMED_CANCELLED,
        NATURAL_COMPLETION,
        FAILED
    };

    using CurrentAction = std::variant<std::monostate, OrdinaryOrderAction, OcoOrderAction>;

    mutable std::mutex stateMutex;
    std::condition_variable_any stateChanged;
    bool cancellationRequested = false;
    bool operationActive = false;
    bool actionMayBeRegistered = false;
    bool actionCompletedNaturally = false;
    bool chainFinished = false;
    bool cancellationWorkerRunning = false;
    CurrentAction currentAction;
    std::stop_source waitStopSource;
    Result result = Result::NONE;
    std::string failure;

    void confirmOrdinaryCancellation(const OrdinaryOrderAction &action, std::stop_token stopToken);

    void confirmOcoCancellation(const OcoOrderAction &action, std::stop_token stopToken);

    void recordConfirmedCancellation();

    void recordNaturalCompletion();

    void recordCancellationFailure(const std::string &error);

    void finishCancellationWorker();

  public:
    void requestCancellation();

    void failCancellation(const std::string &error);

    bool isCancellationRequested() const;

    void throwIfCancellationRequested() const;

    void startOperation(bool canRegisterAction);

    void registerOrdinaryOrder(const Exchanger &service, const OrderInfo &orderInfo);

    void registerOcoOrder(const Exchanger &service, const OcoInfo &ocoInfo);

    std::stop_token getWaitStopToken() const;

    void completeActionNaturally();

    void finishOperation();

    void finishChain();

    void cancelCurrentOperation(std::stop_token stopToken);

    void waitForCancellationResolution();

    bool isCancellationConfirmed() const;

    bool hasCancellationFailed() const;

    std::string getCancellationFailure() const;
};

#endif
