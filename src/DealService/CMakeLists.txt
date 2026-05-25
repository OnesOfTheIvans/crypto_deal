add_library(DealLib
    DealService.cpp
    binance/BinanceDealService.cpp
    bybit/BybitDealService.cpp
    common/DecimalConverter.cpp
    common/exception_handling.cpp
    common/http_request.cpp
    common/HttpRequestContext.cpp
)

target_include_directories(DealLib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(DealLib
    PUBLIC
        Boost::json
        Boost::url
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
)
