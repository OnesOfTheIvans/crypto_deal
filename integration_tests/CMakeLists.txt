add_executable(DealServiceIntegrationTests
    DealServiceIntegrationTests.cpp
)

target_link_libraries(DealServiceIntegrationTests PRIVATE
    GTest::gtest
    GTest::gtest_main
    DealLib

    ${OPENSSL_LIBRARIES}
    ssl
    crypto

    Boost::system
    Boost::json
)

if(WIN32)
    target_link_libraries(DealServiceIntegrationTests PRIVATE ws2_32 mswsock)
endif()

add_custom_target(run_integration_tests
    COMMAND $<TARGET_FILE:DealServiceIntegrationTests> --gtest_color=yes
    DEPENDS DealServiceIntegrationTests
    USES_TERMINAL
)