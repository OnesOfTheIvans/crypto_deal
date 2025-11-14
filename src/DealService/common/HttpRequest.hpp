#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include "HttpRequestContext.hpp"

//Boost.Beast
#include <boost/beast/http.hpp>
//Boost.Asio
#include <boost/asio/ssl.hpp>

#include <string>
#include <sstream>
#include <iomanip>

std::string httpsGet(HttpRequestContext& context);

std::string httpsPost(HttpRequestContext& context);

#endif