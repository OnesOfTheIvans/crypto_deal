add_library(DealLib
    DealService.cpp
    binance/BinanceDealService.cpp
    bybit/BybitDealService.cpp
    common/http_request.cpp
    common/HttpRequestContext.cpp
)

target_include_directories(DealLib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)