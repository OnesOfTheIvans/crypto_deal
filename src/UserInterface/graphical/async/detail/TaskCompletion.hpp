#ifndef ASYNC_TASK_COMPLETION_H
#define ASYNC_TASK_COMPLETION_H

#include "graphical/async/UiTaskState.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

#include <concepts>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace AsyncTaskDetail {
    template <typename Work, bool acceptsStopToken> struct TaskResult;

    template <typename Work> struct TaskResult<Work, true>
    {
        using Type = std::remove_cvref_t<std::invoke_result_t<Work &, std::stop_token>>;
    };

    template <typename Work> struct TaskResult<Work, false>
    {
        using Type = std::remove_cvref_t<std::invoke_result_t<Work &>>;
    };

    template <typename Work>
    using TaskResultType = typename TaskResult<Work, std::invocable<Work &, std::stop_token>>::Type;

    template <typename Result> struct TaskOutcome
    {
        std::optional<Result> result;
        std::exception_ptr failure;
    };

    template <> struct TaskOutcome<void>
    {
        std::exception_ptr failure;
    };

    QString getFailureMessage(const std::exception_ptr &failure);

    class TaskCompletionBase
    {
      public:
        QPointer<QObject> receiver;
        QPointer<UiTaskState> state;

        TaskCompletionBase(QObject &receiver, UiTaskState &state) : receiver(&receiver), state(&state) {}

        virtual bool hasFailed() const = 0;

        virtual QString getFailureMessage() const = 0;

        virtual void deliverSuccess() = 0;

        virtual void deliverFailure(const QString &error) = 0;

        virtual ~TaskCompletionBase() = default;
    };

    template <typename Result, typename Success, typename Failure>
    class TaskCompletion final : public TaskCompletionBase
    {
      private:
        std::shared_ptr<TaskOutcome<Result>> outcome;
        Success success;
        Failure failure;

      public:
        TaskCompletion(QObject &receiver,
                       UiTaskState &state,
                       std::shared_ptr<TaskOutcome<Result>> outcome,
                       Success success,
                       Failure failure)
            : TaskCompletionBase(receiver, state), outcome(std::move(outcome)), success(std::move(success)),
              failure(std::move(failure))
        {}

        bool hasFailed() const override
        {
            return outcome->failure != nullptr;
        }

        QString getFailureMessage() const override
        {
            return AsyncTaskDetail::getFailureMessage(outcome->failure);
        }

        void deliverSuccess() override
        {
            if constexpr (std::is_void_v<Result>)
            {
                std::invoke(success);
            }
            else
            {
                std::invoke(success, std::move(outcome->result).value());
            }
        }

        void deliverFailure(const QString &error) override
        {
            std::invoke(failure, error);
        }
    };
}

#endif
