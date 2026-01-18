// BinanceSBEWebSocket.hpp
#ifndef BINANCE_SBE_WEBSOCKET_HPP
#define BINANCE_SBE_WEBSOCKET_HPP

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

class WebSocket {
private:
    std::string api_key_;
    net::io_context ioc_;
    ssl::context ctx_;
    tcp::resolver resolver_;
    websocket::stream<beast::ssl_stream<tcp::socket>> ws_;

public:
    WebSocket(const std::string& api_key);
    ~WebSocket();
    
    void connect(const std::string& host, const std::string& port, const std::string& path);
    void subscribe(const std::string& subscription_msg);
    void read_messages();
    void close();

    void print_binary_dump(const unsigned char* data, size_t len);
    void print_binary_hex(const unsigned char* data, size_t len);

    bool read_binary(std::vector<uint8_t>& out_buffer);
};

#endif // BINANCE_SBE_WEBSOCKET_HPP