#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <openssl/hmac.h>
#include <openssl/ssl.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

std::string host;
std::string api_key;
std::string secret_key;

void initConfigVariables() {
    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini("../config.ini", pt);
    host = pt.get<std::string>("API.BINANCE_HOST");
    api_key = pt.get<std::string>("API.BINANCE_API_KEY");
    secret_key = pt.get<std::string>("API.BINANCE_SECRET_KEY");
}

// Simple parser for {"serverTime":1754046335000}
uint64_t parse_server_time(const std::string& json_response) {
    auto pos = json_response.find("\"serverTime\":");
    if (pos == std::string::npos) return 0;
    pos += 13; // length of "\"serverTime\":"
    size_t end_pos = json_response.find_first_not_of("0123456789", pos);
    std::string time_str = json_response.substr(pos, end_pos - pos);
    try {
        return std::stoull(time_str);
    } catch (...) {
        return 0;
    }
}

// Correct HMAC SHA256 returning hex string
std::string hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(),
         key.data(), (int)key.size(),
         (const unsigned char*)data.data(), data.size(),
         digest, &digest_len);

    std::ostringstream oss;
    for (unsigned int i = 0; i < digest_len; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    return oss.str();
}

// https_get function unchanged except added logs:
std::string https_get(net::io_context& ioc, ssl::context& ctx, const std::string& target) {
    try {
        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        std::cout << "[*] Resolving host: " << host << std::endl;
        auto const results = resolver.resolve(host, "https");

        std::cout << "[*] Connecting..." << std::endl;
        beast::get_lowest_layer(stream).connect(results);

        if(!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
            throw beast::system_error{beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category())};

        std::cout << "[*] Performing SSL handshake..." << std::endl;
        stream.handshake(ssl::stream_base::client);
        std::cout << "[*] SSL handshake done" << std::endl;

        http::request<http::empty_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        std::cout << "[*] Writing GET request..." << std::endl;
        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        std::cout << "[*] Reading GET response..." << std::endl;
        http::read(stream, buffer, res);

        std::cout << "---- HTTP RESPONSE ----\n" << res << "-----------------------\n";

        beast::error_code ec;
        stream.shutdown(ec);
        if(ec == net::error::eof || ec == ssl::error::stream_truncated)
            ec = {};
        if(ec)
            throw beast::system_error{ec};
        std::cout << "[*] Shutdown complete" << std::endl;

        return res.body();
    } catch(std::exception const& e) {
        std::cerr << "HTTPS GET error: " << e.what() << std::endl;
        return "";
    }
}

// https_post unchanged except added logs:
std::string https_post(net::io_context& ioc, ssl::context& ctx, const std::string& target) {
    try {
        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        std::cout << "[*] Resolving host: " << host << std::endl;
        auto const results = resolver.resolve(host, "https");

        std::cout << "[*] Connecting..." << std::endl;
        beast::get_lowest_layer(stream).connect(results);

        if(!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
            throw beast::system_error{beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category())};

        std::cout << "[*] Performing SSL handshake..." << std::endl;
        stream.handshake(ssl::stream_base::client);
        std::cout << "[*] SSL handshake done" << std::endl;

        http::request<http::string_body> req{http::verb::post, target, 11};
        req.set(http::field::host, host);
        req.set("X-MBX-APIKEY", api_key);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        std::cout << "---- HTTP REQUEST ----\n" << req << "----------------------\n";

        std::cout << "[*] Writing POST request..." << std::endl;
        http::write(stream, req);
        std::cout << "[*] Request sent" << std::endl;

        beast::flat_buffer buffer;
        http::response<http::string_body> res;

        std::cout << "[*] Reading response..." << std::endl;
        http::read(stream, buffer, res);

        std::cout << "---- HTTP RESPONSE ----\n" << res << "-----------------------\n";

        beast::error_code ec;
        stream.shutdown(ec);
        if(ec == net::error::eof || ec == ssl::error::stream_truncated)
            ec = {};
        if(ec)
            throw beast::system_error{ec};
        std::cout << "[*] Shutdown complete" << std::endl;

        return res.body();
    } catch(std::exception const& e) {
        std::cerr << "HTTPS POST error: " << e.what() << std::endl;
        return "";
    }
}

int main() {
    initConfigVariables();
    net::io_context ioc;
    ssl::context ctx{ssl::context::sslv23_client};

    // Step 1: Get server time from Binance
    std::cout << "Fetching server time from Binance..." << std::endl;
    std::string time_response = https_get(ioc, ctx, "/api/v3/time");
    if (time_response.empty()) {
        std::cerr << "Failed to get server time. Exiting." << std::endl;
        return 1;
    }
    uint64_t server_time = parse_server_time(time_response);
    if (server_time == 0) {
        std::cerr << "Failed to parse server time. Exiting." << std::endl;
        return 1;
    }
    std::cout << "Server time: " << server_time << std::endl;

    // Step 2: Prepare query string using server_time, sign it and send test order
    std::ostringstream qs;
    qs << "symbol=USDCUSDT"
       << "&side=BUY"
       << "&type=MARKET"
       << "&quantity=100"
       << "&recvWindow=5000"
       << "&timestamp=" << server_time;

    std::string query_string = qs.str();
    std::string signature = hmac_sha256(secret_key, query_string);
    std::string full_query = query_string + "&signature=" + signature;

    std::string test_target = "/api/v3/order/test?" + full_query;

    std::cout << "Sending test order..." << std::endl;
    std::string response = https_post(ioc, ctx, test_target);
    std::cout << "Test order response: " << response << std::endl;

    if (response == "{}") {
        std::cout << "Test passed, sending real order..." << std::endl;
        std::string real_target = "/api/v3/order?" + full_query;
        std::string real_response = https_post(ioc, ctx, real_target);
        std::cout << "Real order response: " << real_response << std::endl;
    } else {
        std::cout << "Test order failed or unexpected response, not placing real order." << std::endl;
    }

    return 0;
}
