cmake_minimum_required(VERSION 3.10)

project(CryptoDeal)

cmake_policy(SET CMP0074 NEW)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Boost REQUIRED COMPONENTS system filesystem thread)
find_package(OpenSSL REQUIRED)
find_package(GTest REQUIRED)

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)