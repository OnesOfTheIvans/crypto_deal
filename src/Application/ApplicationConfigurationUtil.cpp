#include "ApplicationConfigurationUtil.hpp"

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

using namespace std;

namespace {
    bool hasNonWhitespaceCharacter(const string &value)
    {
        return any_of(value.begin(), value.end(), [](unsigned char character) { return !isspace(character); });
    }

    string getRequiredConfigurationValue(const boost::property_tree::ptree &configurationTree, const string &key)
    {
        const auto value = configurationTree.get_optional<string>(key);
        if (!value.has_value())
        {
            throw runtime_error("Missing required application configuration key: " + key);
        }

        if (!hasNonWhitespaceCharacter(value.value()))
        {
            throw runtime_error("Application configuration value is empty for key: " + key);
        }

        return value.value();
    }

    ExchangeConfiguration loadExchangeConfiguration(const boost::property_tree::ptree &configurationTree,
                                                    const string &keyPrefix)
    {
        return {
            .host = getRequiredConfigurationValue(configurationTree, keyPrefix + "HOST"),
            .websocketHost = getRequiredConfigurationValue(configurationTree, keyPrefix + "WEBSOCKET_HOST"),
            .apiKey = getRequiredConfigurationValue(configurationTree, keyPrefix + "API_KEY"),
            .secretKey = getRequiredConfigurationValue(configurationTree, keyPrefix + "SECRET_KEY"),
        };
    }
}

ApplicationConfiguration loadApplicationConfiguration(const filesystem::path &path)
{
    boost::property_tree::ptree configurationTree;
    try
    {
        boost::property_tree::ini_parser::read_ini(path.string(), configurationTree);
    }
    catch (const boost::property_tree::ini_parser_error &)
    {
        throw runtime_error("Unable to read application configuration file: " + path.string());
    }

    return {
        .binance = loadExchangeConfiguration(configurationTree, "API.BINANCE_"),
        .bybit = loadExchangeConfiguration(configurationTree, "API.BYBIT_"),
    };
}
