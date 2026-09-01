#ifndef ORDER_WAIT_INTERRUPTED_H
#define ORDER_WAIT_INTERRUPTED_H

#include <stdexcept>

class OrderWaitInterrupted final : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

#endif
