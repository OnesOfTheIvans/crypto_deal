#include "../src/OperationChain/OperationChainDefinitionLoader.hpp"
#include "../src/DealService/common/DecimalConverter.hpp"
#include "../src/OperationChain/OperationChainBuilder.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {
    class TemporaryJsonFile
    {
      private:
        std::filesystem::path path;

      public:
        explicit TemporaryJsonFile(std::string_view contents)
        {
            static std::atomic_uint64_t nextFileId = 0;
            path = std::filesystem::temp_directory_path() /
                   ("crypto-deal-operation-chains-" + std::to_string(nextFileId.fetch_add(1)) + ".json");

            std::ofstream output(path, std::ios::trunc);
            if (!output)
            {
                throw std::runtime_error("Failed to create temporary operation-chain JSON file");
            }
            output << contents;
            if (!output)
            {
                throw std::runtime_error("Failed to write temporary operation-chain JSON file");
            }
        }

        ~TemporaryJsonFile()
        {
            std::error_code errorCode;
            std::filesystem::remove(path, errorCode);
            if (errorCode)
            {
                std::cerr << "Failed to remove temporary operation-chain JSON file: " << errorCode.message() << '\n';
            }
        }

        const std::filesystem::path &getPath() const
        {
            return path;
        }
    };

    const std::string AllOperationsJson = R"json(
{
  "schemaVersion": 1,
  "chains": [
    {
      "name": "All operations",
      "initial": {
        "exchange": "BINANCE",
        "asset": "USDT",
        "quantity": "0.000100"
      },
      "operations": [
        {
          "type": "BUY_CRYPTO",
          "config": { "outAsset": "BTC" }
        },
        {
          "type": "SELL_CRYPTO",
          "config": { "outAsset": "USDT" }
        },
        {
          "type": "PLACE_ORDER",
          "config": {
            "outAsset": "ETH",
            "side": "BUY",
            "orderType": "LIMIT",
            "price": "123.4500",
            "timeInForce": "GTC",
            "triggerPrice": "120.00",
            "orderFilter": "Order"
          }
        },
        {
          "type": "PLACE_ORDER",
          "config": {
            "outAsset": "SOL",
            "side": "SELL",
            "orderType": "MARKET",
            "marketUnit": "baseCoin"
          }
        },
        {
          "type": "PLACE_OCO",
          "config": {
            "outAsset": "BTC",
            "side": "SELL",
            "price": "70000.00",
            "stopPrice": "60000.00",
            "stopLimitPrice": "59900.00",
            "stopLimitTimeInForce": "GTC"
          }
        },
        {
          "type": "SEND_TO",
          "config": {
            "destinationExchange": "BYBIT",
            "chain": "TESTNET",
            "address": "simulated-address"
          }
        }
      ]
    }
  ]
}
)json";

    void expectLoadError(std::string_view contents, std::string_view expectedPath)
    {
        const TemporaryJsonFile file(contents);
        try
        {
            OperationChainDefinitionLoader::load(file.getPath());
            FAIL() << "Expected operation-chain definition loading to fail";
        }
        catch (const std::runtime_error &exception)
        {
            EXPECT_NE(std::string(exception.what()).find(expectedPath), std::string::npos) << exception.what();
        }
    }
}

TEST(OperationChainDefinitionLoaderTest, LoadsEverySupportedOperationTypeAndOptionalField)
{
    const TemporaryJsonFile file(AllOperationsJson);

    const std::vector<OperationChainDefinition> definitions = OperationChainDefinitionLoader::load(file.getPath());

    ASSERT_EQ(definitions.size(), 1);
    const OperationChainDefinition &definition = definitions.front();
    EXPECT_EQ(definition.getName(), "All operations");
    EXPECT_EQ(definition.getInitialExchangerType(), ExchangerType::BINANCE);
    EXPECT_EQ(definition.getInitialAsset(), "USDT");
    EXPECT_EQ(definition.getInitialQuantity(), DecimalConverter::parseDecimal("0.000100"));

    const std::vector<OperationDefinition> &operations = definition.getOperations();
    ASSERT_EQ(operations.size(), 6);
    EXPECT_EQ(operations[0].getType(), OperationType::BUY_CRYPTO);
    EXPECT_EQ(std::get<BaseConfig>(operations[0].getConfig()).outAsset, "BTC");
    EXPECT_EQ(operations[1].getType(), OperationType::SELL_CRYPTO);
    EXPECT_EQ(std::get<BaseConfig>(operations[1].getConfig()).outAsset, "USDT");

    EXPECT_EQ(operations[2].getType(), OperationType::PLACE_ORDER);
    const PlaceOrderConfig &limitOrder = std::get<PlaceOrderConfig>(operations[2].getConfig());
    EXPECT_EQ(limitOrder.outAsset, "ETH");
    EXPECT_EQ(limitOrder.side, OrderOperation::BUY);
    EXPECT_EQ(limitOrder.type, OrderType::LIMIT);
    EXPECT_EQ(limitOrder.price, DecimalConverter::parseDecimal("123.4500"));
    EXPECT_EQ(limitOrder.timeInForce, "GTC");
    EXPECT_EQ(limitOrder.triggerPrice, "120.00");
    EXPECT_EQ(limitOrder.orderFilter, "Order");
    EXPECT_FALSE(limitOrder.marketUnit.has_value());

    const PlaceOrderConfig &marketOrder = std::get<PlaceOrderConfig>(operations[3].getConfig());
    EXPECT_EQ(marketOrder.side, OrderOperation::SELL);
    EXPECT_EQ(marketOrder.type, OrderType::MARKET);
    EXPECT_EQ(marketOrder.marketUnit, "baseCoin");

    EXPECT_EQ(operations[4].getType(), OperationType::PLACE_OCO);
    const PlaceOcoConfig &oco = std::get<PlaceOcoConfig>(operations[4].getConfig());
    EXPECT_EQ(oco.outAsset, "BTC");
    EXPECT_EQ(oco.side, OrderOperation::SELL);
    EXPECT_EQ(oco.price, DecimalConverter::parseDecimal("70000.00"));
    EXPECT_EQ(oco.stopPrice, DecimalConverter::parseDecimal("60000.00"));
    EXPECT_EQ(oco.stopLimitPrice, DecimalConverter::parseDecimal("59900.00"));
    EXPECT_EQ(oco.stopLimitTimeInForce, "GTC");
    EXPECT_FALSE(oco.listClientOrderId.has_value());
    EXPECT_FALSE(oco.limitClientOrderId.has_value());
    EXPECT_FALSE(oco.stopClientOrderId.has_value());

    EXPECT_EQ(operations[5].getType(), OperationType::SEND_TO);
    const SendToConfig &sendTo = std::get<SendToConfig>(operations[5].getConfig());
    EXPECT_EQ(sendTo.destinationExchanger, ExchangerType::BYBIT);
    EXPECT_EQ(sendTo.chain, "TESTNET");
    EXPECT_EQ(sendTo.address, "simulated-address");
}

TEST(OperationChainDefinitionLoaderTest, BuildsFreshChainsThroughOperationFactory)
{
    const TemporaryJsonFile file(AllOperationsJson);
    const std::vector<OperationChainDefinition> definitions = OperationChainDefinitionLoader::load(file.getPath());
    const OperationChainBuilder builder;

    const std::unique_ptr<OperationChain> firstChain = builder.build(definitions.front(), {});
    const std::unique_ptr<OperationChain> secondChain = builder.build(definitions.front(), {});

    ASSERT_NE(firstChain, nullptr);
    ASSERT_NE(secondChain, nullptr);
    EXPECT_NE(firstChain.get(), secondChain.get());
}

TEST(OperationChainDefinitionLoaderTest, RejectsDuplicateNamesWithFieldPath)
{
    expectLoadError(R"json(
{
  "schemaVersion": 1,
  "chains": [
    {
      "name": "Duplicate",
      "initial": { "exchange": "BINANCE", "asset": "USDT", "quantity": "1" },
      "operations": [{ "type": "BUY_CRYPTO", "config": { "outAsset": "BTC" } }]
    },
    {
      "name": "Duplicate",
      "initial": { "exchange": "BYBIT", "asset": "USDT", "quantity": "1" },
      "operations": [{ "type": "BUY_CRYPTO", "config": { "outAsset": "BTC" } }]
    }
  ]
}
)json",
                    "$.chains[1].name");
}

TEST(OperationChainDefinitionLoaderTest, RejectsUnknownAndCollisionProneFields)
{
    expectLoadError(R"json(
{
  "schemaVersion": 1,
  "chains": [{
    "name": "OCO",
    "initial": { "exchange": "BINANCE", "asset": "BTC", "quantity": "0.001" },
    "operations": [{
      "type": "PLACE_OCO",
      "config": {
        "outAsset": "USDT",
        "side": "SELL",
        "price": "70000",
        "stopPrice": "60000",
        "listClientOrderId": "fixed-id"
      }
    }]
  }]
}
)json",
                    "$.chains[0].operations[0].config.listClientOrderId");
}

TEST(OperationChainDefinitionLoaderTest, RejectsNonStringAndNonPositiveDecimals)
{
    expectLoadError(R"json(
{
  "schemaVersion": 1,
  "chains": [{
    "name": "Numeric quantity",
    "initial": { "exchange": "BINANCE", "asset": "USDT", "quantity": 0.001 },
    "operations": [{ "type": "BUY_CRYPTO", "config": { "outAsset": "BTC" } }]
  }]
}
)json",
                    "$.chains[0].initial.quantity");

    expectLoadError(R"json(
{
  "schemaVersion": 1,
  "chains": [{
    "name": "Zero quantity",
    "initial": { "exchange": "BINANCE", "asset": "USDT", "quantity": "0.000" },
    "operations": [{ "type": "BUY_CRYPTO", "config": { "outAsset": "BTC" } }]
  }]
}
)json",
                    "$.chains[0].initial.quantity");
}

TEST(OperationChainDefinitionLoaderTest, RejectsMalformedWholeFile)
{
    expectLoadError("{\"schemaVersion\":1,\"chains\":[", "Failed to parse operation-chain definitions file");
}

TEST(OperationChainDefinitionLoaderTest, EnforcesOperationSpecificFields)
{
    expectLoadError(R"json(
{
  "schemaVersion": 1,
  "chains": [{
    "name": "Market with price",
    "initial": { "exchange": "BYBIT", "asset": "USDT", "quantity": "1" },
    "operations": [{
      "type": "PLACE_ORDER",
      "config": { "outAsset": "BTC", "side": "BUY", "orderType": "MARKET", "price": "1" }
    }]
  }]
}
)json",
                    "$.chains[0].operations[0].config.price");

    expectLoadError(R"json(
{
  "schemaVersion": 1,
  "chains": [{
    "name": "Incomplete stop limit",
    "initial": { "exchange": "BINANCE", "asset": "BTC", "quantity": "1" },
    "operations": [{
      "type": "PLACE_OCO",
      "config": {
        "outAsset": "USDT",
        "side": "SELL",
        "price": "2",
        "stopPrice": "1",
        "stopLimitPrice": "0.9"
      }
    }]
  }]
}
)json",
                    "$.chains[0].operations[0].config");
}

TEST(OperationChainDefinitionLoaderTest, DomainRejectsMismatchedConfigAndEmptyOperations)
{
    EXPECT_THROW(OperationDefinition(OperationType::PLACE_ORDER, BaseConfig{"BTC"}), std::runtime_error);
    EXPECT_THROW(OperationChainDefinition("Empty", ExchangerType::BINANCE, "USDT", Decimal{1}, {}), std::runtime_error);
}

TEST(OperationChainDefinitionLoaderTest, LoadsCommittedDefinitionsWithoutStartingThem)
{
    const std::vector<OperationChainDefinition> definitions =
        OperationChainDefinitionLoader::load(OPERATION_CHAINS_FILE);

    ASSERT_EQ(definitions.size(), 2);
    EXPECT_EQ(definitions[0].getName(), "Binance testnet BTC round trip");
    EXPECT_EQ(definitions[0].getInitialExchangerType(), ExchangerType::BINANCE);
    EXPECT_EQ(definitions[1].getName(), "Bybit testnet BTC round trip");
    EXPECT_EQ(definitions[1].getInitialExchangerType(), ExchangerType::BYBIT);
    EXPECT_EQ(definitions[0].getOperations().size(), 2);
    EXPECT_EQ(definitions[1].getOperations().size(), 2);
}
