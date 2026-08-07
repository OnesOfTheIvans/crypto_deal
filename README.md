# crypto_deal

## Unit and integration tests

```bash
cmake -S . -B build
cmake --build build --target DealServiceUnitIntegrationTests
./build/tests/DealServiceUnitIntegrationTests --gtest_color=yes
```

The same suite can be run through CTest:

```bash
ctest --test-dir build --output-on-failure
```

## End-to-end tests

End-to-end tests use real Binance and Bybit endpoints and require valid credentials in `config.ini`.

```bash
cmake -S . -B build -DBUILD_END_TO_END_TESTS=ON
cmake --build build --target DealServiceEndToEndTests OperationChainEndToEndTests
./build/end_to_end_tests/DealServiceEndToEndTests --gtest_color=yes
./build/end_to_end_tests/OperationChainEndToEndTests --gtest_color=yes
```

The project-provided runner can be used instead:

```bash
cmake --build build --target run_end_to_end_tests
```
