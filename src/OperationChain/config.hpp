#ifndef CONFIG_H
#define CONFIG_H

#include "ExchangerType.hpp"
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
    std::string side;
    std::string type;
    double price;
    std::optional<std::string> timeInForce;
    std::optional<std::string> triggerPrice;
    std::optional<std::string> orderFilter;
    std::optional<std::string> marketUnit;
};

struct PlaceOcoConfig
{
    std::string outAsset;
    std::string side;
    double price;
    double stopPrice;
    std::optional<double> stopLimitPrice;
    std::optional<std::string> stopLimitTimeInForce;
    std::optional<std::string> listClientOrderId;
    std::optional<std::string> limitClientOrderId;
    std::optional<std::string> stopClientOrderId;
};

using Config = std::variant<BaseConfig, PlaceOrderConfig, PlaceOcoConfig, SendToConfig>;

#endif