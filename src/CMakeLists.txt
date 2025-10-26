add_executable(CryptoDeal main.cpp)

add_subdirectory(DealService)

target_link_libraries(CryptoDeal PRIVATE DealLib Boost::system Boost::filesystem Boost::thread OpenSSL::SSL OpenSSL::Crypto)