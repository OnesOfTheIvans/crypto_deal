#ifndef EXCHANGE_CONFIGURATION_H
#define EXCHANGE_CONFIGURATION_H

#include <string>

struct ExchangeConfiguration
{
    std::string host;
    std::string websocketHost;
    std::string apiKey;
    std::string secretKey;
};

#endif
