

add_executable(DealServiceTests
    MockHttpRequest.cpp
    MockHttpRequestContext.cpp
    TestBinanceDealService.cpp
    TestBybitDealService.cpp
    ../src/DealService/DealService.cpp
    ../src/DealService/binance/BinanceDealService.cpp
    ../src/DealService/bybit/BybitDealService.cpp
)

target_include_directories(DealServiceTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/DealService
)

target_link_libraries(DealServiceTests PRIVATE
    GTest::GTest
    GTest::Main
    Boost::system
    Boost::json
    OpenSSL::SSL
    OpenSSL::Crypto
)

add_test(NAME DealServiceTests COMMAND DealServiceTests)
