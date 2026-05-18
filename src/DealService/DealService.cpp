#include "DealService.hpp"
#include "common/DecimalConverter.hpp"
#include "common/exception_handling.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

using namespace std;
using namespace exception_handling;
namespace json = boost::json;

namespace {
    template <typename T> optional<T *> optionalOfNullable(T *ptr)
    {
        if (ptr == nullptr)
        {
            return nullopt;
        }

        return ptr;
    }

    string fieldNameToString(json::string_view fieldName)
    {
        return string(fieldName.data(), fieldName.size());
    }

    optional<const json::value *> getJsonField(const json::object &sourceObject, json::string_view fieldName)
    {
        throwIf(fieldName.empty(), "Field name cannot be empty");
        return optionalOfNullable(sourceObject.if_contains(fieldName));
    }

    string typeErrorMessage(json::string_view fieldName, const string &expectedType)
    {
        return "Invalid " + fieldNameToString(fieldName) + ": expected " + expectedType;
    }

    long long parseTimestampText(string_view text, json::string_view fieldName)
    {
        long long parsedValue = 0;
        const auto *begin = text.data();
        const auto *end = begin + text.size();
        const auto result = from_chars(begin, end, parsedValue);
        throwIf(result.ec != errc{} || result.ptr != end, typeErrorMessage(fieldName, "timestamp integer string"));

        return parsedValue;
    }

    long long parseJsonIntegerToLongLong(const json::value &value, json::string_view fieldName)
    {
        if (value.is_int64())
        {
            return value.as_int64();
        }

        if (value.is_uint64())
        {
            const auto rawValue = value.as_uint64();
            throwIf(rawValue > static_cast<uint64_t>(numeric_limits<long long>::max()),
                    typeErrorMessage(fieldName, "integer in long long range"));

            return static_cast<long long>(rawValue);
        }

        throw runtime_error(typeErrorMessage(fieldName, "integer"));
    }

    long long parseJsonTimestampToLongLong(const json::value &value, json::string_view fieldName)
    {
        if (value.is_string())
        {
            return parseTimestampText(value.as_string().c_str(), fieldName);
        }

        return parseJsonIntegerToLongLong(value, fieldName);
    }

    int parseJsonIntegerToInt(const json::value &value, json::string_view fieldName)
    {
        const long long parsedValue = parseJsonIntegerToLongLong(value, fieldName);

        throwIf(parsedValue < numeric_limits<int>::min() || parsedValue > numeric_limits<int>::max(),
                typeErrorMessage(fieldName, "integer in int range"));

        return static_cast<int>(parsedValue);
    }
}

// Correct HMAC SHA256 returning hex string
string DealService::hmac_sha256(const string &key, const string &data) const
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(),
         key.data(),
         (int)key.size(),
         (const unsigned char *)data.data(),
         data.size(),
         digest,
         &digest_len);

    ostringstream oss;
    for (unsigned int i = 0; i < digest_len; ++i)
    {
        oss << hex << setw(2) << setfill('0') << (int)digest[i];
    }
    return oss.str();
}

ExchangerType DealService::getExchangerType() const
{
    return exchangerType;
}

void DealService::parseAndSetParameter(string &destinationField,
                                       const json::object &sourceObject,
                                       json::string_view fieldName,
                                       bool isOptional) const
{
    const optional<const json::value *> jsonField = getJsonField(sourceObject, fieldName);
    if (!jsonField.has_value())
    {
        throwIf(!isOptional, "Missing required string field: " + fieldNameToString(fieldName));
        return;
    }

    const json::value *value = jsonField.value();
    destinationField = [&]() -> string
    {
        switch (value->kind())
        {
        case json::kind::string:
            return value->as_string().c_str();

        case json::kind::int64:
            return to_string(value->as_int64());

        case json::kind::uint64:
            return to_string(value->as_uint64());

        default:
            throw runtime_error(typeErrorMessage(fieldName, "string or integer"));
        }
    }();
}

void DealService::parseAndSetParameter(Decimal &destinationField,
                                       const json::object &sourceObject,
                                       json::string_view fieldName,
                                       bool isOptional) const
{
    const optional<const json::value *> jsonField = getJsonField(sourceObject, fieldName);
    if (!jsonField.has_value())
    {
        throwIf(!isOptional, "Missing required Decimal field: " + fieldNameToString(fieldName));
        return;
    }

    const json::value *value = jsonField.value();
    throwIf(!value->is_string(), typeErrorMessage(fieldName, "decimal string"));

    const string_view text = value->as_string().c_str();
    destinationField = text.empty() ? Decimal{} : DecimalConverter::parseDecimal(text);
}

void DealService::parseAndSetParameter(long long &destinationField,
                                       const json::object &sourceObject,
                                       json::string_view fieldName,
                                       bool isOptional) const
{
    const optional<const json::value *> jsonField = getJsonField(sourceObject, fieldName);
    if (!jsonField.has_value())
    {
        throwIf(!isOptional, "Missing required long long field: " + fieldNameToString(fieldName));
        return;
    }

    const json::value *value = jsonField.value();
    destinationField = parseJsonTimestampToLongLong(*value, fieldName);
}

void DealService::parseAndSetParameter(int &destinationField,
                                       const json::object &sourceObject,
                                       json::string_view fieldName,
                                       bool isOptional) const
{
    const optional<const json::value *> jsonField = getJsonField(sourceObject, fieldName);
    if (!jsonField.has_value())
    {
        throwIf(!isOptional, "Missing required int field: " + fieldNameToString(fieldName));
        return;
    }

    const json::value *value = jsonField.value();
    destinationField = parseJsonIntegerToInt(*value, fieldName);
}
