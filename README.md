# CryptoDeal

CryptoDeal is a C++23 and Qt 6 desktop utility for working with Binance and Bybit spot orders, balances, and reusable
operation chains. The application shares one service instance per exchange across its Orders, Accounts, and Operation
Chains pages so startup state and live updates remain consistent throughout a GUI session.

The GUI can submit and cancel real orders. Simply opening it loads market/account data and starts private user streams,
but an order or loaded operation chain is not started until its confirmation is accepted. The three runs labelled
`Simulated` are local demonstrations and never call an exchange service.

## Prerequisites

- CMake 3.28 or newer and a C++23 compiler.
- Qt 6 Widgets and Test development packages.
- Boost 1.90 or newer with JSON and URL, plus OpenSSL, Threads, and GoogleTest development packages.

## Configure the exchanges

Create the ignored local configuration file from the committed template:

```bash
cp config_sample.ini config.ini
```

Replace every placeholder in `config.ini`. REST and WebSocket values are hostnames only: do not include `https://`,
`wss://`, a port, or a URL path. Choose the exchange environment appropriate to your account and use API credentials
from that same environment. The credentials need only the permissions required for the actions you intend to use;
withdrawal permission is not required for normal order and balance workflows. Never commit `config.ini`.

`operation_chains.json` is also loaded at startup. Its committed definitions remain idle until a user confirms a run.
Review or replace them before starting any real chain.

## Build and run

From the repository root:

```bash
cmake -S . -B build
cmake --build build -j1
./build/src/CryptoDeal
```

On startup, Binance and Bybit pair catalogs, balance snapshots, and live balance streams initialize independently.
One exchange can become usable while the other is still loading or reports an exact error.

## GUI guide

- **Orders** loads exchange-specific pairs and trading rules, validates quantities and prices, and asks for an immutable
  summary confirmation before submission. Session orders retain accepted, failed, active, and terminal results and
  provide explicit refresh and cancellation actions.
- **Accounts** shows separate Binance and Bybit balance tables. Manual REST refresh state and private-stream state are
  displayed independently; a failed refresh preserves the latest successful snapshot.
- **Operation Chains** lists committed definitions and session runs. Starting or cancelling a real run requires
  confirmation. The run inspector exposes each step's typed configuration, context, timing, and exact outcome.

Static blue, green, amber, and red status panels distinguish loading, success, recoverable warnings, and failures. The
status text carries the same meaning without relying on color, and all primary navigation and actions are keyboard
focusable.

## Tests

Configure and build first, then run every deterministic unit and integration test (including the offscreen GUI suite):

```bash
ctest --test-dir build --output-on-failure
```

Individual suites can be run directly:

```bash
./build/tests/ApplicationUnitTests --gtest_color=yes
./build/tests/DealServiceUnitIntegrationTests --gtest_color=yes
QT_QPA_PLATFORM=offscreen ./build/tests/GraphicalUserInterfaceTests --gtest_color=yes
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

End-to-end tests are optional, disabled by default, and use real Binance and Bybit endpoints. They require valid
credentials in `config.ini` and can place or cancel real orders, so inspect their filters and configured environment
before running them.

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

## Troubleshooting

- A startup dialog reporting a missing configuration key means `config.ini` is absent, unreadable, or still contains
  an empty required value.
- DNS, TLS, or connection failures usually mean a host contains a scheme/path or does not match the chosen exchange
  environment.
- Pair, balance, trading-rule, and stream failures are reported separately in the GUI. A cached-balance warning means
  the previous successful snapshot remains visible; an unavailable error means no usable snapshot exists.
- Order placement remains disabled until the selected exchange has a successful balance snapshot and the selected pair
  has valid trading rules.
