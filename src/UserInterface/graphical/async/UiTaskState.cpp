#include "UiTaskState.hpp"

UiTaskState::UiTaskState(QObject *parent) : QObject(parent), status(Status::IDLE) {}

UiTaskState::Status UiTaskState::getStatus() const
{
    return status;
}

const QString &UiTaskState::getError() const
{
    return error;
}

bool UiTaskState::isLoading() const
{
    return status == Status::LOADING;
}

bool UiTaskState::startLoading()
{
    if (isLoading())
    {
        return false;
    }

    if (!error.isEmpty())
    {
        error.clear();
        emit errorChanged(error);
    }
    status = Status::LOADING;
    emit statusChanged(status);
    return true;
}

void UiTaskState::finishSuccessfully()
{
    status = Status::SUCCEEDED;
    emit statusChanged(status);
}

void UiTaskState::finishWithFailure(const QString &failure)
{
    error = failure;
    emit errorChanged(error);
    status = Status::FAILED;
    emit statusChanged(status);
}
