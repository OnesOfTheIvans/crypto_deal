#include "ExchangerPull.hpp"
#include "type_aliasing.hpp"

using namespace std;

void ExchangerPull::addExchanger(Exchanger &exchanger)
{
    exchangers.insert(make_pair(exchanger->getExchangerType(), Exchanger(exchanger)));
}

Exchanger &ExchangerPull::getExchanger(ExchangerType type)
{
    return exchangers.at(type);
}