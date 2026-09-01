#include "../src/OperationChain/OperationChainRunManager.hpp"
#include "TestDealService.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <semaphore>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;

namespace {
    class RunManagerDealService final : public TestDealService
    {
      public:
        using BuyAction = function<OrderInfo(const string &, const string &, Decimal)>;
        using WaitAction = function<OrderInfo(const string &, const string &)>;
        using StopAction = function<void()>;

      private:
        BuyAction buyAction;
        WaitAction waitAction;
        StopAction stopAction;

      public:
        RunManagerDealService(ExchangerType exchangerType,
                              BuyAction buyAction,
                              WaitAction waitAction,
                              StopAction stopAction = {})
            : TestDealService(exchangerType), buyAction(move(buyAction)), waitAction(move(waitAction)),
              stopAction(move(stopAction))
        {}

        OrderInfo buyCrypto(const string &assetToBuy, const string &assetToSell, Decimal quantity) override
        {
            return buyAction ? buyAction(assetToBuy, assetToSell, quantity) : OrderInfo{};
        }

        OrderInfo waitUntilOrderFilled(const string &symbol, const string &orderId) override
        {
            return waitAction ? waitAction(symbol, orderId) : OrderInfo{};
        }

        void stopUserStream() override
        {
            if (stopAction)
            {
                stopAction();
            }
            TestDealService::stopUserStream();
        }
    };

    OperationChainDefinition
    createBuyDefinition(string name, string outAsset, ExchangerType exchangerType = ExchangerType::BINANCE)
    {
        return OperationChainDefinition(move(name),
                                        exchangerType,
                                        "USDT",
                                        Decimal{1},
                                        {{OperationType::BUY_CRYPTO, BaseConfig{move(outAsset)}}});
    }

    OrderInfo
    createAcceptedOrder(const string &assetToBuy, const string &assetToSell, Decimal quantity, size_t orderNumber)
    {
        OrderInfo order;
        order.symbol = assetToBuy + assetToSell;
        order.orderId = "run-order-" + to_string(orderNumber);
        order.origQty = quantity;
        order.status = "NEW";
        return order;
    }

    OrderInfo createFilledOrder(const string &symbol, const string &orderId)
    {
        OrderInfo order;
        order.symbol = symbol;
        order.orderId = orderId;
        order.origQty = Decimal{1};
        order.executedQty = Decimal{1};
        order.status = "FILLED";
        return order;
    }

    shared_ptr<RunManagerDealService> createImmediateService(ExchangerType exchangerType)
    {
        auto nextOrderNumber = make_shared<atomic<size_t>>(1);
        return make_shared<RunManagerDealService>(
            exchangerType,
            [nextOrderNumber](const string &assetToBuy, const string &assetToSell, Decimal quantity)
            { return createAcceptedOrder(assetToBuy, assetToSell, quantity, nextOrderNumber->fetch_add(1)); },
            [](const string &symbol, const string &orderId) { return createFilledOrder(symbol, orderId); });
    }
}

TEST(OperationChainRunManagerTest, RunsSameAndDifferentDefinitionsConcurrentlyAndRetainsOrderedHistory)
{
    atomic<size_t> nextOrderNumber{1};
    latch waitsStarted(3);
    counting_semaphore<3> releaseWaits(0);
    const auto createBlockingService = [&nextOrderNumber, &waitsStarted, &releaseWaits](ExchangerType exchangerType)
    {
        return make_shared<RunManagerDealService>(
            exchangerType,
            [&nextOrderNumber](const string &assetToBuy, const string &assetToSell, Decimal quantity)
            { return createAcceptedOrder(assetToBuy, assetToSell, quantity, nextOrderNumber.fetch_add(1)); },
            [&waitsStarted, &releaseWaits](const string &symbol, const string &orderId)
            {
                waitsStarted.count_down();
                releaseWaits.acquire();
                return createFilledOrder(symbol, orderId);
            });
    };
    auto binanceService = createBlockingService(ExchangerType::BINANCE);
    auto bybitService = createBlockingService(ExchangerType::BYBIT);
    OperationChainRunManager manager(
        {createBuyDefinition("Buy BTC", "BTC"), createBuyDefinition("Buy ETH", "ETH", ExchangerType::BYBIT)},
        binanceService,
        bybitService);

    const OperationChainRunId firstRunId = manager.startRun("Buy BTC");
    const OperationChainRunId secondRunId = manager.startRun("Buy BTC");
    const OperationChainRunId thirdRunId = manager.startRun("Buy ETH");
    waitsStarted.wait();

    EXPECT_EQ(firstRunId, 1);
    EXPECT_EQ(secondRunId, 2);
    EXPECT_EQ(thirdRunId, 3);
    const vector<OperationChainRunSnapshot> activeRuns = manager.getRuns();
    ASSERT_EQ(activeRuns.size(), 3);
    set<uint64_t> activeUpdateSequences;
    for (size_t index = 0; index < activeRuns.size(); ++index)
    {
        EXPECT_EQ(activeRuns[index].runId, index + 1);
        EXPECT_EQ(activeRuns[index].chainSnapshot.status, OperationChainStatus::RUNNING);
        ASSERT_EQ(activeRuns[index].chainSnapshot.steps.size(), 1);
        EXPECT_EQ(activeRuns[index].chainSnapshot.steps[0].status, OperationStepStatus::AWAITING);
        EXPECT_TRUE(activeRuns[index].chainSnapshot.steps[0].acceptedIdentifiers.orderId.has_value());
        activeUpdateSequences.insert(activeRuns[index].updateSequence);
    }
    EXPECT_EQ(activeUpdateSequences.size(), activeRuns.size());

    releaseWaits.release(3);
    manager.stopAndWait();

    const vector<OperationChainRunSnapshot> completedRuns = manager.getRuns();
    ASSERT_EQ(completedRuns.size(), 3);
    for (size_t index = 0; index < completedRuns.size(); ++index)
    {
        EXPECT_EQ(completedRuns[index].runId, index + 1);
        EXPECT_EQ(completedRuns[index].chainSnapshot.status, OperationChainStatus::COMPLETED);
        EXPECT_GT(completedRuns[index].updateSequence, activeRuns[index].updateSequence);
    }
    EXPECT_EQ(completedRuns[0].chainSnapshot.definitionName, "Buy BTC");
    EXPECT_EQ(completedRuns[1].chainSnapshot.definitionName, "Buy BTC");
    EXPECT_EQ(completedRuns[2].chainSnapshot.definitionName, "Buy ETH");
    EXPECT_EQ(completedRuns[0].chainSnapshot.currentContext.asset, "BTC");
    EXPECT_EQ(completedRuns[1].chainSnapshot.currentContext.asset, "BTC");
    EXPECT_EQ(completedRuns[2].chainSnapshot.currentContext.asset, "ETH");
}

TEST(OperationChainRunManagerTest, IsolatesFailedRunFromConcurrentSuccessfulRun)
{
    latch placementsStarted(2);
    atomic<size_t> nextOrderNumber{1};
    auto binanceService = make_shared<RunManagerDealService>(
        ExchangerType::BINANCE,
        [&placementsStarted, &nextOrderNumber](const string &assetToBuy, const string &assetToSell, Decimal quantity)
        {
            placementsStarted.count_down();
            if (assetToBuy == "FAIL")
            {
                throw runtime_error("Planned chain failure");
            }
            return createAcceptedOrder(assetToBuy, assetToSell, quantity, nextOrderNumber.fetch_add(1));
        },
        [](const string &symbol, const string &orderId) { return createFilledOrder(symbol, orderId); });
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Failing", "FAIL"), createBuyDefinition("Successful", "BTC")},
                                     binanceService,
                                     bybitService);

    const OperationChainRunId failedRunId = manager.startRun("Failing");
    const OperationChainRunId successfulRunId = manager.startRun("Successful");
    placementsStarted.wait();
    manager.stopAndWait();

    const optional<OperationChainRunSnapshot> failedRun = manager.getRun(failedRunId);
    const optional<OperationChainRunSnapshot> successfulRun = manager.getRun(successfulRunId);
    ASSERT_TRUE(failedRun.has_value());
    ASSERT_TRUE(successfulRun.has_value());
    EXPECT_EQ(failedRun.value().chainSnapshot.status, OperationChainStatus::FAILED);
    EXPECT_EQ(failedRun.value().chainSnapshot.error, "Planned chain failure");
    EXPECT_EQ(successfulRun.value().chainSnapshot.status, OperationChainStatus::COMPLETED);
    EXPECT_TRUE(successfulRun.value().chainSnapshot.error.empty());
}

TEST(OperationChainRunManagerTest, PublishesDetachedGloballySequencedSnapshots)
{
    auto binanceService = createImmediateService(ExchangerType::BINANCE);
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Observable", "BTC")}, binanceService, bybitService);
    mutex eventsMutex;
    condition_variable eventsChanged;
    vector<OperationChainRunSnapshot> events;
    manager.setRunChangeHandler(
        [&eventsMutex, &eventsChanged, &events](OperationChainRunSnapshot snapshot)
        {
            {
                lock_guard<mutex> lock(eventsMutex);
                events.push_back(snapshot);
            }
            snapshot.chainSnapshot.definitionName = "Mutated callback copy";
            eventsChanged.notify_all();
        });

    const OperationChainRunId runId = manager.startRun("Observable");
    {
        unique_lock<mutex> lock(eventsMutex);
        ASSERT_TRUE(eventsChanged.wait_for(lock,
                                           chrono::seconds(2),
                                           [&events]() {
                                               return !events.empty() && events.back().chainSnapshot.status ==
                                                                             OperationChainStatus::COMPLETED;
                                           }));
    }
    manager.stopAndWait();

    ASSERT_GE(events.size(), 5);
    EXPECT_EQ(events.front().runId, runId);
    EXPECT_EQ(events.front().chainSnapshot.status, OperationChainStatus::PENDING);
    EXPECT_EQ(events.front().chainSnapshot.revision, 0);
    for (size_t index = 1; index < events.size(); ++index)
    {
        EXPECT_GT(events[index].updateSequence, events[index - 1].updateSequence);
        EXPECT_GT(events[index].chainSnapshot.revision, events[index - 1].chainSnapshot.revision);
    }

    optional<OperationChainRunSnapshot> detachedSnapshot = manager.getRun(runId);
    ASSERT_TRUE(detachedSnapshot.has_value());
    EXPECT_EQ(detachedSnapshot.value().chainSnapshot.definitionName, "Observable");
    detachedSnapshot.value().chainSnapshot.definitionName = "Mutated getter copy";
    const optional<OperationChainRunSnapshot> storedSnapshot = manager.getRun(runId);
    ASSERT_TRUE(storedSnapshot.has_value());
    EXPECT_EQ(storedSnapshot.value().chainSnapshot.definitionName, "Observable");
}

TEST(OperationChainRunManagerTest, ReportsObserverFailureWithoutChangingRun)
{
    latch waitCompleted(1);
    auto nextOrderNumber = make_shared<atomic<size_t>>(1);
    auto binanceService = make_shared<RunManagerDealService>(
        ExchangerType::BINANCE,
        [nextOrderNumber](const string &assetToBuy, const string &assetToSell, Decimal quantity)
        { return createAcceptedOrder(assetToBuy, assetToSell, quantity, nextOrderNumber->fetch_add(1)); },
        [&waitCompleted](const string &symbol, const string &orderId)
        {
            waitCompleted.count_down();
            return createFilledOrder(symbol, orderId);
        });
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Observer failure", "BTC")}, binanceService, bybitService);
    manager.setRunChangeHandler([](OperationChainRunSnapshot) { throw runtime_error("Planned observer failure"); });

    testing::internal::CaptureStderr();
    const OperationChainRunId runId = manager.startRun("Observer failure");
    waitCompleted.wait();
    manager.stopAndWait();
    const string errorOutput = testing::internal::GetCapturedStderr();

    EXPECT_NE(errorOutput.find("Operation-chain run observer failed: Planned observer failure"), string::npos);
    const optional<OperationChainRunSnapshot> run = manager.getRun(runId);
    ASSERT_TRUE(run.has_value());
    EXPECT_EQ(run.value().chainSnapshot.status, OperationChainStatus::COMPLETED);
}

TEST(OperationChainRunManagerTest, ValidatesDefinitionsAndRejectsStartsAfterShutdown)
{
    auto binanceService = createImmediateService(ExchangerType::BINANCE);
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    const OperationChainDefinition definition = createBuyDefinition("Known", "BTC");

    EXPECT_THROW(OperationChainRunManager({definition, definition}, binanceService, bybitService), runtime_error);
    EXPECT_THROW(OperationChainRunManager({definition}, bybitService, binanceService), runtime_error);

    OperationChainRunManager manager({definition}, binanceService, bybitService);
    EXPECT_THROW(manager.startRun("Missing"), runtime_error);
    const OperationChainRunId runId = manager.startRun("Known");
    manager.stopAndWait();

    EXPECT_EQ(runId, 1);
    ASSERT_EQ(manager.getDefinitions().size(), 1);
    EXPECT_EQ(manager.getDefinitions().front().getName(), "Known");
    EXPECT_TRUE(manager.isStopping());
    EXPECT_THROW(manager.startRun("Known"), runtime_error);
    EXPECT_EQ(manager.getRuns().size(), 1);
}

TEST(OperationChainRunManagerTest, JoinsWaitingWorkerAfterSharedServiceInterruption)
{
    latch waitStarted(1);
    binary_semaphore releaseWait(0);
    atomic<bool> waitReleased{false};
    auto nextOrderNumber = make_shared<atomic<size_t>>(1);
    auto binanceService = make_shared<RunManagerDealService>(
        ExchangerType::BINANCE,
        [nextOrderNumber](const string &assetToBuy, const string &assetToSell, Decimal quantity)
        { return createAcceptedOrder(assetToBuy, assetToSell, quantity, nextOrderNumber->fetch_add(1)); },
        [&waitStarted, &releaseWait](const string &, const string &) -> OrderInfo
        {
            waitStarted.count_down();
            releaseWait.acquire();
            throw runtime_error("User stream stopped during chain wait");
        },
        [&waitReleased, &releaseWait]()
        {
            if (!waitReleased.exchange(true))
            {
                releaseWait.release();
            }
        });
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Waiting", "BTC")}, binanceService, bybitService);

    const OperationChainRunId runId = manager.startRun("Waiting");
    waitStarted.wait();
    manager.requestStop();
    binanceService->stopUserStream();
    manager.stopAndWait();

    const optional<OperationChainRunSnapshot> run = manager.getRun(runId);
    ASSERT_TRUE(run.has_value());
    EXPECT_EQ(run.value().chainSnapshot.status, OperationChainStatus::FAILED);
    EXPECT_EQ(run.value().chainSnapshot.error, "User stream stopped during chain wait");
    EXPECT_TRUE(manager.isStopping());
}
