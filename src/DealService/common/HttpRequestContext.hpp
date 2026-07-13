#ifndef HTTP_REQUEST_CONTEXT_H
#define HTTP_REQUEST_CONTEXT_H

// Boost.Beast
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
// Boost.Asio
#include <boost/asio/ip/tcp.hpp>
// Boost.Containers
#include <boost/container/flat_map.hpp>

#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

class HttpRequestContext
{
  private:
    const int HTTP_PROTOCOL_VERSION = 11;

    net::io_context &ioc;
    ssl::context &ctx;
    std::string host;
    std::string target;
    net::ip::tcp::resolver resolver;
    beast::ssl_stream<beast::tcp_stream> stream;
    http::request<http::string_body> request;

  public:
    HttpRequestContext(net::io_context &ioc, ssl::context &ctx, const std::string &host, const std::string &target)
        : ioc(ioc), ctx(ctx), host(host), target(target), resolver(ioc), stream(ioc, ctx)
    {}

    void prepareRequest(const http::verb &type);

    const http::request<http::string_body> &getRequest() const;

    const beast::ssl_stream<beast::tcp_stream> &getStream() const;

    beast::ssl_stream<beast::tcp_stream> &getStream();

    std::string getTarget() const
    {
        return target;
    }

    void setRequestHeaders(const boost::container::flat_map<std::string, std::string> &headers);

    void setRequestBody(const std::string &body);
};

#endif