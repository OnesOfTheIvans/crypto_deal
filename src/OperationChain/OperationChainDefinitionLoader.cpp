#include "OperationChainDefinitionLoader.hpp"

#include "common/DecimalConverter.hpp"
#include "common/exception_handling.hpp"

#include <boost/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace exception_handling;
using namespace std;

namespace json = boost::json;

namespace {
    [[noreturn]] void throwSchemaError(const string &source, const string &path, const string &message)
    {
        throw runtime_error("Invalid operation-chain definitions in '" + source + "' at " + path + ": " + message);
    }

    string buildFieldPath(const string &path, string_view field)
    {
        return path + "." + string(field);
    }

    void validateFields(const json::object &object,
                        initializer_list<string_view> allowedFields,
                        const string &source,
                        const string &path)
    {
        for (const auto &member : object)
        {
            const string_view field(member.key().data(), member.key().size());
            if (find(allowedFields.begin(), allowedFields.end(), field) == allowedFields.end())
            {
                throwSchemaError(source, buildFieldPath(path, field), "unknown field");
            }
        }
    }

    const json::value &
    getRequiredValue(const json::object &object, string_view field, const string &source, const string &path)
    {
        const auto member = object.find(json::string_view(field.data(), field.size()));
        if (member == object.end())
        {
            throwSchemaError(source, buildFieldPath(path, field), "required field is missing");
        }

        return member->value();
    }

    const json::object &
    getRequiredObject(const json::object &object, string_view field, const string &source, const string &path)
    {
        const json::value &value = getRequiredValue(object, field, source, path);
        if (!value.is_object())
        {
            throwSchemaError(source, buildFieldPath(path, field), "expected an object");
        }

        return value.as_object();
    }

    const json::array &
    getRequiredArray(const json::object &object, string_view field, const string &source, const string &path)
    {
        const json::value &value = getRequiredValue(object, field, source, path);
        if (!value.is_array())
        {
            throwSchemaError(source, buildFieldPath(path, field), "expected an array");
        }

        return value.as_array();
    }

    string getRequiredString(const json::object &object, string_view field, const string &source, const string &path)
    {
        const json::value &value = getRequiredValue(object, field, source, path);
        if (!value.is_string() || value.as_string().empty())
        {
            throwSchemaError(source, buildFieldPath(path, field), "expected a non-empty string");
        }

        return string(value.as_string().data(), value.as_string().size());
    }

    optional<string>
    getOptionalString(const json::object &object, string_view field, const string &source, const string &path)
    {
        const auto member = object.find(json::string_view(field.data(), field.size()));
        if (member == object.end())
        {
            return nullopt;
        }
        if (!member->value().is_string() || member->value().as_string().empty())
        {
            throwSchemaError(source, buildFieldPath(path, field), "expected a non-empty string");
        }

        return string(member->value().as_string().data(), member->value().as_string().size());
    }

    bool hasPositiveDecimalSyntax(string_view text)
    {
        const auto decimalPoint = text.find('.');
        if (text.empty() || decimalPoint == 0 || decimalPoint == text.size() - 1 ||
            text.find('.', decimalPoint == string_view::npos ? text.size() : decimalPoint + 1) != string_view::npos)
        {
            return false;
        }

        return all_of(text.begin(),
                      text.end(),
                      [](char character)
                      { return character == '.' || isdigit(static_cast<unsigned char>(character)) != 0; });
    }

    Decimal parsePositiveDecimal(string_view text, const string &source, const string &path)
    {
        if (!hasPositiveDecimalSyntax(text))
        {
            throwSchemaError(source, path, "expected a positive fixed decimal string");
        }

        Decimal value;
        try
        {
            value = DecimalConverter::parseDecimal(text);
        }
        catch (const runtime_error &)
        {
            throwSchemaError(source, path, "expected a positive fixed decimal string");
        }

        if (value <= 0)
        {
            throwSchemaError(source, path, "expected a value greater than zero");
        }
        return value;
    }

    Decimal
    getRequiredPositiveDecimal(const json::object &object, string_view field, const string &source, const string &path)
    {
        const string fieldPath = buildFieldPath(path, field);
        return parsePositiveDecimal(getRequiredString(object, field, source, path), source, fieldPath);
    }

    optional<Decimal>
    getOptionalPositiveDecimal(const json::object &object, string_view field, const string &source, const string &path)
    {
        const optional<string> text = getOptionalString(object, field, source, path);
        if (!text.has_value())
        {
            return nullopt;
        }

        return parsePositiveDecimal(text.value(), source, buildFieldPath(path, field));
    }

    ExchangerType parseExchangerType(string_view text, const string &source, const string &path)
    {
        if (text == "BINANCE")
        {
            return ExchangerType::BINANCE;
        }
        if (text == "BYBIT")
        {
            return ExchangerType::BYBIT;
        }

        throwSchemaError(source, path, "expected BINANCE or BYBIT");
    }

    OrderOperation parseOrderOperation(string_view text, const string &source, const string &path)
    {
        if (text == "BUY")
        {
            return OrderOperation::BUY;
        }
        if (text == "SELL")
        {
            return OrderOperation::SELL;
        }

        throwSchemaError(source, path, "expected BUY or SELL");
    }

    OrderType parseOrderType(string_view text, const string &source, const string &path)
    {
        if (text == "MARKET")
        {
            return OrderType::MARKET;
        }
        if (text == "LIMIT")
        {
            return OrderType::LIMIT;
        }

        throwSchemaError(source, path, "expected MARKET or LIMIT");
    }

    OperationType parseOperationType(string_view text, const string &source, const string &path)
    {
        if (text == "BUY_CRYPTO")
        {
            return OperationType::BUY_CRYPTO;
        }
        if (text == "SELL_CRYPTO")
        {
            return OperationType::SELL_CRYPTO;
        }
        if (text == "PLACE_ORDER")
        {
            return OperationType::PLACE_ORDER;
        }
        if (text == "PLACE_OCO")
        {
            return OperationType::PLACE_OCO;
        }
        if (text == "SEND_TO")
        {
            return OperationType::SEND_TO;
        }

        throwSchemaError(source, path, "expected BUY_CRYPTO, SELL_CRYPTO, PLACE_ORDER, PLACE_OCO, or SEND_TO");
    }

    BaseConfig parseBaseConfig(const json::object &object, const string &source, const string &path)
    {
        validateFields(object, {"outAsset"}, source, path);

        BaseConfig config;
        config.outAsset = getRequiredString(object, "outAsset", source, path);
        return config;
    }

    PlaceOrderConfig parsePlaceOrderConfig(const json::object &object, const string &source, const string &path)
    {
        validateFields(
            object,
            {"outAsset", "side", "orderType", "price", "timeInForce", "triggerPrice", "orderFilter", "marketUnit"},
            source,
            path);

        PlaceOrderConfig config;
        config.outAsset = getRequiredString(object, "outAsset", source, path);
        config.side =
            parseOrderOperation(getRequiredString(object, "side", source, path), source, buildFieldPath(path, "side"));
        config.type = parseOrderType(getRequiredString(object, "orderType", source, path),
                                     source,
                                     buildFieldPath(path, "orderType"));

        const optional<Decimal> price = getOptionalPositiveDecimal(object, "price", source, path);
        config.timeInForce = getOptionalString(object, "timeInForce", source, path);
        if (config.type == OrderType::LIMIT && !price.has_value())
        {
            throwSchemaError(source, buildFieldPath(path, "price"), "required for a LIMIT order");
        }
        if (config.type == OrderType::MARKET && price.has_value())
        {
            throwSchemaError(source, buildFieldPath(path, "price"), "not allowed for a MARKET order");
        }
        if (config.type == OrderType::MARKET && config.timeInForce.has_value())
        {
            throwSchemaError(source, buildFieldPath(path, "timeInForce"), "not allowed for a MARKET order");
        }
        if (price.has_value())
        {
            config.price = price.value();
        }

        config.triggerPrice = getOptionalString(object, "triggerPrice", source, path);
        if (config.triggerPrice.has_value())
        {
            parsePositiveDecimal(config.triggerPrice.value(), source, buildFieldPath(path, "triggerPrice"));
        }
        config.orderFilter = getOptionalString(object, "orderFilter", source, path);
        config.marketUnit = getOptionalString(object, "marketUnit", source, path);
        if (config.type == OrderType::LIMIT && config.marketUnit.has_value())
        {
            throwSchemaError(source, buildFieldPath(path, "marketUnit"), "not allowed for a LIMIT order");
        }
        return config;
    }

    PlaceOcoConfig parsePlaceOcoConfig(const json::object &object, const string &source, const string &path)
    {
        validateFields(object,
                       {"outAsset", "side", "price", "stopPrice", "stopLimitPrice", "stopLimitTimeInForce"},
                       source,
                       path);

        PlaceOcoConfig config;
        config.outAsset = getRequiredString(object, "outAsset", source, path);
        config.side =
            parseOrderOperation(getRequiredString(object, "side", source, path), source, buildFieldPath(path, "side"));
        config.price = getRequiredPositiveDecimal(object, "price", source, path);
        config.stopPrice = getRequiredPositiveDecimal(object, "stopPrice", source, path);
        config.stopLimitPrice = getOptionalPositiveDecimal(object, "stopLimitPrice", source, path);
        config.stopLimitTimeInForce = getOptionalString(object, "stopLimitTimeInForce", source, path);

        if (config.stopLimitPrice.has_value() != config.stopLimitTimeInForce.has_value())
        {
            throwSchemaError(source,
                             path,
                             "stopLimitPrice and stopLimitTimeInForce must either both be present or both be absent");
        }

        return config;
    }

    SendToConfig parseSendToConfig(const json::object &object, const string &source, const string &path)
    {
        validateFields(object, {"destinationExchange", "chain", "address"}, source, path);

        SendToConfig config;
        config.destinationExchanger = parseExchangerType(getRequiredString(object, "destinationExchange", source, path),
                                                         source,
                                                         buildFieldPath(path, "destinationExchange"));
        config.chain = getRequiredString(object, "chain", source, path);
        config.address = getRequiredString(object, "address", source, path);
        return config;
    }

    OperationDefinition parseOperationDefinition(const json::value &value, const string &source, const string &path)
    {
        if (!value.is_object())
        {
            throwSchemaError(source, path, "expected an object");
        }

        const json::object &object = value.as_object();
        validateFields(object, {"type", "config"}, source, path);
        const OperationType type =
            parseOperationType(getRequiredString(object, "type", source, path), source, buildFieldPath(path, "type"));
        const json::object &config = getRequiredObject(object, "config", source, path);
        const string configPath = buildFieldPath(path, "config");

        switch (type)
        {
        case OperationType::BUY_CRYPTO:
        case OperationType::SELL_CRYPTO:
            return OperationDefinition(type, parseBaseConfig(config, source, configPath));
        case OperationType::PLACE_ORDER:
            return OperationDefinition(type, parsePlaceOrderConfig(config, source, configPath));
        case OperationType::PLACE_OCO:
            return OperationDefinition(type, parsePlaceOcoConfig(config, source, configPath));
        case OperationType::SEND_TO:
            return OperationDefinition(type, parseSendToConfig(config, source, configPath));
        }

        throwSchemaError(source, buildFieldPath(path, "type"), "unsupported operation type");
    }

    OperationChainDefinition parseChainDefinition(const json::value &value, const string &source, const string &path)
    {
        if (!value.is_object())
        {
            throwSchemaError(source, path, "expected an object");
        }

        const json::object &object = value.as_object();
        validateFields(object, {"name", "initial", "operations"}, source, path);
        const string name = getRequiredString(object, "name", source, path);

        const json::object &initial = getRequiredObject(object, "initial", source, path);
        const string initialPath = buildFieldPath(path, "initial");
        validateFields(initial, {"exchange", "asset", "quantity"}, source, initialPath);
        const ExchangerType exchangerType =
            parseExchangerType(getRequiredString(initial, "exchange", source, initialPath),
                               source,
                               buildFieldPath(initialPath, "exchange"));
        const string asset = getRequiredString(initial, "asset", source, initialPath);
        const Decimal quantity = getRequiredPositiveDecimal(initial, "quantity", source, initialPath);

        const json::array &operationValues = getRequiredArray(object, "operations", source, path);
        if (operationValues.empty())
        {
            throwSchemaError(source, buildFieldPath(path, "operations"), "expected at least one operation");
        }

        vector<OperationDefinition> operations;
        operations.reserve(operationValues.size());
        for (size_t index = 0; index < operationValues.size(); ++index)
        {
            operations.push_back(
                parseOperationDefinition(operationValues[index],
                                         source,
                                         buildFieldPath(path, "operations") + "[" + to_string(index) + "]"));
        }

        return OperationChainDefinition(name, exchangerType, asset, quantity, move(operations));
    }

    vector<OperationChainDefinition> parseDefinitions(string_view text, const string &source)
    {
        boost::system::error_code errorCode;
        const json::value document = json::parse(text, errorCode);
        if (errorCode)
        {
            throw runtime_error("Failed to parse operation-chain definitions file '" + source +
                                "': " + errorCode.message());
        }
        if (!document.is_object())
        {
            throwSchemaError(source, "$", "expected a root object");
        }

        const json::object &root = document.as_object();
        validateFields(root, {"schemaVersion", "chains"}, source, "$");
        const json::value &schemaVersion = getRequiredValue(root, "schemaVersion", source, "$");
        const bool isSupportedVersion = (schemaVersion.is_int64() && schemaVersion.as_int64() == 1) ||
                                        (schemaVersion.is_uint64() && schemaVersion.as_uint64() == 1);
        if (!isSupportedVersion)
        {
            throwSchemaError(source, "$.schemaVersion", "expected integer value 1");
        }

        const json::array &chainValues = getRequiredArray(root, "chains", source, "$");
        vector<OperationChainDefinition> definitions;
        definitions.reserve(chainValues.size());
        unordered_set<string> names;
        for (size_t index = 0; index < chainValues.size(); ++index)
        {
            const string path = "$.chains[" + to_string(index) + "]";
            OperationChainDefinition definition = parseChainDefinition(chainValues[index], source, path);
            if (!names.insert(definition.getName()).second)
            {
                throwSchemaError(source, buildFieldPath(path, "name"), "chain names must be unique");
            }
            definitions.push_back(move(definition));
        }

        return definitions;
    }
}

vector<OperationChainDefinition> OperationChainDefinitionLoader::load(const filesystem::path &filePath)
{
    ifstream input(filePath);
    throwIf(!input.is_open(), "Failed to open operation-chain definitions file '" + filePath.string() + "'");

    ostringstream contents;
    contents << input.rdbuf();
    throwIf(input.bad(), "Failed to read operation-chain definitions file '" + filePath.string() + "'");

    return parseDefinitions(contents.str(), filePath.string());
}
