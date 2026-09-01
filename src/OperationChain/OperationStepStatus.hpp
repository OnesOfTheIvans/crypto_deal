#ifndef OPERATION_STEP_STATUS_H
#define OPERATION_STEP_STATUS_H

enum class OperationStepStatus
{
    PENDING,
    RUNNING,
    AWAITING,
    SUCCEEDED,
    FAILED,
    CANCELLED
};

#endif
