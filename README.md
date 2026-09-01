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

## Operation-chain definitions

Reusable proof-of-concept chains are defined in `operation_chains.json`. The file uses this versioned shape:

```json
{
  "schemaVersion": 1,
  "chains": [
    {
      "name": "Unique chain name",
      "initial": {
        "exchange": "BINANCE",
        "asset": "USDT",
        "quantity": "0.00010"
      },
      "operations": [
        {
          "type": "BUY_CRYPTO",
          "config": { "outAsset": "BTC" }
        }
      ]
    }
  ]
}
```

Exchanges use `BINANCE` or `BYBIT`; sides use `BUY` or `SELL`; order types use `MARKET` or `LIMIT`. Decimal values
must be positive fixed-decimal strings so they can be converted without binary floating-point loss. Supported
operation configs are:

- `BUY_CRYPTO` and `SELL_CRYPTO`: `outAsset`.
- `PLACE_ORDER`: `outAsset`, `side`, `orderType`, and optional `price`, `timeInForce`, `triggerPrice`, `orderFilter`,
  and `marketUnit`. A limit order requires `price`; a market order cannot contain `price` or `timeInForce`.
- `PLACE_OCO`: `outAsset`, `side`, `price`, `stopPrice`, and the optional pair `stopLimitPrice` plus
  `stopLimitTimeInForce`.
- `SEND_TO`: `destinationExchange`, `chain`, and `address`. This remains a testnet-compatible transfer simulation.

Unknown fields and invalid definitions stop application startup. OCO client identifiers are intentionally omitted;
the exchange services generate fresh identifiers so multiple runs of one definition cannot collide. Definitions do
not start automatically.

Each built `OperationChain` is one single-use run. Execution remains synchronous, while `getSnapshot()` and the
optional state-change handler expose detached thread-safe run and step state. Snapshots include typed operation
configuration, input/output context, timestamps, monotonic revisions, accepted ordinary/OCO identifiers, Awaiting
progress, and exact failures. A failed operation is recorded before its original exception is rethrown.

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
