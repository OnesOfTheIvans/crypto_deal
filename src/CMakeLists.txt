add_executable(CryptoDeal main.cpp)

add_subdirectory(DealService)
add_subdirectory(OperationChain)

target_link_libraries(CryptoDeal PRIVATE DealLib OperationChainLib Boost::system Boost::filesystem Boost::thread Boost::json OpenSSL::SSL OpenSSL::Crypto)