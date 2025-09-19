#include "http_response.hpp"

using namespace std;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

// https_get function unchanged except added logs:
string https_get(net::io_context& ioc, ssl::context& ctx, const string& target, const string host) {
    try {
        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        cout << "[*] Resolving host: " << host << endl;
        auto const results = resolver.resolve(host, "https");

        cout << "[*] Connecting..." << endl;
        beast::get_lowest_layer(stream).connect(results);

        if(!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
            throw beast::system_error{beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category())};

        cout << "[*] Performing SSL handshake..." << endl;
        stream.handshake(ssl::stream_base::client);
        cout << "[*] SSL handshake done" << endl;

        http::request<http::empty_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

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
string https_post(net::io_context& ioc, ssl::context& ctx, const string& target, const string host, const string apiKey) {
    try {
        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        cout << "[*] Resolving host: " << host << endl;
        auto const results = resolver.resolve(host, "https");

        cout << "[*] Connecting..." << endl;
        beast::get_lowest_layer(stream).connect(results);

        if(!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
            throw beast::system_error{beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category())};

        cout << "[*] Performing SSL handshake..." << endl;
        stream.handshake(ssl::stream_base::client);
        cout << "[*] SSL handshake done" << endl;

        http::request<http::string_body> req{http::verb::post, target, 11};
        req.set(http::field::host, host);
        req.set("X-MBX-APIKEY", apiKey);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

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