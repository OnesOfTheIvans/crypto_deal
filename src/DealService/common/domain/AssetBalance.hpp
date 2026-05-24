#ifndef ASSET_BALANCE_H
#define ASSET_BALANCE_H

#include "common/type_aliasing.hpp"

#include <string>

struct AssetBalance
{
    std::string asset;
    Decimal free{};
    Decimal locked{};
};

#endif