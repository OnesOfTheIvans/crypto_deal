#ifndef SYMBOL_INFO_H
#define SYMBOL_INFO_H

#include <string>

struct SymbolInfo
{
    std::string symbol;
    std::string status;

    std::string baseAsset;  // Binance: baseAsset, Bybit: baseCoin
    std::string quoteAsset; // Binance: quoteAsset, Bybit: quoteCoin

    double tickSize = 0.0; // Binance PRICE_FILTER.tickSize / Bybit priceFilter.tickSize
    double stepSize = 0.0; // Binance LOT_SIZE.stepSize / Bybit lotSizeFilter.qtyStep

    double minQty = 0.0; // Binance LOT_SIZE.minQty / Bybit lotSizeFilter.minOrderQty
    double maxQty = 0.0; // Binance LOT_SIZE.maxQty / Bybit lotSizeFilter.maxOrderQty

    double minPrice = 0.0; // Binance PRICE_FILTER.minPrice / Bybit priceFilter.minPrice
    double maxPrice = 0.0; // Binance PRICE_FILTER.maxPrice / Bybit priceFilter.maxPrice

    double minNotional = 0.0; // Binance MIN_NOTIONAL/NOTIONAL / Bybit lotSizeFilter.minOrderAmt
    double maxNotional = 0.0; // Binance NOTIONAL.maxNotional (if any) / Bybit lotSizeFilter.maxOrderAmt

    int pricePrecision = 0; // Binance if present; Bybit: 0 (derive if needed)
    int qtyPrecision = 0;   // Binance if present; Bybit: 0 (derive if needed)
};

#endif
