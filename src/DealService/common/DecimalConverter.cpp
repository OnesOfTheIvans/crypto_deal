#include "DecimalConverter.hpp"
#include "exception_handling.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <system_error>

using namespace exception_handling;

namespace
{
constexpr int MaxDecimalPlaces = 34;
constexpr std::size_t DecimalBufferSize = 128;

std::string trimTrailingZeros(std::string text)
{
    while (text.size() > 1 && text.back() == '0')
    {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.')
    {
        text.pop_back();
    }
    if (text == "-0")
    {
        text = "0";
    }
    return text;
}
} // namespace

Decimal DecimalConverter::parseDecimal(std::string_view text)
{
    Decimal value;
    const auto result = boost::decimal::from_chars(text.data(), text.data() + text.size(), value);

    throwIf(result.ec != std::errc{} || result.ptr != text.data() + text.size(), "Invalid decimal value");

    return value;
}

int DecimalConverter::decimalsFromStep(Decimal step)
{
    if (step <= 0)
    {
        return 0;
    }

    const std::string stepText = formatDecimal(step);
    const auto dotPos = stepText.find('.');
    if (dotPos == std::string::npos)
    {
        return 0;
    }

    return static_cast<int>(stepText.size() - dotPos - 1);
}

std::string DecimalConverter::formatDecimal(Decimal value)
{
    return formatDecimal(value, MaxDecimalPlaces);
}

std::string DecimalConverter::formatDecimal(Decimal value, int decimals)
{
    decimals = std::clamp(decimals, 0, MaxDecimalPlaces);

    std::array<char, DecimalBufferSize> buffer{};
    const auto result = boost::decimal::to_chars(buffer.data(),
                                                 buffer.data() + buffer.size(),
                                                 value,
                                                 boost::decimal::chars_format::fixed,
                                                 decimals);
    throwIf(result.ec != std::errc{}, "formatDecimal: failed to format decimal value");

    return trimTrailingZeros(std::string(buffer.data(), result.ptr));
}

std::string DecimalConverter::formatByStep(Decimal value, Decimal stepSize)
{
    if (stepSize <= 0)
    {
        return formatDecimal(value);
    }

    return formatDecimal(value, decimalsFromStep(stepSize));
}

Decimal DecimalConverter::ceilToStep(Decimal value, Decimal stepSize)
{
    if (stepSize <= 0)
    {
        return value;
    }

    return boost::decimal::ceil(value / stepSize) * stepSize;
}

Decimal DecimalConverter::floorToStep(Decimal value, Decimal stepSize)
{
    if (stepSize <= 0)
    {
        return value;
    }

    return boost::decimal::floor(value / stepSize) * stepSize;
}
