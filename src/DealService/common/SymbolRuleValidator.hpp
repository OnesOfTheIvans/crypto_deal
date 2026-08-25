#ifndef SYMBOL_RULE_VALIDATOR_H
#define SYMBOL_RULE_VALIDATOR_H

#include "common/domain/SymbolInfo.hpp"

#include <optional>

enum class SymbolRuleViolation
{
    NONE,
    QUANTITY_STEP_UNAVAILABLE,
    QUANTITY_STEP_MISMATCH,
    QUANTITY_BELOW_MINIMUM,
    QUANTITY_ABOVE_MAXIMUM,
    PRICE_TICK_UNAVAILABLE,
    PRICE_TICK_MISMATCH,
    PRICE_BELOW_MINIMUM,
    PRICE_ABOVE_MAXIMUM,
    NOTIONAL_BELOW_MINIMUM,
    NOTIONAL_ABOVE_MAXIMUM
};

struct SymbolRuleValidation
{
    SymbolRuleViolation violation = SymbolRuleViolation::NONE;
    std::optional<Decimal> correction;

    bool isValid() const;
};

class SymbolRuleValidator
{
  public:
    static SymbolRuleValidation validateQuantity(Decimal quantity, const SymbolInfo &symbolInfo);

    static SymbolRuleValidation validatePrice(Decimal price, const SymbolInfo &symbolInfo);

    static SymbolRuleValidation validateNotional(Decimal notional, const SymbolInfo &symbolInfo);
};

#endif
