#ifndef SYMBOL_INFO_H
#define SYMBOL_INFO_H

#include "type_aliasing.hpp"

#include <string>

struct SymbolInfo
{
    std::string symbol;
    std::string status;

    std::string baseAsset;  // Binance: baseAsset, Bybit: baseCoin
    std::string quoteAsset; // Binance: quoteAsset, Bybit: quoteCoin

    Decimal tickSize{}; // Binance PRICE_FILTER.tickSize / Bybit priceFilter.tickSize
    Decimal stepSize{}; // Binance LOT_SIZE.stepSize / Bybit lotSizeFilter.qtyStep

    Decimal minQty{}; // Binance LOT_SIZE.minQty / Bybit lotSizeFilter.minOrderQty
    Decimal maxQty{}; // Binance LOT_SIZE.maxQty / Bybit lotSizeFilter.maxOrderQty

    Decimal minPrice{}; // Binance PRICE_FILTER.minPrice / Bybit priceFilter.minPrice
    Decimal maxPrice{}; // Binance PRICE_FILTER.maxPrice / Bybit priceFilter.maxPrice

    Decimal minNotional{}; // Binance MIN_NOTIONAL/NOTIONAL / Bybit lotSizeFilter.minOrderAmt
    Decimal maxNotional{}; // Binance NOTIONAL.maxNotional (if any) / Bybit lotSizeFilter.maxOrderAmt

    int pricePrecision = 0; // Binance if present; Bybit: 0 (derive if needed)
    int qtyPrecision = 0;   // Binance if present; Bybit: 0 (derive if needed)
};

#endif