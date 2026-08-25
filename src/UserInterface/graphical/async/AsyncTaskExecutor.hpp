#ifndef ASYNC_TASK_EXECUTOR_H
#define ASYNC_TASK_EXECUTOR_H

#include "UiTaskState.hpp"
#include "detail/TaskCompletion.hpp"

#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

class AsyncTaskExecutor final : public QObject
{
  private:
    struct TaskRecord
    {
        std::unique_ptr<AsyncTaskDetail::TaskCompletionBase> completion;
        std::jthread worker;

        explicit TaskRecord(std::unique_ptr<AsyncTaskDetail::TaskCompletionBase> completion)
            : completion(std::move(completion))
        {}

        TaskRecord(TaskRecord &&) = default;

        TaskRecord &operator=(TaskRecord &&) = default;

        TaskRecord(const TaskRecord &) = delete;

        TaskRecord &operator=(const TaskRecord &) = delete;
    };

    std::map<std::uint64_t, TaskRecord> tasks;
    std::atomic<bool> stopping;
    std::uint64_t nextTaskId;

    void queueTaskCompletion(std::uint64_t taskId);

    void completeTask(std::uint64_t taskId);

    void deliverTaskCompletion(AsyncTaskDetail::TaskCompletionBase &completion);

    template <typename Work, typename Result>
    void executeWork(std::uint64_t taskId,
                     std::shared_ptr<AsyncTaskDetail::TaskOutcome<Result>> outcome,
                     Work work,
                     std::stop_token stopToken)
    {
        try
        {
            if constexpr (std::is_void_v<Result>)
            {
                if constexpr (std::invocable<Work &, std::stop_token>)
                {
                    std::invoke(work, stopToken);
                }
                else
                {
                    std::invoke(work);
                }
            }
            else if constexpr (std::invocable<Work &, std::stop_token>)
            {
                outcome->result.emplace(std::invoke(work, stopToken));
            }
            else
            {
                outcome->result.emplace(std::invoke(work));
            }
        }
        catch (...)
        {
            outcome->failure = std::current_exception();
        }

        queueTaskCompletion(taskId);
    }

  public:
    explicit AsyncTaskExecutor(QObject *parent = nullptr);

    ~AsyncTaskExecutor() override;

    template <typename Work, typename Success, typename Failure>
    bool startTask(UiTaskState &state, QObject &receiver, Work &&work, Success &&success, Failure &&failure)
    {
        using WorkType = std::decay_t<Work>;
        using SuccessType = std::decay_t<Success>;
        using FailureType = std::decay_t<Failure>;
        static_assert(std::invocable<WorkType &, std::stop_token> || std::invocable<WorkType &>,
                      "Asynchronous task work must be callable with a stop token or no arguments");

        using Result = AsyncTaskDetail::TaskResultType<WorkType>;
        if constexpr (std::is_void_v<Result>)
        {
            static_assert(std::invocable<SuccessType &>, "Void task success callback must accept no arguments");
        }
        else
        {
            static_assert(std::invocable<SuccessType &, Result>, "Task success callback must accept the task result");
        }
        static_assert(std::invocable<FailureType &, const QString &>,
                      "Task failure callback must accept a QString error");

        Q_ASSERT(QThread::currentThread() == thread());
        Q_ASSERT(state.thread() == thread());
        Q_ASSERT(receiver.thread() == thread());

        if (stopping.load(std::memory_order_acquire) || state.isLoading())
        {
            return false;
        }

        const std::uint64_t taskId = nextTaskId++;
        auto outcome = std::make_shared<AsyncTaskDetail::TaskOutcome<Result>>();
        auto completion = std::make_unique<AsyncTaskDetail::TaskCompletion<Result, SuccessType, FailureType>>(
            receiver,
            state,
            outcome,
            std::forward<Success>(success),
            std::forward<Failure>(failure));
        auto [task, inserted] = tasks.try_emplace(taskId, std::move(completion));
        Q_ASSERT(inserted);
        static_cast<void>(inserted);
        const bool startedLoading = state.startLoading();
        Q_ASSERT(startedLoading);
        static_cast<void>(startedLoading);

        try
        {
            task->second.worker = std::jthread(
                [this, taskId, outcome, work = WorkType(std::forward<Work>(work))](std::stop_token stopToken) mutable
                { executeWork(taskId, std::move(outcome), std::move(work), stopToken); });
        }
        catch (...)
        {
            outcome->failure = std::current_exception();
            queueTaskCompletion(taskId);
        }

        return true;
    }

    template <typename Work, typename Success>
    bool startTask(UiTaskState &state, QObject &receiver, Work &&work, Success &&success)
    {
        return startTask(state,
                         receiver,
                         std::forward<Work>(work),
                         std::forward<Success>(success),
                         [](const QString &) {});
    }

    void requestStop();

    void stopAndWait();

    std::size_t getActiveTaskCount() const;

    bool isStopping() const;
};

#endif
