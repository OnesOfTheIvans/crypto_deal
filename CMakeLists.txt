cmake_minimum_required(VERSION 3.28)

project(CryptoDeal)

cmake_policy(SET CMP0074 NEW)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Boost.Decimal decimal128_t triggers a GCC psABI compatibility note about 32-byte-aligned parameters.
    add_compile_options(-Wno-psabi)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

add_compile_definitions(CONFIG_FILE="${CMAKE_SOURCE_DIR}/config.ini")

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Boost 1.90 REQUIRED COMPONENTS json url)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(GTest REQUIRED)

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)

option(BUILD_END_TO_END_TESTS "Build real-exchange end-to-end tests (no mocks, hits network)" OFF)

if(BUILD_END_TO_END_TESTS)
    add_subdirectory(end_to_end_tests EXCLUDE_FROM_ALL)
endif()
