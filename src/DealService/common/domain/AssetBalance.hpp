#ifndef ASSET_BALANCE_H
#define ASSET_BALANCE_H

#include <boost/decimal.hpp>

#include <string>

using Decimal = boost::decimal::decimal128_t;

struct AssetBalance
{
    std::string asset;
    Decimal free{};
    Decimal locked{};
};

#endif
