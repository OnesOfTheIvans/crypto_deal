add_executable(CryptoDeal main.cpp HttpRequest.cpp DealService.cpp)

target_link_libraries(CryptoDeal PRIVATE Boost::system Boost::filesystem Boost::thread OpenSSL::SSL OpenSSL::Crypto)