#include "graphical/async/AsyncTaskExecutor.hpp"
#include "graphical/async/UiTaskState.hpp"

#include <QApplication>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QtTest/QTest>
#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <vector>

using namespace std;

namespace {
    QApplication &getApplication()
    {
        auto *existingApplication = qobject_cast<QApplication *>(QApplication::instance());
        if (existingApplication != nullptr)
        {
            return *existingApplication;
        }

        static int argumentCount = 1;
        static char applicationName[] = "AsyncTaskExecutorTests";
        static char *arguments[]{applicationName, nullptr};
        static QApplication application(argumentCount, arguments);
        return application;
    }
}

TEST(AsyncTaskExecutorTest, DeliversSuccessfulResultAndStateOnGuiThread)
{
    QApplication &application = getApplication();
    AsyncTaskExecutor executor;
    UiTaskState state;
    QObject receiver;
    vector<UiTaskState::Status> statuses;
    int result = 0;
    QThread *callbackThread = nullptr;

    QObject::connect(&state,
                     &UiTaskState::statusChanged,
                     &receiver,
                     [&statuses](UiTaskState::Status status) { statuses.push_back(status); });

    EXPECT_TRUE(executor.startTask(
        state,
        receiver,
        []() { return 42; },
        [&result, &callbackThread](int value)
        {
            result = value;
            callbackThread = QThread::currentThread();
        }));
    EXPECT_TRUE(state.isLoading());

    QTRY_COMPARE_WITH_TIMEOUT(result, 42, 1000);
    EXPECT_EQ(state.getStatus(), UiTaskState::Status::SUCCEEDED);
    EXPECT_TRUE(state.getError().isEmpty());
    EXPECT_EQ(callbackThread, application.thread());
    EXPECT_EQ(statuses, (vector{UiTaskState::Status::LOADING, UiTaskState::Status::SUCCEEDED}));
    EXPECT_EQ(executor.getActiveTaskCount(), 0);
}

TEST(AsyncTaskExecutorTest, DeliversExactFailureAndClearsItBeforeRetry)
{
    getApplication();
    AsyncTaskExecutor executor;
    UiTaskState state;
    QObject receiver;
    QString deliveredError;
    bool retriedSuccessfully = false;

    EXPECT_TRUE(executor.startTask(
        state,
        receiver,
        []() -> int { throw runtime_error("representative task failure"); },
        [](int) {},
        [&deliveredError](const QString &error) { deliveredError = error; }));

    QTRY_COMPARE_WITH_TIMEOUT(state.getStatus(), UiTaskState::Status::FAILED, 1000);
    EXPECT_EQ(state.getError(), QString("representative task failure"));
    EXPECT_EQ(deliveredError, state.getError());

    EXPECT_TRUE(executor.startTask(state, receiver, []() {}, [&retriedSuccessfully]() { retriedSuccessfully = true; }));
    EXPECT_TRUE(state.isLoading());
    EXPECT_TRUE(state.getError().isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(retriedSuccessfully, 1000);
    EXPECT_EQ(state.getStatus(), UiTaskState::Status::SUCCEEDED);
}

TEST(AsyncTaskExecutorTest, KeepsEventLoopResponsiveDuringIndependentOverlappingTasks)
{
    getApplication();
    AsyncTaskExecutor executor;
    UiTaskState firstState;
    UiTaskState secondState;
    QObject receiver;
    QPushButton firstAction;
    promise<void> releasePromise;
    shared_future<void> release = releasePromise.get_future().share();
    atomic<int> startedTaskCount = 0;
    bool eventProcessed = false;
    int firstResult = 0;
    int secondResult = 0;

    QObject::connect(&firstState,
                     &UiTaskState::statusChanged,
                     &firstAction,
                     [&firstAction](UiTaskState::Status status)
                     { firstAction.setEnabled(status != UiTaskState::Status::LOADING); });

    EXPECT_TRUE(executor.startTask(
        firstState,
        receiver,
        [&startedTaskCount, release]()
        {
            ++startedTaskCount;
            release.wait();
            return 1;
        },
        [&firstResult](int result) { firstResult = result; }));
    EXPECT_TRUE(executor.startTask(
        secondState,
        receiver,
        [&startedTaskCount, release]()
        {
            ++startedTaskCount;
            release.wait();
            return 2;
        },
        [&secondResult](int result) { secondResult = result; }));

    QTRY_COMPARE_WITH_TIMEOUT(startedTaskCount.load(), 2, 1000);
    EXPECT_TRUE(firstState.isLoading());
    EXPECT_TRUE(secondState.isLoading());
    EXPECT_FALSE(firstAction.isEnabled());
    EXPECT_EQ(executor.getActiveTaskCount(), 2);

    QTimer::singleShot(0, &receiver, [&eventProcessed]() { eventProcessed = true; });
    QTRY_VERIFY_WITH_TIMEOUT(eventProcessed, 1000);
    EXPECT_EQ(executor.getActiveTaskCount(), 2);

    releasePromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(firstResult, 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(secondResult, 2, 1000);
    EXPECT_EQ(firstState.getStatus(), UiTaskState::Status::SUCCEEDED);
    EXPECT_EQ(secondState.getStatus(), UiTaskState::Status::SUCCEEDED);
    EXPECT_TRUE(firstAction.isEnabled());
}

TEST(AsyncTaskExecutorTest, RejectsDuplicateSubmissionThroughLoadingState)
{
    getApplication();
    AsyncTaskExecutor executor;
    UiTaskState state;
    QObject receiver;
    promise<void> releasePromise;
    shared_future<void> release = releasePromise.get_future().share();
    atomic<bool> firstTaskStarted = false;
    bool duplicateWorkExecuted = false;
    bool firstTaskFinished = false;

    EXPECT_TRUE(executor.startTask(
        state,
        receiver,
        [&firstTaskStarted, release]()
        {
            firstTaskStarted = true;
            release.wait();
        },
        [&firstTaskFinished]() { firstTaskFinished = true; }));
    QTRY_VERIFY_WITH_TIMEOUT(firstTaskStarted.load(), 1000);

    EXPECT_FALSE(
        executor.startTask(state, receiver, [&duplicateWorkExecuted]() { duplicateWorkExecuted = true; }, []() {}));
    EXPECT_FALSE(duplicateWorkExecuted);
    EXPECT_EQ(executor.getActiveTaskCount(), 1);

    releasePromise.set_value();
    QTRY_VERIFY_WITH_TIMEOUT(firstTaskFinished, 1000);
}

TEST(AsyncTaskExecutorTest, SuppressesCallbackAfterReceiverDestruction)
{
    getApplication();
    AsyncTaskExecutor executor;
    UiTaskState state;
    auto receiver = make_unique<QObject>();
    promise<void> releasePromise;
    shared_future<void> release = releasePromise.get_future().share();
    atomic<bool> taskStarted = false;
    bool callbackExecuted = false;

    EXPECT_TRUE(executor.startTask(
        state,
        *receiver,
        [&taskStarted, release]()
        {
            taskStarted = true;
            release.wait();
            return 7;
        },
        [&callbackExecuted](int) { callbackExecuted = true; }));
    QTRY_VERIFY_WITH_TIMEOUT(taskStarted.load(), 1000);

    receiver.reset();
    releasePromise.set_value();
    QTRY_COMPARE_WITH_TIMEOUT(executor.getActiveTaskCount(), 0, 1000);

    EXPECT_FALSE(callbackExecuted);
    EXPECT_EQ(state.getStatus(), UiTaskState::Status::SUCCEEDED);
}

TEST(AsyncTaskExecutorTest, RequestsCooperativeStopAndJoinsWorkers)
{
    getApplication();
    AsyncTaskExecutor executor;
    UiTaskState state;
    QObject receiver;
    mutex waitMutex;
    condition_variable_any stopCondition;
    atomic<bool> taskStarted = false;
    bool callbackExecuted = false;

    EXPECT_TRUE(executor.startTask(
        state,
        receiver,
        [&waitMutex, &stopCondition, &taskStarted](stop_token stopToken)
        {
            unique_lock lock(waitMutex);
            taskStarted = true;
            stopCondition.wait(lock, stopToken, []() { return false; });
        },
        [&callbackExecuted]() { callbackExecuted = true; },
        [&callbackExecuted](const QString &) { callbackExecuted = true; }));
    QTRY_VERIFY_WITH_TIMEOUT(taskStarted.load(), 1000);

    executor.requestStop();
    EXPECT_TRUE(executor.isStopping());
    EXPECT_FALSE(executor.startTask(state, receiver, []() {}, []() {}));

    executor.stopAndWait();

    EXPECT_EQ(executor.getActiveTaskCount(), 0);
    EXPECT_FALSE(callbackExecuted);
}
