

add_executable(DealServiceTests
    MockHttpRequest.cpp
    MockHttpRequestContext.cpp
    TestBinanceDealService.cpp
    TestBybitDealService.cpp
    ../src/DealService/DealService.cpp
    ../src/DealService/binance/BinanceDealService.cpp
    ../src/DealService/bybit/BybitDealService.cpp
    ../src/DealService/common/DecimalConverter.cpp
    ../src/DealService/common/exception_handling.cpp
)

target_include_directories(DealServiceTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/DealService
)

target_link_libraries(DealServiceTests PRIVATE
    GTest::GTest
    GTest::Main
    Boost::json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

add_test(NAME DealServiceTests COMMAND DealServiceTests)