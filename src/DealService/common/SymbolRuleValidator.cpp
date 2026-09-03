#include "SymbolRuleValidator.hpp"

#include "DecimalConverter.hpp"

#include <boost/decimal.hpp>

namespace {
    bool isIncrementValid(Decimal value, Decimal increment)
    {
        const Decimal units = value / increment;
        return units == boost::decimal::floor(units);
    }

    Decimal getNearestIncrement(Decimal value, Decimal increment)
    {
        const Decimal lower = DecimalConverter::floorToStep(value, increment);
        const Decimal upper = DecimalConverter::ceilToStep(value, increment);
        if (lower <= 0)
        {
            return upper;
        }
        return value - lower < upper - value ? lower : upper;
    }
}

bool SymbolRuleValidation::isValid() const
{
    return violation == SymbolRuleViolation::NONE;
}

SymbolRuleValidation SymbolRuleValidator::validateQuantity(Decimal quantity, const SymbolInfo &symbolInfo)
{
    if (symbolInfo.stepSize <= 0)
    {
        return {SymbolRuleViolation::QUANTITY_STEP_UNAVAILABLE, std::nullopt};
    }
    if (!isIncrementValid(quantity, symbolInfo.stepSize))
    {
        return {SymbolRuleViolation::QUANTITY_STEP_MISMATCH,
                DecimalConverter::ceilToStep(quantity, symbolInfo.stepSize)};
    }
    if (symbolInfo.minQty > 0 && quantity < symbolInfo.minQty)
    {
        return {SymbolRuleViolation::QUANTITY_BELOW_MINIMUM, std::nullopt};
    }
    if (symbolInfo.maxQty > 0 && quantity > symbolInfo.maxQty)
    {
        return {SymbolRuleViolation::QUANTITY_ABOVE_MAXIMUM, std::nullopt};
    }
    return {};
}

SymbolRuleValidation SymbolRuleValidator::validatePrice(Decimal price, const SymbolInfo &symbolInfo)
{
    if (symbolInfo.tickSize <= 0)
    {
        return {SymbolRuleViolation::PRICE_TICK_UNAVAILABLE, std::nullopt};
    }
    if (!isIncrementValid(price, symbolInfo.tickSize))
    {
        return {SymbolRuleViolation::PRICE_TICK_MISMATCH, getNearestIncrement(price, symbolInfo.tickSize)};
    }
    if (symbolInfo.minPrice > 0 && price < symbolInfo.minPrice)
    {
        return {SymbolRuleViolation::PRICE_BELOW_MINIMUM, std::nullopt};
    }
    if (symbolInfo.maxPrice > 0 && price > symbolInfo.maxPrice)
    {
        return {SymbolRuleViolation::PRICE_ABOVE_MAXIMUM, std::nullopt};
    }
    return {};
}

SymbolRuleValidation SymbolRuleValidator::validateNotional(Decimal notional, const SymbolInfo &symbolInfo)
{
    if (symbolInfo.minNotional > 0 && notional < symbolInfo.minNotional)
    {
        return {SymbolRuleViolation::NOTIONAL_BELOW_MINIMUM, std::nullopt};
    }
    if (symbolInfo.maxNotional > 0 && notional > symbolInfo.maxNotional)
    {
        return {SymbolRuleViolation::NOTIONAL_ABOVE_MAXIMUM, std::nullopt};
    }
    return {};
}
