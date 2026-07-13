#include "http_request.hpp"

// DEBUG
#include <iostream>

#include "type_aliasing.hpp"

using namespace std;

string httpsGet(HttpRequestContext &context)
{
    try
    {
        http::write(context.getStream(), context.getRequest());

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(context.getStream(), buffer, res);

        beast::error_code ec;
        context.getStream().shutdown(ec);
        if (ec == net::error::eof || ec == ssl::error::stream_truncated)
        {
            ec = {};
        }
        if (ec)
        {
            throw beast::system_error{ec};
        }

        return res.body();
    }
    catch (exception const &e)
    {
        cerr << "HTTPS GET error: " << e.what() << endl;
        return "";
    }
}

string httpsPost(HttpRequestContext &context)
{
    try
    {
        http::write(context.getStream(), context.getRequest());

        beast::flat_buffer buffer;
        http::response<http::string_body> res;

        http::read(context.getStream(), buffer, res);

        beast::error_code ec;
        context.getStream().shutdown(ec);
        if (ec == net::error::eof || ec == ssl::error::stream_truncated)
        {
            ec = {};
        }
        if (ec)
        {
            throw beast::system_error{ec};
        }

        return res.body();
    }
    catch (exception const &e)
    {
        cerr << "HTTPS POST error: " << e.what() << endl;
        return "";
    }
}