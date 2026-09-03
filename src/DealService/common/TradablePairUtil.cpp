#include "TradablePairUtil.hpp"

#include <algorithm>
#include <set>
#include <tuple>

namespace {
    bool isTradablePairComplete(const TradablePair &pair)
    {
        return !pair.symbol.empty() && !pair.baseAsset.empty() && !pair.quoteAsset.empty();
    }

    bool isTradablePairOrderedBefore(const TradablePair &left, const TradablePair &right)
    {
        return std::tie(left.baseAsset, left.quoteAsset, left.symbol) <
               std::tie(right.baseAsset, right.quoteAsset, right.symbol);
    }
}

std::vector<TradablePair> filterAndSortTradablePairs(std::vector<TradablePair> pairs)
{
    std::erase_if(pairs, [](const TradablePair &pair) { return !isTradablePairComplete(pair); });
    std::ranges::sort(pairs, isTradablePairOrderedBefore);

    std::set<std::string> symbols;
    std::erase_if(pairs, [&symbols](const TradablePair &pair) { return !symbols.insert(pair.symbol).second; });

    return pairs;
}
