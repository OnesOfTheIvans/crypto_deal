add_executable(DealServiceEndToEndTests
    DealServiceEndToEndTest.cpp
)

target_link_libraries(DealServiceEndToEndTests PRIVATE
    GTest::gtest
    GTest::gtest_main
    DealLib

    ${OPENSSL_LIBRARIES}
    ssl
    crypto

    Boost::json
    Boost::url
)

if(WIN32)
    target_link_libraries(DealServiceEndToEndTests PRIVATE ws2_32 mswsock)
endif()

add_executable(OperationChainEndToEndTests
    OperationChainEndToEndTest.cpp
)

target_link_libraries(OperationChainEndToEndTests PRIVATE
    GTest::gtest
    GTest::gtest_main
    DealLib
    OperationChainLib

    ${OPENSSL_LIBRARIES}
    ssl
    crypto

    Boost::json
    Boost::url
)

if(WIN32)
    target_link_libraries(OperationChainEndToEndTests PRIVATE ws2_32 mswsock)
endif()

add_custom_target(run_operation_chain_end_to_end_tests
    COMMAND $<TARGET_FILE:OperationChainEndToEndTests> --gtest_color=yes
    DEPENDS OperationChainEndToEndTests
    USES_TERMINAL
)

add_custom_target(run_end_to_end_tests
    COMMAND $<TARGET_FILE:DealServiceEndToEndTests> --gtest_color=yes
    COMMAND $<TARGET_FILE:OperationChainEndToEndTests> --gtest_color=yes
    DEPENDS DealServiceEndToEndTests OperationChainEndToEndTests
    USES_TERMINAL
)
