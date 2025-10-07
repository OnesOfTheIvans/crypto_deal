#ifndef ORDER_OPERATION_H
#define ORDER_OPERATION_H

#include <string>
#include <unordered_map>

enum class OrderOperation {
    BUY,
    SELL
};

namespace orderOperation {
    static const std::unordered_map<OrderOperation, std::string> operationToString {
        { OrderOperation::BUY,  "BUY" },
        { OrderOperation::SELL, "SELL" }
    };

    // inline std::string toString(const OrderOperation& operation) {
    //     return std::string(magic_enum::enum_name(operation)); // magic_enum third party lib must be included
    // }

    // inline std::string toString(const OrderOperation& operation) {
    //     switch (operation)
    //     {
    //     case Operation::BUY:
    //         return "BUY";
    //     case Operation::SELL:
    //         return "SELL";
    //     }
    // }
}

#endif