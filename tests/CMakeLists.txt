

add_executable(DealServiceUnitIntegrationTests
    MockHttpRequest.cpp
    MockHttpRequestContext.cpp
    DealServiceTest.cpp
    SymbolRuleValidatorTest.cpp
    TradablePairUtilTest.cpp
    BinanceDealServiceIntegrationTest.cpp
    BybitDealServiceIntegrationTest.cpp
    DealServiceConcurrencyTest.cpp
    OperationFactoryTest.cpp
    OperationChainTest.cpp
    OperationChainIntegrationTest.cpp
    ../src/DealService/DealService.cpp
    ../src/DealService/binance/BinanceDealService.cpp
    ../src/DealService/bybit/BybitDealService.cpp
    ../src/DealService/common/DecimalConverter.cpp
    ../src/DealService/common/SymbolRuleValidator.cpp
    ../src/DealService/common/TradablePairUtil.cpp
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

add_executable(GraphicalUserInterfaceTests
    AsyncTaskExecutorTest.cpp
    BalanceCatalogTest.cpp
    GraphicalUserInterfaceTest.cpp
    OrderEntryFormTest.cpp
    OrderInputValidationTest.cpp
    OrderPlacementModelTest.cpp
    PairCatalogTest.cpp
    SymbolInfoCatalogTest.cpp
)

target_link_libraries(GraphicalUserInterfaceTests PRIVATE
    GraphicalUserInterfaceLib
    GTest::GTest
    GTest::Main
    Qt6::Test
)

add_test(NAME GraphicalUserInterfaceTests COMMAND GraphicalUserInterfaceTests)
set_tests_properties(GraphicalUserInterfaceTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
