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

class HttpRequestContext
{
  private:
    const int HTTP_PROTOCOL_VERSION = 11;

    boost::asio::io_context &ioc;
    boost::asio::ssl::context &ctx;
    std::string host;
    std::string target;
    boost::asio::ip::tcp::resolver resolver;
    boost::beast::ssl_stream<boost::beast::tcp_stream> stream;
    boost::beast::http::request<boost::beast::http::string_body> request;

  public:
    HttpRequestContext(boost::asio::io_context &ioc,
                       boost::asio::ssl::context &ctx,
                       const std::string &host,
                       const std::string &target)
        : ioc(ioc), ctx(ctx), host(host), target(target), resolver(ioc), stream(ioc, ctx)
    {}

    void prepareRequest(const boost::beast::http::verb &type);

    const boost::beast::http::request<boost::beast::http::string_body> &getRequest() const;

    const boost::beast::ssl_stream<boost::beast::tcp_stream> &getStream() const;

    boost::beast::ssl_stream<boost::beast::tcp_stream> &getStream();

    void setRequestHeaders(const boost::container::flat_map<std::string, std::string> &headers);

    void setRequestBody(const std::string &body);
};

#endif