#ifndef ORDER_PLACEMENT_MODEL_H
#define ORDER_PLACEMENT_MODEL_H

#include "BasicOrderDraft.hpp"
#include "common/domain/OrderInfo.hpp"
#include "graphical/async/UiTaskState.hpp"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

class AsyncTaskExecutor;
class DealService;

class OrderPlacementModel final : public QObject
{
    Q_OBJECT

  public:
    enum class Status
    {
        IDLE,
        SUBMITTING,
        ACCEPTED,
        WAITING,
        FILLED,
        SUBMISSION_FAILED,
        WAIT_FAILED
    };
    Q_ENUM(Status)

  private:
    AsyncTaskExecutor &taskExecutor;
    std::shared_ptr<DealService> binanceDealService;
    std::shared_ptr<DealService> bybitDealService;
    UiTaskState submissionState;
    UiTaskState waitState;
    Status status;
    std::optional<BasicOrderDraft> currentDraft;
    std::optional<OrderInfo> acceptedOrder;
    std::optional<OrderInfo> terminalOrder;
    QString error;

    std::shared_ptr<DealService> getDealService(ExchangerType exchangerType) const;

    void acceptOrder(OrderInfo orderInfo);

    void continueAfterAcceptance();

    void startOrderWait();

    void finishOrderWait(OrderInfo orderInfo);

    void failSubmission(const QString &failure);

    void failOrderWait(const QString &failure);

    void updateStatus(Status newStatus);

    bool isAcceptedOrderFilled() const;

  public:
    OrderPlacementModel(AsyncTaskExecutor &taskExecutor,
                        std::shared_ptr<DealService> binanceDealService,
                        std::shared_ptr<DealService> bybitDealService,
                        QObject *parent = nullptr);

    bool placeOrder(const BasicOrderDraft &draft);

    Status getStatus() const;

    const std::optional<BasicOrderDraft> &getCurrentDraft() const;

    const std::optional<OrderInfo> &getAcceptedOrder() const;

    const std::optional<OrderInfo> &getTerminalOrder() const;

    const QString &getError() const;

    bool hasActivePlacement() const;

  signals:
    void statusChanged(OrderPlacementModel::Status status);
};

#endif
