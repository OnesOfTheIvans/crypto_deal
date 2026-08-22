add_executable(CryptoDeal main.cpp)

add_subdirectory(Application)
add_subdirectory(DealService)
add_subdirectory(OperationChain)
add_subdirectory(UserInterface)

target_link_libraries(CryptoDeal
    PRIVATE
        ApplicationLib
        DealLib
        GraphicalUserInterfaceLib
        OperationChainLib
        Qt6::Widgets
)
