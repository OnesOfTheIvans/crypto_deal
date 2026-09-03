#include "../src/OperationChain/OperationChainRunManager.hpp"
#include "../src/OperationChain/DefaultSimulatedOperationChainRuns.hpp"
#include "TestDealService.hpp"
#include "common/OrderWaitInterrupted.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <latch>
#include <map>
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
    bool isTerminal(OperationChainStatus status)
    {
        return status == OperationChainStatus::COMPLETED || status == OperationChainStatus::FAILED ||
               status == OperationChainStatus::CANCELLED;
    }

    class RunObserver
    {
      private:
        mutable mutex stateMutex;
        condition_variable stateChanged;
        map<OperationChainRunId, OperationChainRunSnapshot> snapshots;
        map<OperationChainRunId, vector<OperationChainRunSnapshot>> histories;

      public:
        void record(OperationChainRunSnapshot snapshot)
        {
            {
                lock_guard<mutex> lock(stateMutex);
                const auto current = snapshots.find(snapshot.runId);
                if (current == snapshots.end() || snapshot.updateSequence > current->second.updateSequence)
                {
                    histories[snapshot.runId].push_back(snapshot);
                    snapshots[snapshot.runId] = move(snapshot);
                }
            }
            stateChanged.notify_all();
        }

        OperationChainRunSnapshot waitForTerminal(OperationChainRunId runId)
        {
            return waitForSnapshot(runId,
                                   [](const OperationChainRunSnapshot &snapshot)
                                   { return isTerminal(snapshot.chainSnapshot.status); });
        }

        OperationChainRunSnapshot waitForSnapshot(OperationChainRunId runId,
                                                  const function<bool(const OperationChainRunSnapshot &)> &predicate)
        {
            unique_lock<mutex> lock(stateMutex);
            const bool completed =
                stateChanged.wait_for(lock,
                                      chrono::seconds(2),
                                      [this, runId, &predicate]()
                                      {
                                          const auto snapshot = snapshots.find(runId);
                                          return snapshot != snapshots.end() && predicate(snapshot->second);
                                      });
            EXPECT_TRUE(completed);
            return snapshots.at(runId);
        }

        vector<OperationChainRunSnapshot> getHistory(OperationChainRunId runId) const
        {
            lock_guard<mutex> lock(stateMutex);
            const auto history = histories.find(runId);
            return history == histories.end() ? vector<OperationChainRunSnapshot>{} : history->second;
        }
    };

    class SimulationGuardDealService final : public TestDealService
    {
      private:
        atomic<size_t> operationCalls{0};

        void recordOperationCall()
        {
            ++operationCalls;
        }

      public:
        explicit SimulationGuardDealService(ExchangerType exchangerType) : TestDealService(exchangerType) {}

        size_t getOperationCallCount() const
        {
            return operationCalls.load();
        }

        OrderInfo buyCrypto(const string &, const string &, Decimal) override
        {
            recordOperationCall();
            return {};
        }

        OrderInfo sellCrypto(const string &, const string &, Decimal) override
        {
            recordOperationCall();
            return {};
        }

        OrderInfo waitUntilOrderFilled(const string &, const string &) override
        {
            recordOperationCall();
            return {};
        }

        OrderInfo waitUntilOrderFilled(const string &, const string &, stop_token) override
        {
            recordOperationCall();
            return {};
        }

        OrderInfo placeOrder(const PlaceOrderRequest &) override
        {
            recordOperationCall();
            return {};
        }

        OrderInfo cancelOrderAndWaitUntilTerminal(const OrderQuery &) override
        {
            recordOperationCall();
            return {};
        }

        OrderInfo cancelOrderAndWaitUntilTerminal(const OrderQuery &, stop_token) override
        {
            recordOperationCall();
            return {};
        }

        OcoInfo placeOco(const PlaceOcoRequest &) override
        {
            recordOperationCall();
            return {};
        }

        OcoWaitResult waitUntilOcoOrderFilled(const OcoInfo &) override
        {
            recordOperationCall();
            return {};
        }

        OcoWaitResult waitUntilOcoOrderFilled(const OcoInfo &, stop_token) override
        {
            recordOperationCall();
            return {};
        }

        OcoInfo cancelOcoAndWaitUntilTerminal(const OcoInfo &ocoInfo) override
        {
            recordOperationCall();
            return ocoInfo;
        }

        OcoInfo cancelOcoAndWaitUntilTerminal(const OcoInfo &ocoInfo, stop_token) override
        {
            recordOperationCall();
            return ocoInfo;
        }
    };

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

    class CancellationDealService final : public TestDealService
    {
      public:
        using BuyAction = function<OrderInfo(const string &, const string &, Decimal)>;
        using OrderWaitAction = function<OrderInfo(const string &, const string &, stop_token)>;
        using OrderCancellationAction = function<OrderInfo(const OrderQuery &, stop_token)>;
        using OcoPlacementAction = function<OcoInfo(const PlaceOcoRequest &)>;
        using OcoWaitAction = function<OcoWaitResult(const OcoInfo &, stop_token)>;
        using OcoCancellationAction = function<OcoInfo(const OcoInfo &, stop_token)>;

        BuyAction buyAction;
        OrderWaitAction orderWaitAction;
        OrderCancellationAction orderCancellationAction;
        OcoPlacementAction ocoPlacementAction;
        OcoWaitAction ocoWaitAction;
        OcoCancellationAction ocoCancellationAction;
        atomic<size_t> buyCalls{0};
        atomic<size_t> orderCancellationCalls{0};
        atomic<size_t> ocoCancellationCalls{0};

        explicit CancellationDealService(ExchangerType exchangerType) : TestDealService(exchangerType) {}

        OrderInfo buyCrypto(const string &baseAsset, const string &quoteAsset, Decimal quantity) override
        {
            ++buyCalls;
            return buyAction ? buyAction(baseAsset, quoteAsset, quantity) : OrderInfo{};
        }

        OrderInfo waitUntilOrderFilled(const string &symbol, const string &orderId) override
        {
            return waitUntilOrderFilled(symbol, orderId, {});
        }

        OrderInfo waitUntilOrderFilled(const string &symbol, const string &orderId, stop_token stopToken) override
        {
            return orderWaitAction ? orderWaitAction(symbol, orderId, stopToken) : OrderInfo{};
        }

        OrderInfo cancelOrderAndWaitUntilTerminal(const OrderQuery &query) override
        {
            return cancelOrderAndWaitUntilTerminal(query, {});
        }

        OrderInfo cancelOrderAndWaitUntilTerminal(const OrderQuery &query, stop_token stopToken) override
        {
            ++orderCancellationCalls;
            return orderCancellationAction ? orderCancellationAction(query, stopToken) : OrderInfo{};
        }

        OcoInfo placeOco(const PlaceOcoRequest &request) override
        {
            return ocoPlacementAction ? ocoPlacementAction(request) : OcoInfo{};
        }

        OcoWaitResult waitUntilOcoOrderFilled(const OcoInfo &ocoInfo) override
        {
            return waitUntilOcoOrderFilled(ocoInfo, {});
        }

        OcoWaitResult waitUntilOcoOrderFilled(const OcoInfo &ocoInfo, stop_token stopToken) override
        {
            return ocoWaitAction ? ocoWaitAction(ocoInfo, stopToken) : OcoWaitResult{};
        }

        OcoInfo cancelOcoAndWaitUntilTerminal(const OcoInfo &ocoInfo) override
        {
            return cancelOcoAndWaitUntilTerminal(ocoInfo, {});
        }

        OcoInfo cancelOcoAndWaitUntilTerminal(const OcoInfo &ocoInfo, stop_token stopToken) override
        {
            ++ocoCancellationCalls;
            return ocoCancellationAction ? ocoCancellationAction(ocoInfo, stopToken) : OcoInfo{};
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

    OperationChainDefinition createOcoDefinition(string name, ExchangerType exchangerType = ExchangerType::BINANCE)
    {
        PlaceOcoConfig config;
        config.outAsset = "BTC";
        config.side = OrderOperation::BUY;
        config.price = Decimal{11};
        config.stopPrice = Decimal{9};
        return OperationChainDefinition(move(name),
                                        exchangerType,
                                        "USDT",
                                        Decimal{1},
                                        {{OperationType::PLACE_OCO, config}});
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

    OrderInfo createCancelledOrder(const string &symbol, const string &orderId, ExchangerType exchangerType)
    {
        OrderInfo order;
        order.symbol = symbol;
        order.orderId = orderId;
        order.origQty = Decimal{1};
        order.status = exchangerType == ExchangerType::BINANCE ? "CANCELED" : "Cancelled";
        return order;
    }

    OrderInfo waitUntilInterrupted(const string &, const string &, stop_token stopToken, latch &waitStarted)
    {
        mutex waitMutex;
        condition_variable_any waitChanged;
        unique_lock<mutex> lock(waitMutex);
        waitStarted.count_down();
        static_cast<void>(waitChanged.wait(lock, stopToken, []() { return false; }));
        throw OrderWaitInterrupted("Test order wait was interrupted");
    }

    OcoWaitResult waitUntilOcoInterrupted(const OcoInfo &, stop_token stopToken, latch &waitStarted)
    {
        mutex waitMutex;
        condition_variable_any waitChanged;
        unique_lock<mutex> lock(waitMutex);
        waitStarted.count_down();
        static_cast<void>(waitChanged.wait(lock, stopToken, []() { return false; }));
        throw OrderWaitInterrupted("Test OCO wait was interrupted");
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

TEST(OperationChainRunManagerTest, RunsDefaultSimulationsToDistinctAwaitingStepsWithoutExchangeCalls)
{
    auto binanceService = make_shared<SimulationGuardDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SimulationGuardDealService>(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Real chain", "BTC")}, binanceService, bybitService);
    RunObserver observer;
    manager.setRunChangeHandler([&observer](OperationChainRunSnapshot snapshot) { observer.record(move(snapshot)); });

    const vector<SimulatedOperationChainRunPlan> plans =
        createDefaultSimulatedOperationChainRunPlans(chrono::milliseconds{0}, chrono::milliseconds{0});
    vector<OperationChainRunId> runIds;
    for (const SimulatedOperationChainRunPlan &plan : plans)
    {
        runIds.push_back(manager.startSimulatedRun(plan));
    }

    const array<size_t, 3> awaitingIndexes{2, 3, 4};
    const array<string, 3> expectedNames{"Simulated Binance accumulation",
                                         "Simulated cross-exchange hedge",
                                         "Simulated Bybit protection"};
    const array<ExchangerType, 3> expectedCurrentExchangers{ExchangerType::BINANCE,
                                                            ExchangerType::BYBIT,
                                                            ExchangerType::BINANCE};
    const array<string, 3> expectedCurrentAssets{"USDT", "USDT", "BTC"};
    const array<string, 3> expectedCurrentQuantities{"2480", "4975", "0.05"};
    const array<vector<OperationType>, 3> expectedTypes{vector<OperationType>{OperationType::BUY_CRYPTO,
                                                                              OperationType::SELL_CRYPTO,
                                                                              OperationType::PLACE_ORDER,
                                                                              OperationType::PLACE_OCO,
                                                                              OperationType::SEND_TO},
                                                        vector<OperationType>{OperationType::BUY_CRYPTO,
                                                                              OperationType::SEND_TO,
                                                                              OperationType::SELL_CRYPTO,
                                                                              OperationType::PLACE_ORDER,
                                                                              OperationType::PLACE_OCO},
                                                        vector<OperationType>{OperationType::BUY_CRYPTO,
                                                                              OperationType::PLACE_OCO,
                                                                              OperationType::BUY_CRYPTO,
                                                                              OperationType::SEND_TO,
                                                                              OperationType::PLACE_OCO}};

    ASSERT_EQ(runIds, (vector<OperationChainRunId>{1, 2, 3}));
    ASSERT_EQ(plans.size(), size_t{3});
    for (size_t runIndex = 0; runIndex < runIds.size(); ++runIndex)
    {
        const OperationChainRunSnapshot snapshot = observer.waitForSnapshot(
            runIds[runIndex],
            [expectedIndex = awaitingIndexes[runIndex]](const OperationChainRunSnapshot &candidate)
            {
                return candidate.chainSnapshot.status == OperationChainStatus::RUNNING &&
                       candidate.chainSnapshot.currentStepIndex == expectedIndex &&
                       candidate.chainSnapshot.steps[expectedIndex].status == OperationStepStatus::AWAITING;
            });

        EXPECT_EQ(snapshot.kind, OperationChainRunKind::SIMULATED);
        EXPECT_EQ(snapshot.chainSnapshot.definitionName, expectedNames[runIndex]);
        ASSERT_EQ(snapshot.chainSnapshot.steps.size(), size_t{5});
        ASSERT_TRUE(snapshot.chainSnapshot.currentStepIndex.has_value());
        EXPECT_EQ(snapshot.chainSnapshot.currentStepIndex.value(), awaitingIndexes[runIndex]);
        EXPECT_EQ(snapshot.chainSnapshot.currentContext.exchangerType, expectedCurrentExchangers[runIndex]);
        EXPECT_EQ(snapshot.chainSnapshot.currentContext.asset, expectedCurrentAssets[runIndex]);
        EXPECT_EQ(snapshot.chainSnapshot.currentContext.quantity,
                  DecimalConverter::parseDecimal(expectedCurrentQuantities[runIndex]));

        for (size_t stepIndex = 0; stepIndex < snapshot.chainSnapshot.steps.size(); ++stepIndex)
        {
            const OperationStepSnapshot &step = snapshot.chainSnapshot.steps[stepIndex];
            EXPECT_EQ(step.type, expectedTypes[runIndex][stepIndex]);
            if (stepIndex < awaitingIndexes[runIndex])
            {
                EXPECT_EQ(step.status, OperationStepStatus::SUCCEEDED);
                EXPECT_TRUE(step.inputContext.has_value());
                EXPECT_TRUE(step.outputContext.has_value());
            }
            else if (stepIndex == awaitingIndexes[runIndex])
            {
                EXPECT_EQ(step.status, OperationStepStatus::AWAITING);
                EXPECT_TRUE(step.inputContext.has_value());
                EXPECT_FALSE(step.outputContext.has_value());
            }
            else
            {
                EXPECT_EQ(step.status, OperationStepStatus::PENDING);
                EXPECT_FALSE(step.inputContext.has_value());
                EXPECT_FALSE(step.outputContext.has_value());
            }
        }
    }

    const OperationChainRunSnapshot binanceRun = manager.getRun(runIds[0]).value();
    EXPECT_EQ(binanceRun.chainSnapshot.steps[0].acceptedIdentifiers.orderId, "SIM-BIN-BUY-001");
    EXPECT_EQ(binanceRun.chainSnapshot.steps[2].acceptedIdentifiers.orderId, "SIM-BIN-MARKET-003");
    const OperationChainRunSnapshot protectionRun = manager.getRun(runIds[2]).value();
    EXPECT_EQ(protectionRun.chainSnapshot.steps[1].acceptedIdentifiers.ocoGroupId, "SIM-PROTECT-OCO-002");
    EXPECT_EQ(protectionRun.chainSnapshot.steps[4].acceptedIdentifiers.takeProfitOrderId, "SIM-PROTECT-TP-005");
    const vector<OperationChainRunSnapshot> firstRunHistory = observer.getHistory(runIds[0]);
    ASSERT_EQ(firstRunHistory.size(), size_t{8});
    EXPECT_EQ(firstRunHistory[0].chainSnapshot.status, OperationChainStatus::PENDING);
    EXPECT_EQ(firstRunHistory[1].chainSnapshot.status, OperationChainStatus::RUNNING);
    EXPECT_FALSE(firstRunHistory[1].chainSnapshot.currentStepIndex.has_value());
    EXPECT_EQ(firstRunHistory[2].chainSnapshot.steps[0].status, OperationStepStatus::RUNNING);
    EXPECT_EQ(firstRunHistory[3].chainSnapshot.steps[0].status, OperationStepStatus::SUCCEEDED);
    EXPECT_EQ(firstRunHistory[4].chainSnapshot.steps[1].status, OperationStepStatus::RUNNING);
    EXPECT_EQ(firstRunHistory[5].chainSnapshot.steps[1].status, OperationStepStatus::SUCCEEDED);
    EXPECT_EQ(firstRunHistory[6].chainSnapshot.steps[2].status, OperationStepStatus::RUNNING);
    EXPECT_EQ(firstRunHistory[7].chainSnapshot.steps[2].status, OperationStepStatus::AWAITING);
    for (size_t eventIndex = 0; eventIndex < firstRunHistory.size(); ++eventIndex)
    {
        EXPECT_EQ(firstRunHistory[eventIndex].chainSnapshot.revision, eventIndex);
        if (eventIndex > 0)
        {
            EXPECT_GT(firstRunHistory[eventIndex].updateSequence, firstRunHistory[eventIndex - 1].updateSequence);
        }
    }
    ASSERT_EQ(manager.getDefinitions().size(), size_t{1});
    EXPECT_EQ(manager.getDefinitions().front().getName(), "Real chain");
    EXPECT_EQ(binanceService->getOperationCallCount(), size_t{0});
    EXPECT_EQ(bybitService->getOperationCallCount(), size_t{0});

    const OperationChainRunId realRunId = manager.startRun("Real chain");
    const OperationChainRunSnapshot realRun = observer.waitForTerminal(realRunId);
    EXPECT_EQ(realRunId, OperationChainRunId{4});
    EXPECT_EQ(realRun.kind, OperationChainRunKind::REAL);
    EXPECT_EQ(realRun.chainSnapshot.status, OperationChainStatus::COMPLETED);
    EXPECT_GT(binanceService->getOperationCallCount(), size_t{0});
    EXPECT_EQ(bybitService->getOperationCallCount(), size_t{0});

    manager.stopAndWait();
    for (OperationChainRunId runId : runIds)
    {
        const OperationChainRunSnapshot stoppedSnapshot = manager.getRun(runId).value();
        EXPECT_EQ(stoppedSnapshot.chainSnapshot.status, OperationChainStatus::RUNNING);
        EXPECT_FALSE(stoppedSnapshot.cancellationRequested);
    }
}

TEST(OperationChainRunManagerTest, CancelsOnlySelectedSimulationLocallyAndRetainsItsHistory)
{
    auto binanceService = make_shared<SimulationGuardDealService>(ExchangerType::BINANCE);
    auto bybitService = make_shared<SimulationGuardDealService>(ExchangerType::BYBIT);
    OperationChainRunManager manager({}, binanceService, bybitService);
    RunObserver observer;
    manager.setRunChangeHandler([&observer](OperationChainRunSnapshot snapshot) { observer.record(move(snapshot)); });

    vector<OperationChainRunId> runIds;
    for (const SimulatedOperationChainRunPlan &plan :
         createDefaultSimulatedOperationChainRunPlans(chrono::milliseconds{0}, chrono::milliseconds{0}))
    {
        runIds.push_back(manager.startSimulatedRun(plan));
    }
    for (size_t runIndex = 0; runIndex < runIds.size(); ++runIndex)
    {
        observer.waitForSnapshot(runIds[runIndex],
                                 [expectedIndex = runIndex + 2](const OperationChainRunSnapshot &candidate)
                                 {
                                     return candidate.chainSnapshot.currentStepIndex == expectedIndex &&
                                            candidate.chainSnapshot.steps[expectedIndex].status ==
                                                OperationStepStatus::AWAITING;
                                 });
    }

    manager.requestRunCancellation(runIds[1]);
    const OperationChainRunSnapshot cancelled = observer.waitForTerminal(runIds[1]);
    const uint64_t terminalUpdateSequence = cancelled.updateSequence;
    manager.requestRunCancellation(runIds[1]);

    EXPECT_TRUE(cancelled.cancellationRequested);
    EXPECT_EQ(cancelled.kind, OperationChainRunKind::SIMULATED);
    EXPECT_EQ(cancelled.chainSnapshot.status, OperationChainStatus::CANCELLED);
    EXPECT_FALSE(cancelled.chainSnapshot.currentStepIndex.has_value());
    EXPECT_EQ(cancelled.chainSnapshot.steps[3].status, OperationStepStatus::CANCELLED);
    EXPECT_EQ(manager.getRun(runIds[1])->updateSequence, terminalUpdateSequence);
    EXPECT_EQ(manager.getRuns().size(), size_t{3});
    EXPECT_EQ(manager.getRun(runIds[0])->chainSnapshot.steps[2].status, OperationStepStatus::AWAITING);
    EXPECT_EQ(manager.getRun(runIds[2])->chainSnapshot.steps[4].status, OperationStepStatus::AWAITING);
    EXPECT_EQ(binanceService->getOperationCallCount(), size_t{0});
    EXPECT_EQ(bybitService->getOperationCallCount(), size_t{0});

    manager.stopAndWait();
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

TEST(OperationChainRunManagerTest, CancelsPendingRunBeforeItsFirstRequest)
{
    auto binanceService = make_shared<CancellationDealService>(ExchangerType::BINANCE);
    binanceService->buyAction = [](const string &, const string &, Decimal) -> OrderInfo
    { throw runtime_error("Placement must not start"); };
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Pending", "BTC")}, binanceService, bybitService);
    RunObserver observer;
    manager.setRunChangeHandler(
        [&manager, &observer](OperationChainRunSnapshot snapshot)
        {
            const bool shouldCancel =
                snapshot.chainSnapshot.status == OperationChainStatus::PENDING && !snapshot.cancellationRequested;
            const OperationChainRunId runId = snapshot.runId;
            observer.record(move(snapshot));
            if (shouldCancel)
            {
                manager.requestRunCancellation(runId);
            }
        });

    const OperationChainRunId runId = manager.startRun("Pending");
    const OperationChainRunSnapshot terminal = observer.waitForTerminal(runId);
    manager.stopAndWait();

    EXPECT_EQ(binanceService->buyCalls.load(), 0);
    EXPECT_TRUE(terminal.cancellationRequested);
    EXPECT_EQ(terminal.chainSnapshot.status, OperationChainStatus::CANCELLED);
    EXPECT_EQ(terminal.chainSnapshot.steps[0].status, OperationStepStatus::PENDING);
    EXPECT_FALSE(terminal.chainSnapshot.currentStepIndex.has_value());
}

TEST(OperationChainRunManagerTest, CancelsAcceptedOrderAfterPlacementInFlight)
{
    latch placementStarted(1);
    binary_semaphore releasePlacement(0);
    latch waitStarted(1);
    auto binanceService = make_shared<CancellationDealService>(ExchangerType::BINANCE);
    binanceService->buyAction =
        [&placementStarted, &releasePlacement](const string &baseAsset, const string &quoteAsset, Decimal quantity)
    {
        placementStarted.count_down();
        releasePlacement.acquire();
        return createAcceptedOrder(baseAsset, quoteAsset, quantity, 1);
    };
    binanceService->orderWaitAction = [&waitStarted](const string &symbol, const string &orderId, stop_token stopToken)
    { return waitUntilInterrupted(symbol, orderId, stopToken, waitStarted); };
    binanceService->orderCancellationAction = [](const OrderQuery &query, stop_token)
    { return createCancelledOrder(query.symbol, query.orderId.value(), ExchangerType::BINANCE); };
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Placement", "BTC")}, binanceService, bybitService);
    RunObserver observer;
    manager.setRunChangeHandler([&observer](OperationChainRunSnapshot snapshot) { observer.record(move(snapshot)); });

    const OperationChainRunId runId = manager.startRun("Placement");
    placementStarted.wait();
    const OperationChainRunSnapshot beforeCancellation = manager.getRun(runId).value();
    manager.requestRunCancellation(runId);
    const OperationChainRunSnapshot cancellationRequested = manager.getRun(runId).value();
    releasePlacement.release();
    waitStarted.wait();
    const OperationChainRunSnapshot terminal = observer.waitForTerminal(runId);
    manager.stopAndWait();

    EXPECT_EQ(binanceService->orderCancellationCalls.load(), 1);
    EXPECT_GT(cancellationRequested.updateSequence, beforeCancellation.updateSequence);
    EXPECT_TRUE(cancellationRequested.cancellationRequested);
    EXPECT_EQ(cancellationRequested.chainSnapshot.status, OperationChainStatus::RUNNING);
    EXPECT_TRUE(terminal.cancellationRequested);
    EXPECT_EQ(terminal.chainSnapshot.status, OperationChainStatus::CANCELLED);
    EXPECT_EQ(terminal.chainSnapshot.steps[0].status, OperationStepStatus::CANCELLED);
    EXPECT_EQ(terminal.chainSnapshot.steps[0].acceptedIdentifiers.orderId, "run-order-1");
    EXPECT_FALSE(terminal.chainSnapshot.currentStepIndex.has_value());
}

TEST(OperationChainRunManagerTest, RepeatedAndTerminalCancellationRequestsAreHarmless)
{
    latch waitStarted(1);
    auto binanceService = make_shared<CancellationDealService>(ExchangerType::BINANCE);
    binanceService->buyAction = [](const string &baseAsset, const string &quoteAsset, Decimal quantity)
    { return createAcceptedOrder(baseAsset, quoteAsset, quantity, 1); };
    binanceService->orderWaitAction = [&waitStarted](const string &symbol, const string &orderId, stop_token stopToken)
    { return waitUntilInterrupted(symbol, orderId, stopToken, waitStarted); };
    binanceService->orderCancellationAction = [](const OrderQuery &query, stop_token)
    { return createCancelledOrder(query.symbol, query.orderId.value(), ExchangerType::BINANCE); };
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Repeated", "BTC")}, binanceService, bybitService);
    RunObserver observer;
    manager.setRunChangeHandler([&observer](OperationChainRunSnapshot snapshot) { observer.record(move(snapshot)); });

    const OperationChainRunId runId = manager.startRun("Repeated");
    waitStarted.wait();
    manager.requestRunCancellation(runId);
    manager.requestRunCancellation(runId);
    const OperationChainRunSnapshot terminal = observer.waitForTerminal(runId);
    manager.requestRunCancellation(runId);
    const OperationChainRunSnapshot stored = manager.getRun(runId).value();
    EXPECT_THROW(manager.requestRunCancellation(999), runtime_error);
    manager.stopAndWait();

    EXPECT_EQ(binanceService->orderCancellationCalls.load(), 1);
    EXPECT_EQ(stored.updateSequence, terminal.updateSequence);
    EXPECT_EQ(stored.chainSnapshot.status, OperationChainStatus::CANCELLED);
}

TEST(OperationChainRunManagerTest, TreatsTerminalWaitFailureAsConfirmedCancellation)
{
    latch waitStarted(1);
    mutex terminalMutex;
    condition_variable_any terminalChanged;
    bool cancellationPublished = false;
    auto binanceService = make_shared<CancellationDealService>(ExchangerType::BINANCE);
    binanceService->buyAction = [](const string &baseAsset, const string &quoteAsset, Decimal quantity)
    { return createAcceptedOrder(baseAsset, quoteAsset, quantity, 1); };
    binanceService->orderWaitAction =
        [&waitStarted, &terminalMutex, &terminalChanged, &cancellationPublished](const string &,
                                                                                 const string &,
                                                                                 stop_token stopToken) -> OrderInfo
    {
        unique_lock<mutex> lock(terminalMutex);
        waitStarted.count_down();
        const bool terminal =
            terminalChanged.wait(lock, stopToken, [&cancellationPublished]() { return cancellationPublished; });
        if (terminal)
        {
            throw runtime_error("Normal fill wait observed terminal CANCELED status");
        }
        throw OrderWaitInterrupted("Normal fill wait was interrupted");
    };
    binanceService->orderCancellationAction =
        [&terminalMutex, &terminalChanged, &cancellationPublished](const OrderQuery &query, stop_token)
    {
        {
            lock_guard<mutex> lock(terminalMutex);
            cancellationPublished = true;
        }
        terminalChanged.notify_all();
        return createCancelledOrder(query.symbol, query.orderId.value(), ExchangerType::BINANCE);
    };
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Terminal race", "BTC")}, binanceService, bybitService);
    RunObserver observer;
    manager.setRunChangeHandler([&observer](OperationChainRunSnapshot snapshot) { observer.record(move(snapshot)); });

    const OperationChainRunId runId = manager.startRun("Terminal race");
    waitStarted.wait();
    manager.requestRunCancellation(runId);
    const OperationChainRunSnapshot terminal = observer.waitForTerminal(runId);
    manager.stopAndWait();

    EXPECT_EQ(terminal.chainSnapshot.status, OperationChainStatus::CANCELLED);
    EXPECT_EQ(terminal.chainSnapshot.steps[0].status, OperationStepStatus::CANCELLED);
    EXPECT_TRUE(terminal.chainSnapshot.error.empty());
}

TEST(OperationChainRunManagerTest, RecordsExactOrderCancellationFailure)
{
    latch waitStarted(1);
    auto binanceService = make_shared<CancellationDealService>(ExchangerType::BINANCE);
    binanceService->buyAction = [](const string &baseAsset, const string &quoteAsset, Decimal quantity)
    { return createAcceptedOrder(baseAsset, quoteAsset, quantity, 1); };
    binanceService->orderWaitAction = [&waitStarted](const string &symbol, const string &orderId, stop_token stopToken)
    { return waitUntilInterrupted(symbol, orderId, stopToken, waitStarted); };
    binanceService->orderCancellationAction = [](const OrderQuery &, stop_token) -> OrderInfo
    { throw runtime_error("Exact cancellation failure"); };
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Failure", "BTC")}, binanceService, bybitService);
    RunObserver observer;
    manager.setRunChangeHandler([&observer](OperationChainRunSnapshot snapshot) { observer.record(move(snapshot)); });

    const OperationChainRunId runId = manager.startRun("Failure");
    waitStarted.wait();
    manager.requestRunCancellation(runId);
    const OperationChainRunSnapshot terminal = observer.waitForTerminal(runId);
    manager.stopAndWait();

    EXPECT_EQ(terminal.chainSnapshot.status, OperationChainStatus::FAILED);
    EXPECT_EQ(terminal.chainSnapshot.error, "Exact cancellation failure");
    EXPECT_EQ(terminal.chainSnapshot.steps[0].status, OperationStepStatus::FAILED);
    EXPECT_EQ(terminal.chainSnapshot.steps[0].error, terminal.chainSnapshot.error);
    EXPECT_FALSE(terminal.chainSnapshot.currentStepIndex.has_value());
}

TEST(OperationChainRunManagerTest, NaturalFillWinsCancellationRace)
{
    latch waitStarted(1);
    binary_semaphore releaseFill(0);
    auto binanceService = make_shared<CancellationDealService>(ExchangerType::BINANCE);
    binanceService->buyAction = [](const string &baseAsset, const string &quoteAsset, Decimal quantity)
    { return createAcceptedOrder(baseAsset, quoteAsset, quantity, 1); };
    binanceService->orderWaitAction =
        [&waitStarted, &releaseFill](const string &symbol, const string &orderId, stop_token)
    {
        waitStarted.count_down();
        releaseFill.acquire();
        return createFilledOrder(symbol, orderId);
    };
    binanceService->orderCancellationAction = [&releaseFill](const OrderQuery &query, stop_token)
    {
        releaseFill.release();
        return createFilledOrder(query.symbol, query.orderId.value());
    };
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Fill race", "BTC")}, binanceService, bybitService);
    RunObserver observer;
    manager.setRunChangeHandler([&observer](OperationChainRunSnapshot snapshot) { observer.record(move(snapshot)); });

    const OperationChainRunId runId = manager.startRun("Fill race");
    waitStarted.wait();
    manager.requestRunCancellation(runId);
    const OperationChainRunSnapshot terminal = observer.waitForTerminal(runId);
    manager.stopAndWait();

    EXPECT_TRUE(terminal.cancellationRequested);
    EXPECT_EQ(terminal.chainSnapshot.status, OperationChainStatus::COMPLETED);
    EXPECT_EQ(terminal.chainSnapshot.steps[0].status, OperationStepStatus::SUCCEEDED);
    EXPECT_EQ(terminal.chainSnapshot.currentContext.asset, "BTC");
}

TEST(OperationChainRunManagerTest, CancelsOcoOnlyAfterBothChildrenAreConfirmedCancelled)
{
    latch waitStarted(1);
    auto bybitService = make_shared<CancellationDealService>(ExchangerType::BYBIT);
    bybitService->ocoPlacementAction = [](const PlaceOcoRequest &request)
    {
        OcoInfo ocoInfo;
        ocoInfo.orderListId = "oco-group";
        ocoInfo.takeProfitOrder.symbol = request.symbol;
        ocoInfo.takeProfitOrder.orderId = "take-profit";
        ocoInfo.takeProfitOrder.status = "New";
        ocoInfo.stopLossOrder.symbol = request.symbol;
        ocoInfo.stopLossOrder.orderId = "stop-loss";
        ocoInfo.stopLossOrder.status = "New";
        return ocoInfo;
    };
    bybitService->ocoWaitAction = [&waitStarted](const OcoInfo &ocoInfo, stop_token stopToken)
    { return waitUntilOcoInterrupted(ocoInfo, stopToken, waitStarted); };
    bybitService->ocoCancellationAction = [](const OcoInfo &accepted, stop_token)
    {
        OcoInfo terminal = accepted;
        terminal.takeProfitOrder.status = "Cancelled";
        terminal.stopLossOrder.status = "Cancelled";
        return terminal;
    };
    auto binanceService = createImmediateService(ExchangerType::BINANCE);
    OperationChainRunManager manager({createOcoDefinition("OCO", ExchangerType::BYBIT)}, binanceService, bybitService);
    RunObserver observer;
    manager.setRunChangeHandler([&observer](OperationChainRunSnapshot snapshot) { observer.record(move(snapshot)); });

    const OperationChainRunId runId = manager.startRun("OCO");
    waitStarted.wait();
    manager.requestRunCancellation(runId);
    const OperationChainRunSnapshot terminal = observer.waitForTerminal(runId);
    manager.stopAndWait();

    EXPECT_EQ(bybitService->ocoCancellationCalls.load(), 1);
    EXPECT_EQ(terminal.chainSnapshot.status, OperationChainStatus::CANCELLED);
    EXPECT_EQ(terminal.chainSnapshot.steps[0].status, OperationStepStatus::CANCELLED);
    EXPECT_EQ(terminal.chainSnapshot.steps[0].acceptedIdentifiers.ocoGroupId, "oco-group");
    EXPECT_EQ(terminal.chainSnapshot.steps[0].acceptedIdentifiers.takeProfitOrderId, "take-profit");
    EXPECT_EQ(terminal.chainSnapshot.steps[0].acceptedIdentifiers.stopLossOrderId, "stop-loss");
}

TEST(OperationChainRunManagerTest, CancelsOnlyTheSelectedConcurrentRun)
{
    latch waitsStarted(2);
    mutex waitsMutex;
    condition_variable_any waitsChanged;
    set<string> filledOrderIds;
    auto binanceService = make_shared<CancellationDealService>(ExchangerType::BINANCE);
    binanceService->buyAction = [nextOrderNumber = make_shared<atomic<size_t>>(
                                     1)](const string &baseAsset, const string &quoteAsset, Decimal quantity)
    { return createAcceptedOrder(baseAsset, quoteAsset, quantity, nextOrderNumber->fetch_add(1)); };
    binanceService->orderWaitAction =
        [&waitsStarted, &waitsMutex, &waitsChanged, &filledOrderIds](const string &symbol,
                                                                     const string &orderId,
                                                                     stop_token stopToken)
    {
        unique_lock<mutex> lock(waitsMutex);
        waitsStarted.count_down();
        const bool filled =
            waitsChanged.wait(lock,
                              stopToken,
                              [&filledOrderIds, &orderId]() { return filledOrderIds.contains(orderId); });
        if (!filled)
        {
            throw OrderWaitInterrupted("Selected test wait was interrupted");
        }
        return createFilledOrder(symbol, orderId);
    };
    binanceService->orderCancellationAction = [](const OrderQuery &query, stop_token)
    { return createCancelledOrder(query.symbol, query.orderId.value(), ExchangerType::BINANCE); };
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Concurrent", "BTC")}, binanceService, bybitService);
    RunObserver observer;
    manager.setRunChangeHandler([&observer](OperationChainRunSnapshot snapshot) { observer.record(move(snapshot)); });

    const OperationChainRunId cancelledRunId = manager.startRun("Concurrent");
    const OperationChainRunId completedRunId = manager.startRun("Concurrent");
    waitsStarted.wait();
    const OperationChainRunSnapshot completedRunAwaiting = manager.getRun(completedRunId).value();
    const string completedOrderId = completedRunAwaiting.chainSnapshot.steps[0].acceptedIdentifiers.orderId.value();

    manager.requestRunCancellation(cancelledRunId);
    const OperationChainRunSnapshot cancelledRun = observer.waitForTerminal(cancelledRunId);
    {
        lock_guard<mutex> lock(waitsMutex);
        filledOrderIds.insert(completedOrderId);
    }
    waitsChanged.notify_all();
    const OperationChainRunSnapshot completedRun = observer.waitForTerminal(completedRunId);
    manager.stopAndWait();

    EXPECT_EQ(binanceService->orderCancellationCalls.load(), 1);
    EXPECT_EQ(cancelledRun.chainSnapshot.status, OperationChainStatus::CANCELLED);
    EXPECT_TRUE(cancelledRun.cancellationRequested);
    EXPECT_EQ(completedRun.chainSnapshot.status, OperationChainStatus::COMPLETED);
    EXPECT_FALSE(completedRun.cancellationRequested);
}

TEST(OperationChainRunManagerTest, ShutdownInterruptsCancellationWithoutReportingItAsConfirmed)
{
    latch orderWaitStarted(1);
    binary_semaphore releaseOrderWait(0);
    latch cancellationStarted(1);
    auto binanceService = make_shared<CancellationDealService>(ExchangerType::BINANCE);
    binanceService->buyAction = [](const string &baseAsset, const string &quoteAsset, Decimal quantity)
    { return createAcceptedOrder(baseAsset, quoteAsset, quantity, 1); };
    binanceService->orderWaitAction = [&orderWaitStarted,
                                       &releaseOrderWait](const string &, const string &, stop_token) -> OrderInfo
    {
        orderWaitStarted.count_down();
        releaseOrderWait.acquire();
        throw runtime_error("Shared stream stopped during shutdown");
    };
    binanceService->orderCancellationAction = [&cancellationStarted](const OrderQuery &,
                                                                     stop_token stopToken) -> OrderInfo
    {
        mutex cancellationMutex;
        condition_variable_any cancellationChanged;
        unique_lock<mutex> lock(cancellationMutex);
        cancellationStarted.count_down();
        static_cast<void>(cancellationChanged.wait(lock, stopToken, []() { return false; }));
        throw OrderWaitInterrupted("Cancellation confirmation stopped during shutdown");
    };
    auto bybitService = createImmediateService(ExchangerType::BYBIT);
    OperationChainRunManager manager({createBuyDefinition("Shutdown", "BTC")}, binanceService, bybitService);

    const OperationChainRunId runId = manager.startRun("Shutdown");
    orderWaitStarted.wait();
    manager.requestRunCancellation(runId);
    cancellationStarted.wait();
    manager.requestStop();
    releaseOrderWait.release();
    manager.stopAndWait();

    const OperationChainRunSnapshot run = manager.getRun(runId).value();
    EXPECT_TRUE(run.cancellationRequested);
    EXPECT_EQ(run.chainSnapshot.status, OperationChainStatus::FAILED);
    EXPECT_EQ(run.chainSnapshot.error, "Shared stream stopped during shutdown");
    EXPECT_NE(run.chainSnapshot.status, OperationChainStatus::CANCELLED);
}
