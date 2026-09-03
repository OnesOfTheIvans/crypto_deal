#include "DefaultSimulatedOperationChainRuns.hpp"

#include "common/DecimalConverter.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace std;

namespace {
    Decimal parseDecimal(const string &value)
    {
        return DecimalConverter::parseDecimal(value);
    }

    OperationAcceptedIdentifiers createOrderIdentifiers(string orderId)
    {
        OperationAcceptedIdentifiers identifiers;
        identifiers.orderId = move(orderId);
        return identifiers;
    }

    OperationAcceptedIdentifiers createOcoIdentifiers(string groupId, string takeProfitId, string stopLossId)
    {
        OperationAcceptedIdentifiers identifiers;
        identifiers.ocoGroupId = move(groupId);
        identifiers.takeProfitOrderId = move(takeProfitId);
        identifiers.stopLossOrderId = move(stopLossId);
        return identifiers;
    }

    PlaceOrderConfig createMarketBuyConfig(string outAsset)
    {
        PlaceOrderConfig config;
        config.outAsset = move(outAsset);
        config.side = OrderOperation::BUY;
        config.type = OrderType::MARKET;
        return config;
    }

    PlaceOrderConfig createLimitBuyConfig(string outAsset, const string &price)
    {
        PlaceOrderConfig config;
        config.outAsset = move(outAsset);
        config.side = OrderOperation::BUY;
        config.type = OrderType::LIMIT;
        config.price = parseDecimal(price);
        config.timeInForce = "GTC";
        return config;
    }

    PlaceOcoConfig
    createSellOcoConfig(string outAsset, const string &price, const string &stopPrice, const string &stopLimitPrice)
    {
        PlaceOcoConfig config;
        config.outAsset = move(outAsset);
        config.side = OrderOperation::SELL;
        config.price = parseDecimal(price);
        config.stopPrice = parseDecimal(stopPrice);
        config.stopLimitPrice = parseDecimal(stopLimitPrice);
        config.stopLimitTimeInForce = "GTC";
        return config;
    }

    SimulatedOperationChainRunPlan createBinanceAccumulationPlan(chrono::milliseconds stepDelay)
    {
        vector<OperationDefinition> operations;
        operations.emplace_back(OperationType::BUY_CRYPTO, BaseConfig{"BTC"});
        operations.emplace_back(OperationType::SELL_CRYPTO, BaseConfig{"USDT"});
        operations.emplace_back(OperationType::PLACE_ORDER, createMarketBuyConfig("BTC"));
        operations.emplace_back(OperationType::PLACE_OCO, createSellOcoConfig("USDT", "72000", "62000", "61500"));
        operations.emplace_back(OperationType::SEND_TO,
                                SendToConfig{ExchangerType::BYBIT, "BTC testnet", "simulated-bybit-address"});

        vector<OperationContextSnapshot> succeededContexts{{ExchangerType::BINANCE, "BTC", parseDecimal("0.04")},
                                                           {ExchangerType::BINANCE, "USDT", parseDecimal("2480")}};
        vector<OperationAcceptedIdentifiers> identifiers{createOrderIdentifiers("SIM-BIN-BUY-001"),
                                                         createOrderIdentifiers("SIM-BIN-SELL-002"),
                                                         createOrderIdentifiers("SIM-BIN-MARKET-003"),
                                                         {},
                                                         {}};

        return {OperationChainDefinition("Simulated Binance accumulation",
                                         ExchangerType::BINANCE,
                                         "USDT",
                                         parseDecimal("2500"),
                                         move(operations)),
                move(succeededContexts),
                move(identifiers),
                2,
                chrono::milliseconds{0},
                stepDelay};
    }

    SimulatedOperationChainRunPlan createCrossExchangeHedgePlan(chrono::milliseconds stepDelay,
                                                                chrono::milliseconds startStagger)
    {
        vector<OperationDefinition> operations;
        operations.emplace_back(OperationType::BUY_CRYPTO, BaseConfig{"BTC"});
        operations.emplace_back(OperationType::SEND_TO,
                                SendToConfig{ExchangerType::BYBIT, "BTC testnet", "simulated-bybit-address"});
        operations.emplace_back(OperationType::SELL_CRYPTO, BaseConfig{"USDT"});
        operations.emplace_back(OperationType::PLACE_ORDER, createLimitBuyConfig("ETH", "3500"));
        operations.emplace_back(OperationType::PLACE_OCO, createSellOcoConfig("USDT", "3900", "3200", "3190"));

        vector<OperationContextSnapshot> succeededContexts{{ExchangerType::BINANCE, "BTC", parseDecimal("0.075")},
                                                           {ExchangerType::BYBIT, "BTC", parseDecimal("0.075")},
                                                           {ExchangerType::BYBIT, "USDT", parseDecimal("4975")}};
        vector<OperationAcceptedIdentifiers> identifiers{createOrderIdentifiers("SIM-HEDGE-BUY-001"),
                                                         {},
                                                         createOrderIdentifiers("SIM-HEDGE-SELL-003"),
                                                         createOrderIdentifiers("SIM-HEDGE-LIMIT-004"),
                                                         {}};

        return {OperationChainDefinition("Simulated cross-exchange hedge",
                                         ExchangerType::BINANCE,
                                         "USDT",
                                         parseDecimal("5000"),
                                         move(operations)),
                move(succeededContexts),
                move(identifiers),
                3,
                startStagger,
                stepDelay};
    }

    SimulatedOperationChainRunPlan createBybitProtectionPlan(chrono::milliseconds stepDelay,
                                                             chrono::milliseconds startStagger)
    {
        vector<OperationDefinition> operations;
        operations.emplace_back(OperationType::BUY_CRYPTO, BaseConfig{"ETH"});
        operations.emplace_back(OperationType::PLACE_OCO, createSellOcoConfig("USDT", "2300", "1900", "1890"));
        operations.emplace_back(OperationType::BUY_CRYPTO, BaseConfig{"BTC"});
        operations.emplace_back(OperationType::SEND_TO,
                                SendToConfig{ExchangerType::BINANCE, "BTC testnet", "simulated-binance-address"});
        operations.emplace_back(OperationType::PLACE_OCO, createSellOcoConfig("USDT", "74000", "64000", "63500"));

        vector<OperationContextSnapshot> succeededContexts{{ExchangerType::BYBIT, "ETH", parseDecimal("1.6")},
                                                           {ExchangerType::BYBIT, "USDT", parseDecimal("3300")},
                                                           {ExchangerType::BYBIT, "BTC", parseDecimal("0.05")},
                                                           {ExchangerType::BINANCE, "BTC", parseDecimal("0.05")}};
        vector<OperationAcceptedIdentifiers> identifiers{
            createOrderIdentifiers("SIM-PROTECT-BUY-001"),
            createOcoIdentifiers("SIM-PROTECT-OCO-002", "SIM-PROTECT-TP-002", "SIM-PROTECT-SL-002"),
            createOrderIdentifiers("SIM-PROTECT-BUY-003"),
            {},
            createOcoIdentifiers("SIM-PROTECT-OCO-005", "SIM-PROTECT-TP-005", "SIM-PROTECT-SL-005")};

        return {OperationChainDefinition("Simulated Bybit protection",
                                         ExchangerType::BYBIT,
                                         "USDT",
                                         parseDecimal("3200"),
                                         move(operations)),
                move(succeededContexts),
                move(identifiers),
                4,
                startStagger * 2,
                stepDelay};
    }
}

vector<SimulatedOperationChainRunPlan> createDefaultSimulatedOperationChainRunPlans(chrono::milliseconds stepDelay,
                                                                                    chrono::milliseconds startStagger)
{
    return {createBinanceAccumulationPlan(stepDelay),
            createCrossExchangeHedgePlan(stepDelay, startStagger),
            createBybitProtectionPlan(stepDelay, startStagger)};
}
