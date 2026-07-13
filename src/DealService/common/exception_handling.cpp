#include "exception_handling.hpp"

#include <stdexcept>

namespace exception_handling {
    void throwIf(bool condition, const std::string &message)
    {
        if (condition)
        {
            throw std::runtime_error(message);
        }
    }
}