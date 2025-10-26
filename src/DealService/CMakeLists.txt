add_library(DealLib
    DealService.cpp
    binance/BinanceDealService.cpp
    common/HttpRequest.cpp
)

target_include_directories(DealLib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)