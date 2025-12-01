add_library(OperationChainLib
    OperationChain.cpp
    ExchangerPull.cpp
    operations.cpp
)

target_include_directories(OperationChainLib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(OperationChainLib
    PRIVATE DealLib
)