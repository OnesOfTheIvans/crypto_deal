#ifndef EXCEPTION_HANDLING_HPP
#define EXCEPTION_HANDLING_HPP

#include <string>

namespace exception_handling
{
    void throwIf(bool condition, const std::string &message);
}

#endif