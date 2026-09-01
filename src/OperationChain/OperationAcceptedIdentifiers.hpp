#ifndef OPERATION_ACCEPTED_IDENTIFIERS_H
#define OPERATION_ACCEPTED_IDENTIFIERS_H

#include <optional>
#include <string>

struct OperationAcceptedIdentifiers
{
    std::optional<std::string> orderId;
    std::optional<std::string> ocoGroupId;
    std::optional<std::string> takeProfitOrderId;
    std::optional<std::string> stopLossOrderId;

    bool operator==(const OperationAcceptedIdentifiers &) const = default;
};

#endif
