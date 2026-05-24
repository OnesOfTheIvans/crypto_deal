#ifndef CONFIG_H
#define CONFIG_H

#include "ExchangerType.hpp"
#include "common/domain/OrderOperation.hpp"
#include "common/domain/OrderType.hpp"
#include "common/type_aliasing.hpp"

#include <optional>
#include <string>
#include <variant>

struct BaseConfig
{
    std::string outAsset;
};

struct SendToConfig
{
    ExchangerType destinationExchanger;
    std::string chain;
    std::string address;
};

struct PlaceOrderConfig
{
    std::string outAsset;
    std::optional<OrderOperation> side;
    std::optional<OrderType> type;
    Decimal price{};
    std::optional<std::string> timeInForce;
    std::optional<std::string> triggerPrice;
    std::optional<std::string> orderFilter;
    std::optional<std::string> marketUnit;
};

struct PlaceOcoConfig
{
    std::string outAsset;
    std::optional<OrderOperation> side;
    Decimal price{};
    Decimal stopPrice{};
    std::optional<Decimal> stopLimitPrice;
    std::optional<std::string> stopLimitTimeInForce;
    std::optional<std::string> listClientOrderId;
    std::optional<std::string> limitClientOrderId;
    std::optional<std::string> stopClientOrderId;
};

using Config = std::variant<BaseConfig, PlaceOrderConfig, PlaceOcoConfig, SendToConfig>;

#endif
