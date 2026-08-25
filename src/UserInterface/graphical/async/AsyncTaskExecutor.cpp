#include "AsyncTaskExecutor.hpp"

#include <QMetaObject>

#include <exception>

using namespace std;

namespace AsyncTaskDetail {
    QString getFailureMessage(const exception_ptr &failure)
    {
        if (failure == nullptr)
        {
            return {};
        }

        try
        {
            rethrow_exception(failure);
        }
        catch (const exception &error)
        {
            return QString::fromUtf8(error.what());
        }
        catch (...)
        {
            return "Asynchronous task failed with a non-standard exception";
        }
    }
}

AsyncTaskExecutor::AsyncTaskExecutor(QObject *parent) : QObject(parent), stopping(false), nextTaskId(1) {}

AsyncTaskExecutor::~AsyncTaskExecutor()
{
    stopAndWait();
}

void AsyncTaskExecutor::queueTaskCompletion(uint64_t taskId)
{
    if (stopping.load(memory_order_acquire))
    {
        return;
    }

    QMetaObject::invokeMethod(this, [this, taskId]() { completeTask(taskId); }, Qt::QueuedConnection);
}

void AsyncTaskExecutor::completeTask(uint64_t taskId)
{
    auto task = tasks.extract(taskId);
    if (task.empty())
    {
        return;
    }

    TaskRecord &record = task.mapped();
    if (record.worker.joinable())
    {
        record.worker.join();
    }
    if (!stopping.load(memory_order_acquire))
    {
        deliverTaskCompletion(*record.completion);
    }
}

void AsyncTaskExecutor::deliverTaskCompletion(AsyncTaskDetail::TaskCompletionBase &completion)
{
    if (completion.hasFailed())
    {
        const QString error = completion.getFailureMessage();
        if (completion.state != nullptr)
        {
            completion.state->finishWithFailure(error);
        }
        if (completion.receiver != nullptr)
        {
            try
            {
                completion.deliverFailure(error);
            }
            catch (...)
            {
                // A UI failure handler must not unwind into Qt's event dispatch.
            }
        }
        return;
    }

    try
    {
        if (completion.receiver != nullptr)
        {
            completion.deliverSuccess();
        }
        if (completion.state != nullptr)
        {
            completion.state->finishSuccessfully();
        }
    }
    catch (...)
    {
        const QString error = AsyncTaskDetail::getFailureMessage(current_exception());
        if (completion.state != nullptr)
        {
            completion.state->finishWithFailure(error);
        }
        if (completion.receiver != nullptr)
        {
            try
            {
                completion.deliverFailure(error);
            }
            catch (...)
            {
                // A UI failure handler must not unwind into Qt's event dispatch.
            }
        }
    }
}

void AsyncTaskExecutor::requestStop()
{
    if (stopping.exchange(true, memory_order_acq_rel))
    {
        return;
    }

    for (auto &taskEntry : tasks)
    {
        taskEntry.second.worker.request_stop();
    }
}

void AsyncTaskExecutor::stopAndWait()
{
    requestStop();

    for (auto &taskEntry : tasks)
    {
        if (taskEntry.second.worker.joinable())
        {
            taskEntry.second.worker.join();
        }
    }
    tasks.clear();
}

size_t AsyncTaskExecutor::getActiveTaskCount() const
{
    return tasks.size();
}

bool AsyncTaskExecutor::isStopping() const
{
    return stopping.load(memory_order_acquire);
}
