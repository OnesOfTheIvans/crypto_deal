#ifndef UI_TASK_STATE_H
#define UI_TASK_STATE_H

#include <QObject>
#include <QString>

class AsyncTaskExecutor;

class UiTaskState final : public QObject
{
    Q_OBJECT

  public:
    enum class Status
    {
        IDLE,
        LOADING,
        SUCCEEDED,
        FAILED
    };
    Q_ENUM(Status)

  private:
    Status status;
    QString error;

    bool startLoading();

    void finishSuccessfully();

    void finishWithFailure(const QString &error);

    friend class AsyncTaskExecutor;

  public:
    explicit UiTaskState(QObject *parent = nullptr);

    Status getStatus() const;

    const QString &getError() const;

    bool isLoading() const;

  signals:
    void statusChanged(UiTaskState::Status status);

    void errorChanged(const QString &error);
};

#endif
