#ifndef OCO_WAIT_RESULT_H
#define OCO_WAIT_RESULT_H

#include "OrderInfo.hpp"

struct OcoWaitResult
{
    OrderInfo filledOrder;
    OrderInfo siblingTerminalOrder;
};

#endif
