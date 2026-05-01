#ifndef DECIMAL_CONVERTER_HPP
#define DECIMAL_CONVERTER_HPP

#include <boost/decimal.hpp>

#include <string>
#include <string_view>

using Decimal = boost::decimal::decimal128_t;

class DecimalConverter
{
  public:
    static Decimal parseDecimal(std::string_view text);
    static int decimalsFromStep(Decimal step);
    static std::string formatDecimal(Decimal value);
    static std::string formatDecimal(Decimal value, int decimals);
    static std::string formatByStep(Decimal value, Decimal stepSize);
    static Decimal ceilToStep(Decimal value, Decimal stepSize);
    static Decimal floorToStep(Decimal value, Decimal stepSize);
};

#endif
