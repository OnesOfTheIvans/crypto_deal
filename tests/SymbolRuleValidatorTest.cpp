#include "common/SymbolRuleValidator.hpp"

#include "common/DecimalConverter.hpp"

#include <gtest/gtest.h>

namespace {
    SymbolInfo createSymbolInfo()
    {
        SymbolInfo symbolInfo;
        symbolInfo.stepSize = DecimalConverter::parseDecimal("0.001");
        symbolInfo.tickSize = DecimalConverter::parseDecimal("0.01");
        symbolInfo.minQty = DecimalConverter::parseDecimal("0.002");
        symbolInfo.maxQty = DecimalConverter::parseDecimal("10");
        symbolInfo.minPrice = DecimalConverter::parseDecimal("0.01");
        symbolInfo.maxPrice = DecimalConverter::parseDecimal("100000");
        symbolInfo.minNotional = DecimalConverter::parseDecimal("10");
        symbolInfo.maxNotional = DecimalConverter::parseDecimal("1000");
        return symbolInfo;
    }
}

TEST(SymbolRuleValidatorTest, ValidatesQuantityStepAndStaticRange)
{
    const SymbolInfo symbolInfo = createSymbolInfo();

    EXPECT_TRUE(SymbolRuleValidator::validateQuantity(DecimalConverter::parseDecimal("0.002"), symbolInfo).isValid());
    EXPECT_EQ(SymbolRuleValidator::validateQuantity(DecimalConverter::parseDecimal("0.001"), symbolInfo).violation,
              SymbolRuleViolation::QUANTITY_BELOW_MINIMUM);
    EXPECT_EQ(SymbolRuleValidator::validateQuantity(DecimalConverter::parseDecimal("11"), symbolInfo).violation,
              SymbolRuleViolation::QUANTITY_ABOVE_MAXIMUM);

    const SymbolRuleValidation mismatch =
        SymbolRuleValidator::validateQuantity(DecimalConverter::parseDecimal("0.0025"), symbolInfo);
    ASSERT_EQ(mismatch.violation, SymbolRuleViolation::QUANTITY_STEP_MISMATCH);
    ASSERT_TRUE(mismatch.correction.has_value());
    EXPECT_EQ(mismatch.correction.value(), DecimalConverter::parseDecimal("0.003"));
}

TEST(SymbolRuleValidatorTest, ValidatesPriceTickAndProposesNearestPriceWithUpwardTie)
{
    const SymbolInfo symbolInfo = createSymbolInfo();

    const SymbolRuleValidation lower =
        SymbolRuleValidator::validatePrice(DecimalConverter::parseDecimal("10.024"), symbolInfo);
    const SymbolRuleValidation tie =
        SymbolRuleValidator::validatePrice(DecimalConverter::parseDecimal("10.025"), symbolInfo);

    ASSERT_EQ(lower.violation, SymbolRuleViolation::PRICE_TICK_MISMATCH);
    ASSERT_EQ(tie.violation, SymbolRuleViolation::PRICE_TICK_MISMATCH);
    ASSERT_TRUE(lower.correction.has_value());
    ASSERT_TRUE(tie.correction.has_value());
    EXPECT_EQ(lower.correction.value(), DecimalConverter::parseDecimal("10.02"));
    EXPECT_EQ(tie.correction.value(), DecimalConverter::parseDecimal("10.03"));
}

TEST(SymbolRuleValidatorTest, ValidatesStaticPriceAndNotionalRanges)
{
    const SymbolInfo symbolInfo = createSymbolInfo();

    EXPECT_TRUE(SymbolRuleValidator::validatePrice(DecimalConverter::parseDecimal("5000"), symbolInfo).isValid());
    EXPECT_EQ(SymbolRuleValidator::validatePrice(DecimalConverter::parseDecimal("100000.01"), symbolInfo).violation,
              SymbolRuleViolation::PRICE_ABOVE_MAXIMUM);
    EXPECT_EQ(SymbolRuleValidator::validateNotional(DecimalConverter::parseDecimal("8"), symbolInfo).violation,
              SymbolRuleViolation::NOTIONAL_BELOW_MINIMUM);
    EXPECT_EQ(SymbolRuleValidator::validateNotional(DecimalConverter::parseDecimal("1001"), symbolInfo).violation,
              SymbolRuleViolation::NOTIONAL_ABOVE_MAXIMUM);
}

TEST(SymbolRuleValidatorTest, RejectsUnavailableIncrementRules)
{
    SymbolInfo symbolInfo = createSymbolInfo();
    symbolInfo.stepSize = Decimal{};
    symbolInfo.tickSize = Decimal{};

    EXPECT_EQ(SymbolRuleValidator::validateQuantity(DecimalConverter::parseDecimal("1"), symbolInfo).violation,
              SymbolRuleViolation::QUANTITY_STEP_UNAVAILABLE);
    EXPECT_EQ(SymbolRuleValidator::validatePrice(DecimalConverter::parseDecimal("1"), symbolInfo).violation,
              SymbolRuleViolation::PRICE_TICK_UNAVAILABLE);
}
