#include "DealService.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <openssl/hmac.h>

using namespace std;

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
