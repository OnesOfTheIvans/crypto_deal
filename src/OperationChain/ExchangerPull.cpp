#include "ExchangerPull.hpp"
#include "type_aliasing.hpp"

using namespace std;

ExchangerPull::ExchangerPull(const vector<Exchanger> &exchangers)
{
    for (const auto &exchanger : exchangers)
    {
        this->exchangers.insert(make_pair(exchanger->getExchangerType(), Exchanger(exchanger)));
    }
}

const Exchanger &ExchangerPull::getExchanger(ExchangerType type) const
{
    return exchangers.at(type);
}