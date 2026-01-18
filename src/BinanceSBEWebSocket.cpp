//BinanceSBEWebSocket.cpp
#include "BinanceSBEWebSocket.hpp"
#include <iostream>

WebSocket::WebSocket(const std::string& api_key)
    : api_key_(api_key)
    , ctx_(ssl::context::tlsv12_client)
    , resolver_(ioc_)
    , ws_(ioc_, ctx_) {
    // Load default root certificates
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(ssl::verify_peer);
}

WebSocket::~WebSocket() {
    close();
}

void WebSocket::connect(const std::string& host, const std::string& port, const std::string& path) {
    try {
        // Resolve hostname
        auto const results = resolver_.resolve(host, port);
        
        // Connect to the IP address
        [[maybeunused]]auto ep = net::connect(get_lowest_layer(ws_), results);
        
        // Set SNI Hostname
        if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host.c_str())) {
            throw beast::system_error(
                beast::error_code(
                    static_cast<int>(::ERR_get_error()),
                    net::error::get_ssl_category()
                ),
                "Failed to set SNI Hostname"
            );
        }
        
        // Perform SSL handshake
        ws_.next_layer().handshake(ssl::stream_base::client);
        
        // Set WebSocket handshake decorator to add API key header
        ws_.set_option(websocket::stream_base::decorator(
            [this](websocket::request_type& req) {
                req.set(http::field::user_agent, "BinanceSBEClient/1.0");
                req.set("X-MBX-APIKEY", this->api_key_);
            }
        ));
        
        // Perform WebSocket handshake
        ws_.handshake(host, path);
        
        std::cout << "Connected to " << host << ":" << port << path << std::endl;
        
    } catch (std::exception const& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        throw;
    }
}

void WebSocket::subscribe(const std::string& subscription_msg) {
    try {
        ws_.write(net::buffer(subscription_msg));
        std::cout << "Subscription sent: " << subscription_msg << std::endl;
        
        // Read subscription response (will be in text/JSON format)
        beast::flat_buffer buffer;
        ws_.read(buffer);
        
        std::cout << "Subscription response: " << beast::make_printable(buffer.data()) << std::endl;
    } catch (std::exception const& e) {
        std::cerr << "Subscription error: " << e.what() << std::endl;
        throw;
    }
}

void WebSocket::print_binary_dump(const unsigned char* data, size_t len) {
    std::cout << "Binary dump (hex + ASCII):" << std::endl;
    for (size_t i = 0; i < len; i += 16) {
        // Print offset
        std::cout << std::hex << std::setfill('0') << std::setw(8) << i << "  ";
        
        // Print hex bytes
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < len) {
                std::cout << std::setw(2) << static_cast<int>(data[i + j]) << " ";
            } else {
                std::cout << "   ";
            }
            if (j == 7) std::cout << " ";
        }
        
        std::cout << " |";
        
        // Print ASCII representation
        for (size_t j = 0; j < 16 && (i + j) < len; ++j) {
            unsigned char c = data[i + j];
            std::cout << (c >= 32 && c <= 126 ? static_cast<char>(c) : '.');
        }
        
        std::cout << "|" << std::endl;
    }
    std::cout << std::dec << std::endl;
}

void WebSocket::print_binary_hex(const unsigned char* data, size_t len) {
    std::cout << "Raw binary data (hex):" << std::endl;
    for (size_t i = 0; i < len; ++i) {
        std::cout << std::hex << std::setfill('0') << std::setw(2) 
                    << static_cast<int>(data[i]) << " ";
        if ((i + 1) % 16 == 0) {
            std::cout << std::endl;
        }
    }
    std::cout << std::dec << std::endl << std::endl;
}

void WebSocket::read_messages() {
    try {
        while(true) {
            beast::flat_buffer buffer;
            ws_.read(buffer);

            
            // Check if this is a binary frame (SBE data) or text frame (JSON response)
            if (ws_.got_binary()) {
                std::cout << "Received SBE binary message (" << buffer.size() << " bytes)" << std::endl;
                auto const data = static_cast<const unsigned char*>(buffer.data().data());
                size_t size = buffer.size();
                print_binary_dump(data, size);
            } else {
                // Text frame - likely a subscription response or error
                std::cout << "Received JSON message: " << beast::make_printable(buffer.data()) << std::endl;
            }
        }
    } catch (beast::system_error const& se) {
        if (se.code() != websocket::error::closed) {
            std::cerr << "Error: " << se.code().message() << std::endl;
        }
    } catch (std::exception const& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

void WebSocket::close() {
    try {
        ws_.close(websocket::close_code::normal);
    } catch (std::exception const& e) {
        std::cerr << "Close error: " << e.what() << std::endl;
    }
}

bool WebSocket::read_binary(std::vector<uint8_t>& out_buffer)
{
    try {
        beast::flat_buffer buffer;
        ws_.read(buffer);
        
        if (ws_.got_binary()) {
            out_buffer.resize(buffer.size());
            std::memcpy(out_buffer.data(), buffer.data().data(), buffer.size());
            return true;
        } else {
            // Not a binary message
            return false;
        }
    } catch (std::exception const& e) {
        std::cerr << "Read binary error: " << e.what() << std::endl;
        return false;
    }
}