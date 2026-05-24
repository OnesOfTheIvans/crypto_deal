#include "DealService.hpp"

#include <iomanip>
#include <sstream>

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

ExchangerType DealService::getExchangerType() const
{
    return exchangerType;
}
