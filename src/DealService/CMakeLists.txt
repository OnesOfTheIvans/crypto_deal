add_library(DealLib
    DealService.cpp
    binance/BinanceDealService.cpp
    bybit/BybitDealService.cpp
    common/HttpRequest.cpp
    common/HttpRequestContext.cpp
)

target_include_directories(DealLib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)