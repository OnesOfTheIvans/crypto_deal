#ifndef ORDER_SESSION_MODEL_H
#define ORDER_SESSION_MODEL_H

#include "BasicOrderDraft.hpp"
#include "OcoOrderDraft.hpp"
#include "common/domain/OcoInfo.hpp"
#include "common/domain/OcoWaitResult.hpp"
#include "common/domain/OrderInfo.hpp"
#include "graphical/async/UiTaskState.hpp"

#include <QObject>
#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
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
        SUBMISSION_FAILED,
        MONITORING_FAILED
    };
    Q_ENUM(Status)

    struct Entry
    {
        EntryId id = 0;
        EntryType type = EntryType::BASIC_ORDER;
        Status status = Status::SUBMITTING;
        std::optional<BasicOrderDraft> basicDraft;
        std::optional<OcoOrderDraft> ocoDraft;
        std::optional<OrderInfo> acceptedOrder;
        std::optional<OcoInfo> acceptedOco;
        std::optional<OrderInfo> terminalOrder;
        QString error;
        qint64 createdAtMs = 0;
        qint64 updatedAtMs = 0;
        std::uint64_t revision = 0;
    };

  private:
    struct EntryTaskStates
    {
        UiTaskState submissionState;
        UiTaskState waitState;
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

    bool updateOcoChildrenAfterCompletion(Entry &entry, const OcoWaitResult &result);

    void publishEntryChange(EntryId entryId);

    void sortEntries();

    bool isCurrentWaitingRevision(EntryId entryId, std::uint64_t waitRevision) const;

    bool isAcceptedBasicOrderFilled(const Entry &entry) const;

  public:
    OrderSessionModel(AsyncTaskExecutor &taskExecutor,
                      std::shared_ptr<DealService> binanceDealService,
                      std::shared_ptr<DealService> bybitDealService,
                      QObject *parent = nullptr);

    bool placeOrder(const BasicOrderDraft &draft);

    bool placeOco(const OcoOrderDraft &draft);

    const std::vector<Entry> &getEntries() const;

    const Entry *findEntryById(EntryId entryId) const;

    static bool isActiveStatus(Status status);

  signals:
    void entriesChanged();
};

#endif
