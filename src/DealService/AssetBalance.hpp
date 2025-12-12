#ifndef ASSET_BALANCE_H
#define ASSET_BALANCE_H

#include <string>

struct AssetBalance
{
    std::string asset;
    double free;
    double locked;
};


#endif