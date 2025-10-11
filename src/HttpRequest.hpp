#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

//Boost.Beast
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/ssl.hpp>
//Boost.Asio
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
//OpenSSL
#include <openssl/hmac.h>

#include <string>
#include <sstream>
#include <iomanip>
//debug
#include <iostream>

std::string httpsGet(boost::asio::io_context& ioc, boost::asio::ssl::context& ctx, const std::string& target, const std::string host);
std::string httpsPost(boost::asio::io_context& ioc, boost::asio::ssl::context& ctx, const std::string& target, const std::string host, const std::string apiKey);

#endif