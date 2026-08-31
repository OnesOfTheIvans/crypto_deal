#include "OperationDefinition.hpp"

#include "common/exception_handling.hpp"

#include <utility>

using namespace exception_handling;
using namespace std;

namespace {
    bool isConfigCompatible(OperationType type, const Config &config)
    {
        switch (type)
        {
        case OperationType::BUY_CRYPTO:
        case OperationType::SELL_CRYPTO:
            return holds_alternative<BaseConfig>(config);
        case OperationType::PLACE_ORDER:
            return holds_alternative<PlaceOrderConfig>(config);
        case OperationType::PLACE_OCO:
            return holds_alternative<PlaceOcoConfig>(config);
        case OperationType::SEND_TO:
            return holds_alternative<SendToConfig>(config);
        }

        return false;
    }
}

OperationDefinition::OperationDefinition(OperationType type, Config config) : type(type), config(move(config))
{
    throwIf(!isConfigCompatible(type, this->config), "Operation definition config does not match its operation type");
}

OperationType OperationDefinition::getType() const
{
    return type;
}

const Config &OperationDefinition::getConfig() const
{
    return config;
}
