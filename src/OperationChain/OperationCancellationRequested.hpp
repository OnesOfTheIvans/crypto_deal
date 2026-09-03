#ifndef OPERATION_CANCELLATION_REQUESTED_H
#define OPERATION_CANCELLATION_REQUESTED_H

#include <stdexcept>

class OperationCancellationRequested final : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

#endif
