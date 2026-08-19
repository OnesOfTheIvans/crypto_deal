#ifndef TRADABLE_PAIR_H
#define TRADABLE_PAIR_H

#include <string>

struct TradablePair
{
    std::string symbol;
    std::string baseAsset;
    std::string quoteAsset;

    bool operator==(const TradablePair &) const = default;
};

#endif
