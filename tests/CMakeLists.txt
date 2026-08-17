

add_executable(DealServiceUnitIntegrationTests
    MockHttpRequest.cpp
    MockHttpRequestContext.cpp
    DealServiceTest.cpp
    BinanceDealServiceIntegrationTest.cpp
    BybitDealServiceIntegrationTest.cpp
    OperationFactoryTest.cpp
    OperationChainTest.cpp
    OperationChainIntegrationTest.cpp
    ../src/DealService/DealService.cpp
    ../src/DealService/binance/BinanceDealService.cpp
    ../src/DealService/bybit/BybitDealService.cpp
    ../src/DealService/common/DecimalConverter.cpp
    ../src/DealService/common/exception_handling.cpp
    ../src/OperationChain/ExchangerPull.cpp
    ../src/OperationChain/OperationFactory.cpp
    ../src/OperationChain/OperationChain.cpp
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

add_executable(ApplicationUnitTests
    ApplicationConfigurationTest.cpp
)

target_link_libraries(ApplicationUnitTests PRIVATE
    ApplicationLib
    GTest::GTest
    GTest::Main
)

add_test(NAME ApplicationUnitTests COMMAND ApplicationUnitTests)
