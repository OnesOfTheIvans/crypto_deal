#ifndef DEAL_SERVICE_TYPE_ALIASING_H
#define DEAL_SERVICE_TYPE_ALIASING_H

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

#include <chrono>

template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using msec = std::chrono::milliseconds::rep;

#endif