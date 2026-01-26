#ifndef MOCK_HTTP_REQUEST_H
#define MOCK_HTTP_REQUEST_H

#include "../src/DealService/common/HttpRequestContext.hpp"
#include <functional>
#include <map>
#include <queue>
#include <string>

class MockNetwork
{
  public:
    static MockNetwork &instance()
    {
        static MockNetwork inst;
        return inst;
    }

    void reset()
    {
        responses.clear();
        defaultResponse = "{}";
    }

    void setResponse(const std::string &targetSubstring, const std::string &response)
    {
        responses[targetSubstring] = response;
    }

    void setDefaultResponse(const std::string &response)
    {
        defaultResponse = response;
    }

    std::string getResponse(const std::string &target)
    {
        for (const auto &[key, resp] : responses)
        {
            if (target.find(key) != std::string::npos)
            {
                return resp;
            }
        }
        return defaultResponse;
    }

  private:
    std::map<std::string, std::string> responses;
    std::string defaultResponse = "{}";
};

std::string httpsGet(HttpRequestContext &context);
std::string httpsPost(HttpRequestContext &context);

#endif
