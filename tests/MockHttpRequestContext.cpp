#include "../src/DealService/common/HttpRequestContext.hpp"
#include <iostream>

void HttpRequestContext::prepareRequest(const http::verb &type)
{
    request.method(type);
    request.target(target);
    request.version(HTTP_PROTOCOL_VERSION);
    request.set(http::field::host, host);
    request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
}

const http::request<http::string_body> &HttpRequestContext::getRequest() const
{
    return request;
}

const beast::ssl_stream<beast::tcp_stream> &HttpRequestContext::getStream() const
{
    return stream;
}

beast::ssl_stream<beast::tcp_stream> &HttpRequestContext::getStream()
{
    return stream;
}

void HttpRequestContext::setRequestHeaders(const boost::container::flat_map<std::string, std::string> &headers)
{
    for (const auto &header : headers)
    {
        request.set(header.first, header.second);
    }
}

void HttpRequestContext::setRequestBody(const std::string &body)
{
    request.body() = body;
    request.prepare_payload();
}
