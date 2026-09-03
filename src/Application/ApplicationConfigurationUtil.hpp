#ifndef APPLICATION_CONFIGURATION_UTIL_H
#define APPLICATION_CONFIGURATION_UTIL_H

#include "ApplicationConfiguration.hpp"

#include <filesystem>

ApplicationConfiguration loadApplicationConfiguration(const std::filesystem::path &path);

#endif
