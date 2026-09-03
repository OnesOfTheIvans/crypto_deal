#ifndef ORDER_SESSION_MODEL_H
#define ORDER_SESSION_MODEL_H

#include "BasicOrderDraft.hpp"
#include "OcoOrderDraft.hpp"
#include "common/domain/OcoInfo.hpp"
#include "common/domain/OcoWaitResult.hpp"
#include "common/domain/OrderInfo.hpp"
#include "common/domain/OrderQuery.hpp"
#include "graphical/async/UiTaskState.hpp"

#include <QObject>
#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class AsyncTaskExecutor;
class DealService;

class OrderSessionModel final : public QObject
{
    Q_OBJECT

  public:
    using EntryId = std::uint64_t;

    enum class EntryType
    {
        BASIC_ORDER,
        OCO_GROUP
    };
    Q_ENUM(EntryType)

    enum class Status
    {
        SUBMITTING,
        ACCEPTED,
        WAITING,
        FILLED,
        TERMINAL,
        SUBMISSION_FAILED,
        MONITORING_FAILED
    };
    Q_ENUM(Status)

    enum class Action
    {
        NONE,
        REFRESHING,
        CANCELLING,
        CANCELLING_ALL
    };
    Q_ENUM(Action)

    struct Entry
    {
        EntryId id = 0;
        EntryType type = EntryType::BASIC_ORDER;
        Status status = Status::SUBMITTING;
        Action action = Action::NONE;
        std::optional<BasicOrderDraft> basicDraft;
        std::optional<OcoOrderDraft> ocoDraft;
        std::optional<OrderInfo> acceptedOrder;
        std::optional<OcoInfo> acceptedOco;
        std::optional<OrderInfo> terminalOrder;
        QString error;
        QString actionMessage;
        QString actionError;
        qint64 createdAtMs = 0;
        qint64 updatedAtMs = 0;
        std::uint64_t revision = 0;
    };

  private:
    struct EntryTaskStates
    {
        UiTaskState submissionState;
        UiTaskState waitState;
        UiTaskState actionState;
    };

    struct EntryRefreshRequest
    {
        EntryId entryId = 0;
        std::uint64_t revision = 0;
        EntryType type = EntryType::BASIC_ORDER;
        std::optional<OrderQuery> basicOrder;
        std::optional<OrderQuery> takeProfitOrder;
        std::optional<OrderQuery> stopLossOrder;
    };

    struct EntryRefreshResult
    {
        EntryRefreshRequest request;
        std::optional<OrderInfo> basicOrder;
        std::optional<OrderInfo> takeProfitOrder;
        std::optional<OrderInfo> stopLossOrder;
        std::optional<std::string> basicError;
        std::optional<std::string> takeProfitError;
        std::optional<std::string> stopLossError;
    };

    struct CancelAllResult
    {
        std::vector<EntryRefreshResult> entryResults;
    };

    AsyncTaskExecutor &taskExecutor;
    std::shared_ptr<DealService> binanceDealService;
    std::shared_ptr<DealService> bybitDealService;
    std::vector<Entry> entries;
    std::map<EntryId, std::unique_ptr<EntryTaskStates>> entryTaskStates;
    EntryId nextEntryId;

    std::shared_ptr<DealService> getDealService(ExchangerType exchangerType) const;

    EntryId createBasicEntry(const BasicOrderDraft &draft);

    EntryId createOcoEntry(const OcoOrderDraft &draft);

    Entry *findEntry(EntryId entryId);

    const Entry *findEntry(EntryId entryId) const;

    EntryTaskStates &getEntryTaskStates(EntryId entryId);

    std::optional<EntryRefreshRequest> createEntryRefreshRequest(const Entry &entry) const;

    std::vector<EntryRefreshRequest> createPairRefreshRequests(ExchangerType exchangerType,
                                                               const std::string &symbol) const;

    static EntryRefreshResult getEntryRefreshResult(DealService &dealService, const EntryRefreshRequest &request);

    static OrderQuery createOrderQuery(const OrderInfo &orderInfo);

    static QString getOcoRefreshError(const EntryRefreshResult &result);

    static QString getEntryRefreshError(const EntryRefreshResult &result);

    void acceptBasicOrder(EntryId entryId, OrderInfo orderInfo);

    void continueAfterBasicAcceptance(EntryId entryId);

    void startBasicOrderWait(EntryId entryId);

    void acceptOco(EntryId entryId, OcoInfo ocoInfo);

    void continueAfterOcoAcceptance(EntryId entryId);

    void startOcoWait(EntryId entryId);

    void finishOrderWait(EntryId entryId, std::uint64_t waitRevision, OrderInfo orderInfo);

    void finishOcoWait(EntryId entryId, std::uint64_t waitRevision, OcoWaitResult result);

    void failSubmission(EntryId entryId, const QString &failure);

    void failOrderWait(EntryId entryId, std::uint64_t waitRevision, const QString &failure);

    void finishEntryRefresh(EntryRefreshResult result, Action action, const QString &successMessage);

    void finishOcoCancellation(EntryRefreshResult result);

    void failEntryAction(EntryId entryId,
                         std::uint64_t actionRevision,
                         Action action,
                         const QString &failurePrefix,
                         const QString &failure);

    void finishCancelAll(ExchangerType exchangerType,
                         const std::string &symbol,
                         const std::vector<EntryRefreshRequest> &requests,
                         CancelAllResult result);

    void failCancelAll(ExchangerType exchangerType,
                       const std::string &symbol,
                       const std::vector<EntryRefreshRequest> &requests,
                       const QString &failure);

    bool applyEntryRefreshResult(Entry &entry, const EntryRefreshResult &result);

    bool applyBasicOrderRefresh(Entry &entry, const OrderInfo &orderInfo);

    bool applyOcoRefresh(Entry &entry, const EntryRefreshResult &result);

    static void updateOcoChildAfterRefresh(OrderInfo &childOrder,
                                           const std::optional<OrderInfo> &refreshedOrder,
                                           ExchangerType exchangerType);

    void finishEntryAction(Entry &entry,
                           std::uint64_t actionRevision,
                           Action action,
                           const QString &message,
                           const QString &error);

    void startPairAction(ExchangerType exchangerType,
                         const std::string &symbol,
                         const std::vector<EntryRefreshRequest> &requests);

    void finishPairAction(ExchangerType exchangerType,
                          const std::string &symbol,
                          const std::vector<EntryRefreshRequest> &requests,
                          const QString &message,
                          const QString &error);

    bool updateOcoChildrenAfterCompletion(Entry &entry, const OcoWaitResult &result);

    void publishEntryChange(EntryId entryId);

    void publishEntryActionChange(EntryId entryId);

    void sortEntries();

    bool isCurrentWaitingRevision(EntryId entryId, std::uint64_t waitRevision) const;

    bool isAcceptedBasicOrderFilled(const Entry &entry) const;

    bool isEntryActionCurrent(const Entry &entry, std::uint64_t actionRevision, Action action) const;

    static ExchangerType getEntryExchangerType(const Entry &entry);

    static const TradablePair &getEntryPair(const Entry &entry);

  public:
    OrderSessionModel(AsyncTaskExecutor &taskExecutor,
                      std::shared_ptr<DealService> binanceDealService,
                      std::shared_ptr<DealService> bybitDealService,
                      QObject *parent = nullptr);

    bool placeOrder(const BasicOrderDraft &draft);

    bool placeOco(const OcoOrderDraft &draft);

    bool refreshOrder(EntryId entryId);

    bool cancelOrder(EntryId entryId);

    bool cancelAllOpenOrders(EntryId entryId);

    const std::vector<Entry> &getEntries() const;

    const Entry *findEntryById(EntryId entryId) const;

    bool canRefreshOrder(EntryId entryId) const;

    bool canCancelOrder(EntryId entryId) const;

    bool canCancelAllOpenOrders(EntryId entryId) const;

    static bool isActiveStatus(Status status);

  signals:
    void entriesChanged();
};

#endif
