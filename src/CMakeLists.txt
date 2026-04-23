add_executable(CryptoDeal main.cpp)

add_subdirectory(DealService)
add_subdirectory(OperationChain)

target_link_libraries(CryptoDeal PRIVATE DealLib OperationChainLib)