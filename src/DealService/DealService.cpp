#include "DealService.hpp"
#include "common/OrderWaitInterrupted.hpp"

#include <algorithm>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <utility>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <openssl/hmac.h>

using namespace std;

namespace {
    void throwIfOrderWaitInterrupted(stop_token stopToken, const string &description)
    {
        if (stopToken.stop_requested())
        {
            throw OrderWaitInterrupted(description + " was interrupted");
        }
    }
}

OrderInfo DealService::waitUntilOrderFilled(const string &symbol, const string &orderId, stop_token stopToken)
{
    throwIfOrderWaitInterrupted(stopToken, "Order wait");
    return waitUntilOrderFilled(symbol, orderId);
}

OcoWaitResult DealService::waitUntilOcoOrderFilled(const OcoInfo &ocoInfo, stop_token stopToken)
{
    throwIfOrderWaitInterrupted(stopToken, "OCO wait");
    return waitUntilOcoOrderFilled(ocoInfo);
}

OrderInfo DealService::cancelOrderAndWaitUntilTerminal(const OrderQuery &request, stop_token stopToken)
{
    throwIfOrderWaitInterrupted(stopToken, "Order cancellation");
    return cancelOrderAndWaitUntilTerminal(request);
}

OcoInfo DealService::cancelOcoAndWaitUntilTerminal(const OcoInfo &ocoInfo, stop_token stopToken)
{
    throwIfOrderWaitInterrupted(stopToken, "OCO cancellation");
    return cancelOcoAndWaitUntilTerminal(ocoInfo);
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

void DealService::setUrlParameters(boost::urls::url &url, const map<string, string> &params) const
{
    for (const auto &[key, value] : params)
    {
        url.params().append({key, value});
    }
}

void DealService::notifyBalanceCacheChanged()
{
    function<void()> handler;
    {
        lock_guard<mutex> lock(userStreamEventHandlersMutex);
        handler = userStreamEventHandlers.balancesChanged;
    }

    if (handler)
    {
        try
        {
            handler();
        }
        catch (const exception &error)
        {
            cerr << "Balance cache observer failed: " << error.what() << endl;
        }
        catch (...)
        {
            cerr << "Balance cache observer failed with a non-standard exception" << endl;
        }
    }
}

void DealService::notifyUserStreamStatusChanged()
{
    function<void()> handler;
    {
        lock_guard<mutex> lock(userStreamEventHandlersMutex);
        handler = userStreamEventHandlers.statusChanged;
    }

    if (handler)
    {
        try
        {
            handler();
        }
        catch (const exception &error)
        {
            cerr << "User stream status observer failed: " << error.what() << endl;
        }
        catch (...)
        {
            cerr << "User stream status observer failed with a non-standard exception" << endl;
        }
    }
}

void DealService::setUserStreamEventHandlers(UserStreamEventHandlers handlers)
{
    lock_guard<mutex> lock(userStreamEventHandlersMutex);
    userStreamEventHandlers = move(handlers);
}

void DealService::clearUserStreamEventHandlers()
{
    lock_guard<mutex> lock(userStreamEventHandlersMutex);
    userStreamEventHandlers = {};
}

ExchangerType DealService::getExchangerType() const
{
    return exchangerType;
}

string DealService::generateUniqueOcoId() const
{
    boost::uuids::random_generator generator;
    string uuid = boost::uuids::to_string(generator());
    uuid.erase(remove(uuid.begin(), uuid.end(), '-'), uuid.end());

    return "O" + uuid;
}
