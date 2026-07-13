#ifndef DEAL_SERVICE_TYPE_ALIASING_H
#define DEAL_SERVICE_TYPE_ALIASING_H

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/decimal.hpp>
#include <boost/json.hpp>

#include <chrono>

template <typename K, typename V> using flat_map = boost::container::flat_map<K, V>;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace ws = boost::beast::websocket;
using msec = std::chrono::milliseconds::rep;
using tcp = net::ip::tcp;
using Decimal = boost::decimal::decimal128_t;
using WebsocketStream = ws::stream<beast::ssl_stream<beast::tcp_stream>>;

namespace json = boost::json;

#endif
