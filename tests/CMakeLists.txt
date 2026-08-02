

add_executable(DealServiceUnitIntegrationTests
    MockHttpRequest.cpp
    MockHttpRequestContext.cpp
    DealServiceTest.cpp
    BinanceDealServiceIntegrationTest.cpp
    BybitDealServiceIntegrationTest.cpp
    OperationFactoryTest.cpp
    ../src/DealService/DealService.cpp
    ../src/DealService/binance/BinanceDealService.cpp
    ../src/DealService/bybit/BybitDealService.cpp
    ../src/DealService/common/DecimalConverter.cpp
    ../src/DealService/common/exception_handling.cpp
    ../src/OperationChain/ExchangerPull.cpp
    ../src/OperationChain/OperationFactory.cpp
)

target_include_directories(DealServiceUnitIntegrationTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/DealService
)

target_link_libraries(DealServiceUnitIntegrationTests PRIVATE
    GTest::GTest
    GTest::Main
    Boost::json
    Boost::url
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

add_test(NAME DealServiceUnitIntegrationTests COMMAND DealServiceUnitIntegrationTests)
