#ifndef ORDER_INPUT_VALIDATION_H
#define ORDER_INPUT_VALIDATION_H

#include "common/domain/SymbolInfo.hpp"

#include <QString>

#include <optional>

struct DecimalInputValidation
{
    std::optional<Decimal> value;
    std::optional<Decimal> correction;
    QString error;

    bool isValid() const;
};

class OrderInputValidation
{
  public:
    static DecimalInputValidation validateQuantity(const QString &text, const SymbolInfo &symbolInfo);

    static DecimalInputValidation validatePrice(const QString &text, const SymbolInfo &symbolInfo);

    static QString validateNotional(Decimal quantity, Decimal price, const SymbolInfo &symbolInfo);
};

#endif
