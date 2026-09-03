#include "../src/OperationChain/OperationChain.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <latch>
#include <memory>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {
    OperationChainClock createClock()
    {
        auto nextTick = std::make_shared<std::chrono::milliseconds::rep>(0);
        return [nextTick]()
        {
            const auto timePoint = OperationChainTimePoint{} + std::chrono::milliseconds(*nextTick);
            ++*nextTick;
            return timePoint;
        };
    }

    OperationDefinition createBaseDefinition(OperationType type, const std::string &outAsset)
    {
        return OperationDefinition(type, BaseConfig{outAsset});
    }

    OperationChainDefinition createDefinition(std::string name, std::vector<OperationDefinition> operations)
    {
        return OperationChainDefinition(std::move(name),
                                        ExchangerType::BINANCE,
                                        "USDT",
                                        Decimal{10},
                                        std::move(operations));
    }
}

TEST(OperationChainTest, PublishesSuccessfulStateInOrderWithTypedContextAndIdentifiers)
{
    std::vector<int> executionOrder;
    const operation firstOperation =
        [&executionOrder](OperationContext &context, const OperationProgressHandler &progressHandler)
    {
        executionOrder.push_back(1);
        EXPECT_EQ(context.exchangerType, ExchangerType::BINANCE);
        EXPECT_EQ(context.inAsset, "USDT");
        EXPECT_EQ(context.quantity, Decimal{10});

        OperationAcceptedIdentifiers identifiers;
        identifiers.orderId = "order-1";
        progressHandler(std::move(identifiers));
        context.inAsset = "BTC";
        context.quantity = Decimal{2};
    };
    const operation secondOperation = [&executionOrder](OperationContext &context, const OperationProgressHandler &)
    {
        executionOrder.push_back(2);
        EXPECT_EQ(context.exchangerType, ExchangerType::BINANCE);
        EXPECT_EQ(context.inAsset, "BTC");
        EXPECT_EQ(context.quantity, Decimal{2});

        context.exchangerType = ExchangerType::BYBIT;
        context.inAsset = "ETH";
        context.quantity = Decimal{3};
    };
    SendToConfig sendToConfig;
    sendToConfig.destinationExchanger = ExchangerType::BYBIT;
    sendToConfig.chain = "TESTNET";
    sendToConfig.address = "test-address";
    const OperationChainDefinition definition =
        createDefinition("Observable chain",
                         {createBaseDefinition(OperationType::BUY_CRYPTO, "BTC"),
                          OperationDefinition(OperationType::SEND_TO, sendToConfig)});
    OperationChain chain(definition, {firstOperation, secondOperation}, {}, createClock());

    const OperationChainSnapshot initialSnapshot = chain.getSnapshot();
    EXPECT_EQ(initialSnapshot.definitionName, "Observable chain");
    EXPECT_EQ(initialSnapshot.status, OperationChainStatus::PENDING);
    EXPECT_EQ(initialSnapshot.revision, 0);
    EXPECT_EQ(initialSnapshot.initialContext, (OperationContextSnapshot{ExchangerType::BINANCE, "USDT", Decimal{10}}));
    EXPECT_EQ(initialSnapshot.currentContext, initialSnapshot.initialContext);
    EXPECT_FALSE(initialSnapshot.startedAt.has_value());
    EXPECT_FALSE(initialSnapshot.finishedAt.has_value());
    ASSERT_EQ(initialSnapshot.steps.size(), 2);
    EXPECT_EQ(initialSnapshot.steps[0].status, OperationStepStatus::PENDING);
    EXPECT_EQ(initialSnapshot.steps[0].type, OperationType::BUY_CRYPTO);
    EXPECT_EQ(std::get<BaseConfig>(initialSnapshot.steps[0].config).outAsset, "BTC");
    EXPECT_EQ(initialSnapshot.steps[1].type, OperationType::SEND_TO);

    std::vector<OperationChainSnapshot> events;
    chain.execute([&events](OperationChainSnapshot snapshot) { events.push_back(std::move(snapshot)); });

    EXPECT_EQ(executionOrder, (std::vector<int>{1, 2}));
    ASSERT_EQ(events.size(), 7);
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        EXPECT_EQ(events[index].revision, index + 1);
        if (index > 0)
        {
            EXPECT_LT(events[index - 1].updatedAt, events[index].updatedAt);
        }
    }
    EXPECT_EQ(events[0].status, OperationChainStatus::RUNNING);
    EXPECT_EQ(events[1].steps[0].status, OperationStepStatus::RUNNING);
    EXPECT_EQ(events[2].steps[0].status, OperationStepStatus::AWAITING);
    EXPECT_EQ(events[2].steps[0].acceptedIdentifiers.orderId, "order-1");
    EXPECT_EQ(events[3].steps[0].status, OperationStepStatus::SUCCEEDED);
    EXPECT_EQ(events[4].steps[1].status, OperationStepStatus::RUNNING);
    EXPECT_EQ(events[5].steps[1].status, OperationStepStatus::SUCCEEDED);
    EXPECT_EQ(events[6].status, OperationChainStatus::COMPLETED);

    const OperationChainSnapshot finalSnapshot = chain.getSnapshot();
    EXPECT_EQ(finalSnapshot.status, OperationChainStatus::COMPLETED);
    EXPECT_FALSE(finalSnapshot.currentStepIndex.has_value());
    EXPECT_TRUE(finalSnapshot.startedAt.has_value());
    EXPECT_TRUE(finalSnapshot.finishedAt.has_value());
    EXPECT_EQ(finalSnapshot.currentContext, (OperationContextSnapshot{ExchangerType::BYBIT, "ETH", Decimal{3}}));
    EXPECT_EQ(finalSnapshot.steps[0].inputContext,
              (OperationContextSnapshot{ExchangerType::BINANCE, "USDT", Decimal{10}}));
    EXPECT_EQ(finalSnapshot.steps[0].outputContext,
              (OperationContextSnapshot{ExchangerType::BINANCE, "BTC", Decimal{2}}));
    EXPECT_EQ(finalSnapshot.steps[1].inputContext,
              (OperationContextSnapshot{ExchangerType::BINANCE, "BTC", Decimal{2}}));
    EXPECT_EQ(finalSnapshot.steps[1].outputContext, finalSnapshot.currentContext);
    EXPECT_TRUE(finalSnapshot.steps[0].startedAt.has_value());
    EXPECT_TRUE(finalSnapshot.steps[0].finishedAt.has_value());
    EXPECT_TRUE(finalSnapshot.steps[1].startedAt.has_value());
    EXPECT_TRUE(finalSnapshot.steps[1].finishedAt.has_value());
}

TEST(OperationChainTest, RecordsFailedStepAndLeavesLaterStepsPendingBeforeRethrowing)
{
    std::vector<int> executionOrder;
    const operation firstOperation = [&executionOrder](OperationContext &context, const OperationProgressHandler &)
    {
        executionOrder.push_back(1);
        context.inAsset = "BTC";
        context.quantity = Decimal{2};
    };
    const operation failingOperation =
        [&executionOrder](OperationContext &, const OperationProgressHandler &progressHandler)
    {
        executionOrder.push_back(2);
        OperationAcceptedIdentifiers identifiers;
        identifiers.orderId = "accepted-before-failure";
        progressHandler(std::move(identifiers));
        throw std::runtime_error("Operation failed");
    };
    const operation skippedOperation = [&executionOrder](OperationContext &, const OperationProgressHandler &)
    { executionOrder.push_back(3); };
    const OperationChainDefinition definition =
        createDefinition("Failing chain",
                         {createBaseDefinition(OperationType::BUY_CRYPTO, "BTC"),
                          createBaseDefinition(OperationType::SELL_CRYPTO, "USDT"),
                          createBaseDefinition(OperationType::BUY_CRYPTO, "ETH")});
    OperationChain chain(definition, {firstOperation, failingOperation, skippedOperation}, {}, createClock());
    std::vector<OperationChainSnapshot> events;

    EXPECT_THROW(chain.execute([&events](OperationChainSnapshot snapshot) { events.push_back(std::move(snapshot)); }),
                 std::runtime_error);

    EXPECT_EQ(executionOrder, (std::vector<int>{1, 2}));
    const OperationChainSnapshot snapshot = chain.getSnapshot();
    EXPECT_EQ(snapshot.status, OperationChainStatus::FAILED);
    EXPECT_EQ(snapshot.error, "Operation failed");
    EXPECT_FALSE(snapshot.currentStepIndex.has_value());
    EXPECT_EQ(snapshot.currentContext, (OperationContextSnapshot{ExchangerType::BINANCE, "BTC", Decimal{2}}));
    EXPECT_EQ(snapshot.steps[0].status, OperationStepStatus::SUCCEEDED);
    EXPECT_EQ(snapshot.steps[1].status, OperationStepStatus::FAILED);
    EXPECT_EQ(snapshot.steps[1].error, "Operation failed");
    EXPECT_EQ(snapshot.steps[1].acceptedIdentifiers.orderId, "accepted-before-failure");
    EXPECT_TRUE(snapshot.steps[1].inputContext.has_value());
    EXPECT_FALSE(snapshot.steps[1].outputContext.has_value());
    EXPECT_EQ(snapshot.steps[2].status, OperationStepStatus::PENDING);
    EXPECT_FALSE(snapshot.steps[2].startedAt.has_value());
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().status, OperationChainStatus::FAILED);
    EXPECT_EQ(events.back().steps[1].status, OperationStepStatus::FAILED);
}

TEST(OperationChainTest, PublishesOcoGroupAndChildIdentifiersWhileAwaiting)
{
    const operation ocoOperation = [](OperationContext &context, const OperationProgressHandler &progressHandler)
    {
        OperationAcceptedIdentifiers identifiers;
        identifiers.ocoGroupId = "oco-group";
        identifiers.takeProfitOrderId = "take-profit";
        identifiers.stopLossOrderId = "stop-loss";
        progressHandler(std::move(identifiers));
        context.inAsset = "BTC";
        context.quantity = Decimal{2};
    };
    PlaceOcoConfig config;
    config.outAsset = "BTC";
    config.side = OrderOperation::BUY;
    config.price = Decimal{11};
    config.stopPrice = Decimal{9};
    const OperationChainDefinition definition =
        createDefinition("OCO identifiers", {OperationDefinition(OperationType::PLACE_OCO, config)});
    OperationChain chain(definition, {ocoOperation}, {}, createClock());
    std::optional<OperationChainSnapshot> awaitingSnapshot;

    chain.execute(
        [&awaitingSnapshot](OperationChainSnapshot snapshot)
        {
            if (snapshot.steps[0].status == OperationStepStatus::AWAITING)
            {
                awaitingSnapshot = std::move(snapshot);
            }
        });

    ASSERT_TRUE(awaitingSnapshot.has_value());
    const OperationAcceptedIdentifiers &identifiers = awaitingSnapshot->steps[0].acceptedIdentifiers;
    EXPECT_EQ(identifiers.ocoGroupId, "oco-group");
    EXPECT_EQ(identifiers.takeProfitOrderId, "take-profit");
    EXPECT_EQ(identifiers.stopLossOrderId, "stop-loss");
    EXPECT_FALSE(identifiers.orderId.has_value());
    EXPECT_EQ(chain.getSnapshot().status, OperationChainStatus::COMPLETED);
}

TEST(OperationChainTest, RecordsSafeMessageForNonStandardFailure)
{
    const operation failingOperation = [](OperationContext &, const OperationProgressHandler &) { throw 42; };
    const OperationChainDefinition definition =
        createDefinition("Non-standard failure", {createBaseDefinition(OperationType::BUY_CRYPTO, "BTC")});
    OperationChain chain(definition, {failingOperation}, {}, createClock());

    EXPECT_ANY_THROW(chain.execute());

    const OperationChainSnapshot snapshot = chain.getSnapshot();
    EXPECT_EQ(snapshot.status, OperationChainStatus::FAILED);
    EXPECT_EQ(snapshot.error, "Operation failed with a non-standard exception");
    EXPECT_EQ(snapshot.steps[0].error, snapshot.error);
}

TEST(OperationChainTest, ReturnsDetachedSnapshotsAndRejectsRepeatedExecution)
{
    const operation successfulOperation = [](OperationContext &context, const OperationProgressHandler &)
    { context.inAsset = "BTC"; };
    const OperationChainDefinition definition =
        createDefinition("Single-use chain", {createBaseDefinition(OperationType::BUY_CRYPTO, "BTC")});
    OperationChain chain(definition, {successfulOperation}, {}, createClock());

    OperationChainSnapshot detachedSnapshot = chain.getSnapshot();
    detachedSnapshot.status = OperationChainStatus::FAILED;
    detachedSnapshot.steps.clear();

    EXPECT_EQ(chain.getSnapshot().status, OperationChainStatus::PENDING);
    EXPECT_EQ(chain.getSnapshot().steps.size(), 1);
    EXPECT_NO_THROW(chain.execute());
    EXPECT_THROW(chain.execute(), std::runtime_error);
    EXPECT_EQ(chain.getSnapshot().status, OperationChainStatus::COMPLETED);
}

TEST(OperationChainTest, SupportsConcurrentReadersDuringAwaitingStep)
{
    std::latch awaitingStarted(1);
    std::binary_semaphore releaseOperation(0);
    const operation blockingOperation =
        [&awaitingStarted, &releaseOperation](OperationContext &context,
                                              const OperationProgressHandler &progressHandler)
    {
        OperationAcceptedIdentifiers identifiers;
        identifiers.orderId = "waiting-order";
        progressHandler(std::move(identifiers));
        awaitingStarted.count_down();
        releaseOperation.acquire();
        context.inAsset = "BTC";
    };
    const OperationChainDefinition definition =
        createDefinition("Concurrent readers", {createBaseDefinition(OperationType::BUY_CRYPTO, "BTC")});
    OperationChain chain(definition, {blockingOperation}, {}, createClock());
    std::jthread executionThread([&chain]() { chain.execute(); });
    awaitingStarted.wait();

    std::atomic<bool> snapshotsValid = true;
    std::vector<std::jthread> readers;
    for (int readerIndex = 0; readerIndex < 8; ++readerIndex)
    {
        readers.emplace_back(
            [&chain, &snapshotsValid]()
            {
                for (int readIndex = 0; readIndex < 500; ++readIndex)
                {
                    const OperationChainSnapshot snapshot = chain.getSnapshot();
                    if (snapshot.status != OperationChainStatus::RUNNING || snapshot.currentStepIndex != 0 ||
                        snapshot.steps.size() != 1 || snapshot.steps[0].status != OperationStepStatus::AWAITING ||
                        snapshot.steps[0].acceptedIdentifiers.orderId != "waiting-order")
                    {
                        snapshotsValid = false;
                    }
                }
            });
    }
    readers.clear();

    EXPECT_TRUE(snapshotsValid.load());
    releaseOperation.release();
    executionThread.join();
    EXPECT_EQ(chain.getSnapshot().status, OperationChainStatus::COMPLETED);
}

TEST(OperationChainTest, ObserverFailureIsReportedWithoutChangingExecution)
{
    const operation successfulOperation = [](OperationContext &context, const OperationProgressHandler &)
    { context.inAsset = "BTC"; };
    const OperationChainDefinition definition =
        createDefinition("Observer failure", {createBaseDefinition(OperationType::BUY_CRYPTO, "BTC")});
    OperationChain chain(definition, {successfulOperation}, {}, createClock());

    testing::internal::CaptureStderr();
    chain.execute([](OperationChainSnapshot) { throw std::runtime_error("observer error"); });
    const std::string errorOutput = testing::internal::GetCapturedStderr();

    EXPECT_NE(errorOutput.find("Operation-chain state observer failed: observer error"), std::string::npos);
    EXPECT_EQ(chain.getSnapshot().status, OperationChainStatus::COMPLETED);
}

TEST(OperationChainTest, CancelsPendingExecutionWithoutStartingAnOperation)
{
    std::atomic<bool> operationStarted = false;
    const operation skippedOperation = [&operationStarted](OperationContext &, const OperationProgressHandler &)
    { operationStarted = true; };
    const OperationChainDefinition definition =
        createDefinition("Pending cancellation", {createBaseDefinition(OperationType::BUY_CRYPTO, "BTC")});
    auto cancellationCoordinator = std::make_shared<OperationCancellationCoordinator>();
    OperationChain chain(definition, {skippedOperation}, {}, createClock(), cancellationCoordinator);
    cancellationCoordinator->requestCancellation();

    std::vector<OperationChainSnapshot> events;
    chain.execute([&events](OperationChainSnapshot snapshot) { events.push_back(std::move(snapshot)); });

    EXPECT_FALSE(operationStarted.load());
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events.front().status, OperationChainStatus::CANCELLED);
    EXPECT_EQ(events.front().revision, 1);
    EXPECT_FALSE(events.front().startedAt.has_value());
    EXPECT_TRUE(events.front().finishedAt.has_value());
    EXPECT_EQ(events.front().steps[0].status, OperationStepStatus::PENDING);
    EXPECT_FALSE(events.front().currentStepIndex.has_value());
}

TEST(OperationChainTest, CancelsCurrentStepBeforeItsRequest)
{
    std::atomic<bool> requestStarted = false;
    const operation cancellableOperation =
        [&requestStarted](OperationContext &context, const OperationProgressHandler &)
    {
        context.cancellationCoordinator->throwIfCancellationRequested();
        requestStarted = true;
    };
    const operation skippedOperation = [](OperationContext &, const OperationProgressHandler &) {};
    const OperationChainDefinition definition =
        createDefinition("Pre-request cancellation",
                         {createBaseDefinition(OperationType::BUY_CRYPTO, "BTC"),
                          createBaseDefinition(OperationType::SELL_CRYPTO, "USDT")});
    auto cancellationCoordinator = std::make_shared<OperationCancellationCoordinator>();
    OperationChain chain(definition,
                         {cancellableOperation, skippedOperation},
                         {},
                         createClock(),
                         cancellationCoordinator);

    chain.execute(
        [&cancellationCoordinator](OperationChainSnapshot snapshot)
        {
            if (snapshot.steps[0].status == OperationStepStatus::RUNNING)
            {
                cancellationCoordinator->requestCancellation();
            }
        });

    const OperationChainSnapshot snapshot = chain.getSnapshot();
    EXPECT_FALSE(requestStarted.load());
    EXPECT_EQ(snapshot.status, OperationChainStatus::CANCELLED);
    EXPECT_EQ(snapshot.steps[0].status, OperationStepStatus::CANCELLED);
    EXPECT_TRUE(snapshot.steps[0].startedAt.has_value());
    EXPECT_TRUE(snapshot.steps[0].finishedAt.has_value());
    EXPECT_EQ(snapshot.steps[1].status, OperationStepStatus::PENDING);
    EXPECT_FALSE(snapshot.currentStepIndex.has_value());
}

TEST(OperationChainTest, CancelsBetweenStepsAndPreservesCompletedWork)
{
    std::vector<int> executionOrder;
    const operation firstOperation = [&executionOrder](OperationContext &context, const OperationProgressHandler &)
    {
        executionOrder.push_back(1);
        context.inAsset = "BTC";
    };
    const operation secondOperation = [&executionOrder](OperationContext &, const OperationProgressHandler &)
    { executionOrder.push_back(2); };
    const OperationChainDefinition definition =
        createDefinition("Between-step cancellation",
                         {createBaseDefinition(OperationType::BUY_CRYPTO, "BTC"),
                          createBaseDefinition(OperationType::SELL_CRYPTO, "USDT")});
    auto cancellationCoordinator = std::make_shared<OperationCancellationCoordinator>();
    OperationChain chain(definition, {firstOperation, secondOperation}, {}, createClock(), cancellationCoordinator);

    chain.execute(
        [&cancellationCoordinator](OperationChainSnapshot snapshot)
        {
            if (snapshot.steps[0].status == OperationStepStatus::SUCCEEDED)
            {
                cancellationCoordinator->requestCancellation();
            }
        });

    const OperationChainSnapshot snapshot = chain.getSnapshot();
    EXPECT_EQ(executionOrder, (std::vector<int>{1}));
    EXPECT_EQ(snapshot.status, OperationChainStatus::CANCELLED);
    EXPECT_EQ(snapshot.steps[0].status, OperationStepStatus::SUCCEEDED);
    EXPECT_EQ(snapshot.steps[1].status, OperationStepStatus::PENDING);
    EXPECT_EQ(snapshot.currentContext.asset, "BTC");
    EXPECT_FALSE(snapshot.currentStepIndex.has_value());
}

TEST(OperationChainTest, LetsFinalNonCancellableOperationFinishNaturally)
{
    std::latch operationStarted(1);
    std::binary_semaphore releaseOperation(0);
    const operation blockingOperation =
        [&operationStarted, &releaseOperation](OperationContext &context, const OperationProgressHandler &)
    {
        operationStarted.count_down();
        releaseOperation.acquire();
        context.exchangerType = ExchangerType::BYBIT;
    };
    SendToConfig config;
    config.destinationExchanger = ExchangerType::BYBIT;
    config.chain = "TESTNET";
    config.address = "address";
    const OperationChainDefinition definition =
        createDefinition("Final non-cancellable operation", {OperationDefinition(OperationType::SEND_TO, config)});
    auto cancellationCoordinator = std::make_shared<OperationCancellationCoordinator>();
    OperationChain chain(definition, {blockingOperation}, {}, createClock(), cancellationCoordinator);
    std::jthread worker([&chain]() { chain.execute(); });
    operationStarted.wait();

    cancellationCoordinator->requestCancellation();
    releaseOperation.release();
    worker.join();

    const OperationChainSnapshot snapshot = chain.getSnapshot();
    EXPECT_EQ(snapshot.status, OperationChainStatus::COMPLETED);
    EXPECT_EQ(snapshot.steps[0].status, OperationStepStatus::SUCCEEDED);
    EXPECT_EQ(snapshot.currentContext.exchangerType, ExchangerType::BYBIT);
    EXPECT_FALSE(snapshot.currentStepIndex.has_value());
}
