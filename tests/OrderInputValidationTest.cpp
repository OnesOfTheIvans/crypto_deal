#include "graphical/validation/OrderInputValidation.hpp"

#include "common/DecimalConverter.hpp"

#include <QString>
#include <gtest/gtest.h>

namespace {
    SymbolInfo createSymbolInfo()
    {
        SymbolInfo symbolInfo;
        symbolInfo.tickSize = DecimalConverter::parseDecimal("0.01");
        symbolInfo.stepSize = DecimalConverter::parseDecimal("0.001");
        symbolInfo.minQty = DecimalConverter::parseDecimal("0.002");
        symbolInfo.maxQty = DecimalConverter::parseDecimal("10");
        symbolInfo.minPrice = DecimalConverter::parseDecimal("0.01");
        symbolInfo.maxPrice = DecimalConverter::parseDecimal("100000");
        symbolInfo.minNotional = DecimalConverter::parseDecimal("10");
        symbolInfo.maxNotional = DecimalConverter::parseDecimal("1000");
        symbolInfo.qtyPrecision = 3;
        symbolInfo.pricePrecision = 2;
        return symbolInfo;
    }
}

TEST(OrderInputValidationTest, AcceptsPositiveFixedDecimalsWithinQuantityRules)
{
    const DecimalInputValidation validation = OrderInputValidation::validateQuantity("0.002", createSymbolInfo());

    ASSERT_TRUE(validation.isValid());
    EXPECT_EQ(validation.value.value(), DecimalConverter::parseDecimal("0.002"));
    EXPECT_FALSE(validation.correction.has_value());
    EXPECT_TRUE(validation.error.isEmpty());
}

TEST(OrderInputValidationTest, ReportsSyntaxPositivityPrecisionAndRangeErrorsWithoutChangingText)
{
    const SymbolInfo symbolInfo = createSymbolInfo();

    EXPECT_EQ(OrderInputValidation::validateQuantity("12..3", symbolInfo).error,
              QString("Use fixed decimal notation, for example 0.25."));
    EXPECT_EQ(OrderInputValidation::validateQuantity("1e-3", symbolInfo).error,
              QString("Use fixed decimal notation, for example 0.25."));
    EXPECT_EQ(OrderInputValidation::validateQuantity("1.1234567890123456789012345678901234", symbolInfo).error,
              QString("Use no more than 34 significant digits."));
    EXPECT_EQ(OrderInputValidation::validateQuantity("0", symbolInfo).error, QString("Value must be greater than 0."));
    EXPECT_EQ(OrderInputValidation::validateQuantity("0.0020", symbolInfo).error,
              QString("Amount supports at most 3 decimal places."));
    EXPECT_EQ(OrderInputValidation::validateQuantity("0.001", symbolInfo).error,
              QString("Amount is below the minimum 0.002."));
    EXPECT_EQ(OrderInputValidation::validateQuantity("11", symbolInfo).error,
              QString("Amount is above the maximum 10."));
}

TEST(OrderInputValidationTest, ProposesUpwardQuantityCorrectionForInvalidStep)
{
    const DecimalInputValidation validation = OrderInputValidation::validateQuantity("0.0025", createSymbolInfo());

    ASSERT_FALSE(validation.isValid());
    ASSERT_TRUE(validation.correction.has_value());
    EXPECT_EQ(validation.correction.value(), DecimalConverter::parseDecimal("0.003"));
    EXPECT_EQ(validation.error, QString("Amount must use increments of 0.001."));
}

TEST(OrderInputValidationTest, ProposesNearestPriceTickAndRoundsUpOnAnExactTie)
{
    const SymbolInfo symbolInfo = createSymbolInfo();
    const DecimalInputValidation lower = OrderInputValidation::validatePrice("10.024", symbolInfo);
    const DecimalInputValidation tie = OrderInputValidation::validatePrice("10.025", symbolInfo);

    ASSERT_TRUE(lower.correction.has_value());
    ASSERT_TRUE(tie.correction.has_value());
    EXPECT_EQ(lower.correction.value(), DecimalConverter::parseDecimal("10.02"));
    EXPECT_EQ(tie.correction.value(), DecimalConverter::parseDecimal("10.03"));
    EXPECT_EQ(tie.error, QString("Price must use ticks of 0.01."));
}

TEST(OrderInputValidationTest, ValidatesAvailablePriceAndNotionalLimits)
{
    const SymbolInfo symbolInfo = createSymbolInfo();

    EXPECT_EQ(OrderInputValidation::validatePrice("0.00", symbolInfo).error, QString("Value must be greater than 0."));
    EXPECT_EQ(OrderInputValidation::validatePrice("100000.01", symbolInfo).error,
              QString("Price is above the maximum 100000."));
    EXPECT_EQ(OrderInputValidation::validateNotional(DecimalConverter::parseDecimal("0.002"),
                                                     DecimalConverter::parseDecimal("4000"),
                                                     symbolInfo),
              QString("Notional 8 is below the minimum 10."));
    EXPECT_TRUE(OrderInputValidation::validateNotional(DecimalConverter::parseDecimal("0.002"),
                                                       DecimalConverter::parseDecimal("5000"),
                                                       symbolInfo)
                    .isEmpty());
}
