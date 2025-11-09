#include "HttpRequest.hpp"

//DEBUG
#include <iostream>

using namespace std;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

string httpsGet(HttpRequestContext& context) {
    try {
        cout << "[*] Writing GET request..." << endl;
        http::write(context.getStream(), context.getRequest());

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        cout << "[*] Reading GET response..." << endl;
        http::read(context.getStream(), buffer, res);

        cout << "---- HTTP RESPONSE ----\n" << res << "-----------------------\n";

        beast::error_code ec;
        context.getStream().shutdown(ec);
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

string httpsPost(HttpRequestContext& context) {
    try {
        cout << "---- HTTP REQUEST ----\n" << context.getRequest() << "----------------------\n";

        cout << "[*] Writing POST request..." << endl;
        http::write(context.getStream(), context.getRequest());
        cout << "[*] Request sent" << endl;

        beast::flat_buffer buffer;
        http::response<http::string_body> res;

        cout << "[*] Reading response..." << endl;
        http::read(context.getStream(), buffer, res);

        cout << "---- HTTP RESPONSE ----\n" << res << "-----------------------\n";

        beast::error_code ec;
        context.getStream().shutdown(ec);
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