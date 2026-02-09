# crypto_deal

# Integration tests
cmake -S . -B build -DBUILD_INTEGRATION_TESTS=ON
cmake --build build --target DealServiceIntegrationTests
./build/integration_tests/DealServiceIntegrationTests --gtest_color=yes

or

cmake --build build --target run_integration_tests