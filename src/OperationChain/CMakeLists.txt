add_library(OperationChainLib
    DefaultSimulatedOperationChainRuns.cpp
    OperationCancellationCoordinator.cpp
    OperationChain.cpp
    OperationChainBuilder.cpp
    OperationChainDefinition.cpp
    OperationChainDefinitionLoader.cpp
    OperationChainRunManager.cpp
    OperationDefinition.cpp
    ExchangerPull.cpp
    OperationFactory.cpp
    SimulatedOperationChainRun.cpp
)

target_include_directories(OperationChainLib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(OperationChainLib
    PRIVATE
        Boost::json
        DealLib
)
