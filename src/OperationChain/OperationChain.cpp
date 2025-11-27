#include "../DealService/DealService.hpp" //TODO wrong path

#include <boost/container/flat_map.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Context;
using operation = std::function<Context(Context)>;

enum class ExchangerType
{
    BINANCE,
    BYBIT
};

class Context
{
    ExchangerType target;
    ExchangerType destination;

    std::vector<std::unique_ptr<DealService>> exchangersPull;

    double quantity;
};

class ContextManager
{
    Context context;

    std::vector<operation> operations{[](Context context)
                                      {
                                          // code
                                          return Context();
                                      },
                                      [](Context context)
                                      {
                                          // code
                                          return Context();
                                      }};
};

boost::container::flat_map<std::string, operation> allOperations{{"first",
                                                                  [](Context context)
                                                                  {
                                                                      // code
                                                                      return Context();
                                                                  }},
                                                                 {"second",
                                                                  [](Context context)
                                                                  {
                                                                      // code
                                                                      return Context();
                                                                  }}};