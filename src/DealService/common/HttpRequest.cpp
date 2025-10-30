#include "HttpRequest.hpp"

//DEBUG
#include <iostream>

using namespace std;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;
template <typename K, typename V>
using flat_map = boost::container::flat_map<K, V>;

tcp::resolver createResolver(net::io_context& ioc) {
    return tcp::resolver(ioc);
}

beast::ssl_stream<beast::tcp_stream> createNetworkStream(net::io_context& ioc, ssl::context& ctx) {
    return beast::ssl_stream<beast::tcp_stream>(ioc, ctx);
}

http::request<http::string_body> prepareRequest(const http::verb& type, tcp::resolver& resolver, beast::ssl_stream<beast::tcp_stream>& stream,
    const string& target, const string& host) {
    cout << "[*] Resolving host: " << host << endl;
    auto const results = resolver.resolve(host, "https");

    cout << "[*] Connecting..." << endl;
    beast::get_lowest_layer(stream).connect(results);

    if(!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
        throw beast::system_error{beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category())};

    cout << "[*] Performing SSL handshake..." << endl;
    stream.handshake(ssl::stream_base::client);
    cout << "[*] SSL handshake done" << endl;

    //TODO constant for the HTTP 1.1 protocol instead of 11
    //TODO instead of 'http::request<http::string_body>' may be 'http::request<http::empty_body>' - come up with something
    http::request<http::string_body> req{type, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    return req;
}

void setRequestHeaders(http::request<http::string_body>& req, const flat_map<string, string>& headers) {
    for (auto header: headers) {
        req.set(header.first, header.second);
    }
}

// https_get function unchanged except added logs:
string httpsGet(net::io_context& ioc, ssl::context& ctx, const string& target, const string& host) {
    try {
        auto resolver = createResolver(ioc);
        auto stream = createNetworkStream(ioc, ctx);
        auto req = prepareRequest(http::verb::get, resolver, stream, target, host);

        cout << "[*] Writing GET request..." << endl;
        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        cout << "[*] Reading GET response..." << endl;
        http::read(stream, buffer, res);

        cout << "---- HTTP RESPONSE ----\n" << res << "-----------------------\n";

        beast::error_code ec;
        stream.shutdown(ec);
        if(ec == net::error::eof || ec == ssl::error::stream_truncated)
            ec = {};
        if(ec)
            throw beast::system_error{ec};
        cout << "[*] Shutdown complete" << endl;

        return res.body();
    } catch(exception const& e) {
        cerr << "HTTPS GET error: " << e.what() << endl;
        return "";
    }
}

// https_post unchanged except added logs:
string httpsPost(net::io_context& ioc, ssl::context& ctx, const string& target, const string& host,
    const string& apiKey, const flat_map<string, string>& headers) {
    try {
        auto resolver = createResolver(ioc);
        auto stream = createNetworkStream(ioc, ctx);
        auto req = prepareRequest(http::verb::post, resolver, stream, target, host);

        setRequestHeaders(req, headers);

        cout << "---- HTTP REQUEST ----\n" << req << "----------------------\n";

        cout << "[*] Writing POST request..." << endl;
        http::write(stream, req);
        cout << "[*] Request sent" << endl;

        beast::flat_buffer buffer;
        http::response<http::string_body> res;

        cout << "[*] Reading response..." << endl;
        http::read(stream, buffer, res);

        cout << "---- HTTP RESPONSE ----\n" << res << "-----------------------\n";

        beast::error_code ec;
        stream.shutdown(ec);
        if(ec == net::error::eof || ec == ssl::error::stream_truncated)
            ec = {};
        if(ec)
            throw beast::system_error{ec};
        cout << "[*] Shutdown complete" << endl;

        return res.body();
    } catch(exception const& e) {
        cerr << "HTTPS POST error: " << e.what() << endl;
        return "";
    }
}