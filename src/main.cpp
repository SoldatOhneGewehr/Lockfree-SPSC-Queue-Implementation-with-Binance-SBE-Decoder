// main.cpp
#include "BinanceSBEWebSocket.hpp"
#include "LockFree.hpp"
#include <iostream>

int main() {
    try {
        // API Key from Binance (Ed25519 key)
        std::string api_key = "gnsAMZcoqIhGMB1VaczpqAzuL6LSbQrf33TehWnNFZLo29rLrNuJ8C1t7147q5dc";
        
        // Binance SBE WebSocket endpoint
        std::string host = "stream-sbe.binance.com";
        std::string port = "9443";
        std::string path = "/ws/btcusdt@bestBidAsk";

        WebSocket client(api_key);
        client.connect(host, port, path);
        LockFree lockFree(client);

        // Connect (API key is sent in X-MBX-APIKEY header during handshake)
        
        // Optional: Subscribe to additional streams (if using multi-stream endpoint)
        // std::string subscribe_msg = R"({"method":"SUBSCRIBE","params":["ethusdt@trade"],"id":1})";
        // client.subscribe(subscribe_msg);
        
        // Read incoming SBE binary messages
        client.read_messages();
        
    } catch (std::exception const& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}