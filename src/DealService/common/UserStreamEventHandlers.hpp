#ifndef USER_STREAM_EVENT_HANDLERS_H
#define USER_STREAM_EVENT_HANDLERS_H

#include <functional>

struct UserStreamEventHandlers
{
    std::function<void()> balancesChanged;
    std::function<void()> statusChanged;
};

#endif
