#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include "HttpRequestContext.hpp"

// Boost.Beast
#include <boost/beast/http.hpp>
// Boost.Asio
#include <boost/asio/ssl.hpp>

#include <iomanip>
#include <sstream>
#include <string>

std::string httpsGet(HttpRequestContext &context);

std::string httpsPost(HttpRequestContext &context);

#endif