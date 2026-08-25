#include "OrderInputValidation.hpp"

#include "common/DecimalConverter.hpp"
#include "common/SymbolRuleValidator.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <string>
#include <string_view>

using namespace std;

namespace {
    constexpr int MAX_SIGNIFICANT_DIGITS = 34;

    bool hasFixedDecimalSyntax(string_view text)
    {
        if (text.empty())
        {
            return false;
        }

        int decimalPointCount = 0;
        int digitCount = 0;
        for (const char character : text)
        {
            if (isdigit(static_cast<unsigned char>(character)) != 0)
            {
                ++digitCount;
            }
            else if (character == '.')
            {
                ++decimalPointCount;
            }
            else
            {
                return false;
            }
        }
        return digitCount > 0 && decimalPointCount <= 1;
    }

    int getSignificantDigitCount(string_view text)
    {
        bool foundNonZeroDigit = false;
        int significantDigitCount = 0;
        for (const char character : text)
        {
            if (isdigit(static_cast<unsigned char>(character)) == 0)
            {
                continue;
            }
            if (character != '0')
            {
                foundNonZeroDigit = true;
            }
            if (foundNonZeroDigit)
            {
                ++significantDigitCount;
            }
        }
        return max(significantDigitCount, 1);
    }

    int getFractionalDigitCount(string_view text)
    {
        const size_t decimalPoint = text.find('.');
        return decimalPoint == string_view::npos ? 0 : static_cast<int>(text.size() - decimalPoint - 1);
    }

    int getAllowedDecimalPlaces(Decimal increment, int exchangePrecision)
    {
        const int incrementPrecision = DecimalConverter::decimalsFromStep(increment);
        return exchangePrecision > 0 ? min(incrementPrecision, exchangePrecision) : incrementPrecision;
    }

    string normalizeDecimalText(string text)
    {
        if (!text.empty() && text.front() == '.')
        {
            text.insert(text.begin(), '0');
        }
        if (!text.empty() && text.back() == '.')
        {
            text.push_back('0');
        }
        return text;
    }

    QString formatLimit(Decimal value, Decimal increment)
    {
        return QString::fromStdString(DecimalConverter::formatByStep(value, increment));
    }

    void applyQuantityRuleValidation(DecimalInputValidation &validation,
                                     const SymbolRuleValidation &ruleValidation,
                                     const SymbolInfo &symbolInfo)
    {
        validation.correction = ruleValidation.correction;
        switch (ruleValidation.violation)
        {
        case SymbolRuleViolation::QUANTITY_STEP_UNAVAILABLE:
            validation.error = "Quantity step information is unavailable.";
            break;
        case SymbolRuleViolation::QUANTITY_STEP_MISMATCH:
            validation.error =
                "Amount must use increments of " + formatLimit(symbolInfo.stepSize, symbolInfo.stepSize) + ".";
            break;
        case SymbolRuleViolation::QUANTITY_BELOW_MINIMUM:
            validation.error =
                "Amount is below the minimum " + formatLimit(symbolInfo.minQty, symbolInfo.stepSize) + ".";
            break;
        case SymbolRuleViolation::QUANTITY_ABOVE_MAXIMUM:
            validation.error =
                "Amount is above the maximum " + formatLimit(symbolInfo.maxQty, symbolInfo.stepSize) + ".";
            break;
        case SymbolRuleViolation::NONE:
            break;
        default:
            validation.error = "Amount does not satisfy the selected symbol rules.";
            break;
        }
    }

    void applyPriceRuleValidation(DecimalInputValidation &validation,
                                  const SymbolRuleValidation &ruleValidation,
                                  const SymbolInfo &symbolInfo)
    {
        validation.correction = ruleValidation.correction;
        switch (ruleValidation.violation)
        {
        case SymbolRuleViolation::PRICE_TICK_UNAVAILABLE:
            validation.error = "Price tick information is unavailable.";
            break;
        case SymbolRuleViolation::PRICE_TICK_MISMATCH:
            validation.error = "Price must use ticks of " + formatLimit(symbolInfo.tickSize, symbolInfo.tickSize) + ".";
            break;
        case SymbolRuleViolation::PRICE_BELOW_MINIMUM:
            validation.error =
                "Price is below the minimum " + formatLimit(symbolInfo.minPrice, symbolInfo.tickSize) + ".";
            break;
        case SymbolRuleViolation::PRICE_ABOVE_MAXIMUM:
            validation.error =
                "Price is above the maximum " + formatLimit(symbolInfo.maxPrice, symbolInfo.tickSize) + ".";
            break;
        case SymbolRuleViolation::NONE:
            break;
        default:
            validation.error = "Price does not satisfy the selected symbol rules.";
            break;
        }
    }

    DecimalInputValidation parsePositiveDecimal(const QString &text)
    {
        DecimalInputValidation validation;
        const string input = text.toStdString();
        if (input.empty())
        {
            validation.error = "Enter a value.";
            return validation;
        }
        if (!hasFixedDecimalSyntax(input))
        {
            validation.error = "Use fixed decimal notation, for example 0.25.";
            return validation;
        }
        if (getSignificantDigitCount(input) > MAX_SIGNIFICANT_DIGITS)
        {
            validation.error = "Use no more than 34 significant digits.";
            return validation;
        }

        try
        {
            validation.value = DecimalConverter::parseDecimal(normalizeDecimalText(input));
        }
        catch (const exception &)
        {
            validation.error = "Use a decimal value within the supported range.";
            return validation;
        }

        if (validation.value.value() <= 0)
        {
            validation.error = "Value must be greater than 0.";
            validation.value.reset();
        }
        return validation;
    }

}

bool DecimalInputValidation::isValid() const
{
    return value.has_value() && error.isEmpty();
}

DecimalInputValidation OrderInputValidation::validateQuantity(const QString &text, const SymbolInfo &symbolInfo)
{
    DecimalInputValidation validation = parsePositiveDecimal(text);
    if (!validation.isValid())
    {
        return validation;
    }
    const Decimal value = validation.value.value();
    const SymbolRuleValidation ruleValidation = SymbolRuleValidator::validateQuantity(value, symbolInfo);
    if (ruleValidation.violation == SymbolRuleViolation::QUANTITY_STEP_UNAVAILABLE ||
        ruleValidation.violation == SymbolRuleViolation::QUANTITY_STEP_MISMATCH)
    {
        applyQuantityRuleValidation(validation, ruleValidation, symbolInfo);
        return validation;
    }

    const int allowedDecimalPlaces = getAllowedDecimalPlaces(symbolInfo.stepSize, symbolInfo.qtyPrecision);
    if (getFractionalDigitCount(text.toStdString()) > allowedDecimalPlaces)
    {
        validation.error = QString("Amount supports at most %1 decimal places.").arg(allowedDecimalPlaces);
        return validation;
    }
    if (!ruleValidation.isValid())
    {
        applyQuantityRuleValidation(validation, ruleValidation, symbolInfo);
    }
    return validation;
}

DecimalInputValidation OrderInputValidation::validatePrice(const QString &text, const SymbolInfo &symbolInfo)
{
    DecimalInputValidation validation = parsePositiveDecimal(text);
    if (!validation.isValid())
    {
        return validation;
    }
    const Decimal value = validation.value.value();
    const SymbolRuleValidation ruleValidation = SymbolRuleValidator::validatePrice(value, symbolInfo);
    if (ruleValidation.violation == SymbolRuleViolation::PRICE_TICK_UNAVAILABLE ||
        ruleValidation.violation == SymbolRuleViolation::PRICE_TICK_MISMATCH)
    {
        applyPriceRuleValidation(validation, ruleValidation, symbolInfo);
        return validation;
    }

    const int allowedDecimalPlaces = getAllowedDecimalPlaces(symbolInfo.tickSize, symbolInfo.pricePrecision);
    if (getFractionalDigitCount(text.toStdString()) > allowedDecimalPlaces)
    {
        validation.error = QString("Price supports at most %1 decimal places.").arg(allowedDecimalPlaces);
        return validation;
    }
    if (!ruleValidation.isValid())
    {
        applyPriceRuleValidation(validation, ruleValidation, symbolInfo);
    }
    return validation;
}

QString OrderInputValidation::validateNotional(Decimal quantity, Decimal price, const SymbolInfo &symbolInfo)
{
    const Decimal notional = quantity * price;
    const SymbolRuleValidation ruleValidation = SymbolRuleValidator::validateNotional(notional, symbolInfo);
    if (ruleValidation.violation == SymbolRuleViolation::NOTIONAL_BELOW_MINIMUM)
    {
        return "Notional " + QString::fromStdString(DecimalConverter::formatDecimal(notional)) +
               " is below the minimum " +
               QString::fromStdString(DecimalConverter::formatDecimal(symbolInfo.minNotional)) + ".";
    }
    if (ruleValidation.violation == SymbolRuleViolation::NOTIONAL_ABOVE_MAXIMUM)
    {
        return "Notional " + QString::fromStdString(DecimalConverter::formatDecimal(notional)) +
               " is above the maximum " +
               QString::fromStdString(DecimalConverter::formatDecimal(symbolInfo.maxNotional)) + ".";
    }
    return {};
}
