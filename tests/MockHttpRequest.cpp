#include "MockHttpRequest.hpp"
#include <iostream>

namespace {
    std::string getTargetFromContext(const HttpRequestContext &context)
    {
        return "";
    }
}

std::string httpsGet(HttpRequestContext &context)
{
    MockNetwork::instance().recordRequest(context);
    return MockNetwork::instance().getResponse(context.getTarget());
}

std::string httpsPost(HttpRequestContext &context)
{
    MockNetwork::instance().recordRequest(context);
    return MockNetwork::instance().getResponse(context.getTarget());
}
