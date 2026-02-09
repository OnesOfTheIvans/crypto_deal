#include "HttpRequestContext.hpp"

// DEBUG
#include <iostream>

#include "type_aliasing.hpp"

using namespace std;

void HttpRequestContext::prepareRequest(const http::verb &type)
{
    auto const results = resolver.resolve(host, "https");

    beast::get_lowest_layer(stream).connect(results);

    if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
    {
        throw beast::system_error{
            beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category())};
    }

    stream.handshake(ssl::stream_base::client);

    request = {type, target, HTTP_PROTOCOL_VERSION};
    request.set(http::field::host, host);
    request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
}

void HttpRequestContext::setRequestHeaders(const flat_map<string, string> &headers)
{
    for (auto header : headers)
    {
        request.set(header.first, header.second);
    }
}

void HttpRequestContext::setRequestBody(const string &body)
{
    request.body() = body;
    request.prepare_payload();
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