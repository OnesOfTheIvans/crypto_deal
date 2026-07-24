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

add_custom_target(run_end_to_end_tests
    COMMAND $<TARGET_FILE:DealServiceEndToEndTests> --gtest_color=yes
    DEPENDS DealServiceEndToEndTests
    USES_TERMINAL
)
