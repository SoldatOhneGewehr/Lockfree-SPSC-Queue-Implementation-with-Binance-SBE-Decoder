#include "WebSocket.hpp"

#include <boost/lockfree/queue.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <immintrin.h> // _mm_pause
#include <pthread.h>

// -------------------- Pin thread helper --------------------
void pinThread(int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "pthread_setaffinity_np failed: " << rc << std::endl;
    }
}

// -------------------- Buffer pool --------------------
constexpr size_t BUFFER_SIZE = 4096;
struct alignas(64) RxBuffer {
    uint32_t size;
    uint8_t data[BUFFER_SIZE];
};

// Use pointer type for boost::lockfree::queue (trivially copyable)
using BufferPtr = RxBuffer*;

// -------------------- Queues --------------------
constexpr size_t POOL_SIZE = 4096;
constexpr size_t WORK_Q_CAP = 8192;
boost::lockfree::queue<BufferPtr> freeQueue(POOL_SIZE);
boost::lockfree::queue<BufferPtr> workQueue(WORK_Q_CAP);

// -------------------- SBE parsing utilities (manual minimal) --------------------
// Read little-endian integer readers

static inline uint16_t read_u16_le(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
static inline uint32_t read_u32_le(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
}
static inline int8_t read_i8(const uint8_t* p) { return int8_t(p[0]); }
static inline int64_t read_i64_le(const uint8_t* p) {
    uint64_t v = 0;
    for (int i=0;i<8;++i) v |= (uint64_t(p[i]) << (8*i));
    return int64_t(v);
}

// messageHeader: uint16 blockLength; uint16 templateId; uint16 schemaId; uint16 version;
struct SbeHeader { uint16_t blockLength; uint16_t templateId; uint16_t schemaId; uint16_t version; };

// Parse message header from buffer at offset 0 (must be at least 8 bytes)
bool parse_message_header(const uint8_t* buf, size_t bufSize, SbeHeader& out) {
    if (bufSize < 8) return false;
    out.blockLength = read_u16_le(buf + 0);
    out.templateId  = read_u16_le(buf + 2);
    out.schemaId    = read_u16_le(buf + 4);
    out.version     = read_u16_le(buf + 6);
    return true;
}

// Decode BestBidAsk (templateId 10001) — manual mapping taken from schema walkthrough
void handleBestBidAsk(const uint8_t* fixedBlock, uint16_t blockLength, const uint8_t* varRegion, size_t varRegionSize) {
    // fixedBlock must be at least 50 based on schema; but trust blockLength
    if (blockLength < 50) {
        // defensive
    }
    int64_t eventTime = read_i64_le(fixedBlock + 0);
    int64_t bookUpdateId = read_i64_le(fixedBlock + 8);
    int8_t priceExp = read_i8(fixedBlock + 16);
    int8_t qtyExp   = read_i8(fixedBlock + 17);
    int64_t bidPriceMant = read_i64_le(fixedBlock + 18);
    int64_t bidQtyMant   = read_i64_le(fixedBlock + 26);
    int64_t askPriceMant = read_i64_le(fixedBlock + 34);
    int64_t askQtyMant   = read_i64_le(fixedBlock + 42);

    double bidPrice = double(bidPriceMant) * pow(10.0, double(priceExp));
    double bidQty   = double(bidQtyMant) * pow(10.0, double(qtyExp));
    double askPrice = double(askPriceMant) * pow(10.0, double(priceExp));
    double askQty   = double(askQtyMant) * pow(10.0, double(qtyExp));

    // symbol is varString8: [uint8 length][bytes]
    std::string symbol;
    if (varRegionSize >= 1) {
        uint8_t symLen = varRegion[0];
        if (varRegionSize >= 1 + symLen) {
            symbol.assign(reinterpret_cast<const char*>(varRegion + 1), symLen);
        }
    }

    // For demo, print first char of symbol to avoid too much IO
    std::cout << "[BestBidAsk] sym=" << symbol
              << " bid=" << bidPrice << "@" << bidQty
              << " ask=" << askPrice << "@" << askQty
              << " eventTime=" << eventTime << "\n";
}

// Decode TradesStreamEvent (templateId 10000) minimal parsing
void handleTrades(const uint8_t* fixedBlock, uint16_t blockLength, const uint8_t* varRegion, size_t varRegionSize) {
    // fixedBlock: eventTime (8), transactTime (8), priceExponent (1), qtyExponent (1)
    if (blockLength < 18) return;
    int64_t eventTime = read_i64_le(fixedBlock + 0);
    int64_t transactTime = read_i64_le(fixedBlock + 8);
    int8_t priceExp = read_i8(fixedBlock + 16);
    int8_t qtyExp   = read_i8(fixedBlock + 17);

    // varRegion begins with the trades group header: groupSizeEncoding {uint16 blockLength; uint32 numInGroup}
    // guard bounds
    if (varRegionSize < 6) {
        // no group
        return;
    }
    uint16_t groupBlockLength = read_u16_le(varRegion + 0);
    uint32_t numInGroup = read_u32_le(varRegion + 2);
    const uint8_t* p = varRegion + 6;
    size_t remain = varRegionSize - 6;

    for (uint32_t i = 0; i < numInGroup; ++i) {
        if (remain < groupBlockLength) break;
        // Trades group fields per schema: id(int64), price(mantissa64), qty(mantissa64), isBuyerMaker(uint8), isBestMatch constant
        if (groupBlockLength < (8+8+8+1)) break;
        int64_t id = read_i64_le(p + 0);
        int64_t priceMant = read_i64_le(p + 8);
        int64_t qtyMant   = read_i64_le(p + 16);
        uint8_t isBuyerMaker = p[24];
        double price = double(priceMant) * pow(10.0, double(priceExp));
        double qty   = double(qtyMant)   * pow(10.0, double(qtyExp));
        std::cout << "[Trade] id=" << id << " price=" << price << " qty=" << qty
                  << " buyerMaker=" << (int)isBuyerMaker << "\n";

        p += groupBlockLength;
        remain -= groupBlockLength;
    }

    // After group, symbol varString may follow (we skip here)
}

// High-level dispatcher: buffer contains one or multiple SBE messages concatenated
void processBuffer(BufferPtr buf) {
    size_t offset = 0;
    const uint8_t* base = buf->data;
    size_t bufsz = buf->size;

    while (offset + 8 <= bufsz) {
        SbeHeader hdr;
        if (!parse_message_header(base + offset, bufsz - offset, hdr)) break;
        // fixed block starts at offset + 8 and length hdr.blockLength
        size_t fixedStart = offset + 8;
        if (fixedStart + hdr.blockLength > bufsz) break; // incomplete
        const uint8_t* fixedBlock = base + fixedStart;

        // var region follows fixed block. Since we don't know exact encoded length of message,
        // we parse group(s) and varString conservatively: we pass remaining bytes to handlers,
        // handlers will bound-check
        size_t varRegionStart = fixedStart + hdr.blockLength;
        size_t varRegionSize = (varRegionStart <= bufsz) ? (bufsz - varRegionStart) : 0;

        switch (hdr.templateId) {
            case 10001: // BestBidAsk
                handleBestBidAsk(fixedBlock, hdr.blockLength, base + varRegionStart, varRegionSize);
                break;
            case 10000: // Trades
                handleTrades(fixedBlock, hdr.blockLength, base + varRegionStart, varRegionSize);
                break;
            default:
                // Unknown template - skip fixed block only.
                std::cout << "[Unknown template] id=" << hdr.templateId << " blockLen=" << hdr.blockLength << "\n";
                break;
        }

        // Advance offset by header + fixed block + ??? -> best-effort: try to advance past the fixed block and a possible varString
        // We attempt to consume a varString8 if present (common last field), otherwise we break to avoid infinite loop.
        offset = varRegionStart;

        if (varRegionSize >= 1) {
            uint8_t len = base[varRegionStart]; // first byte maybe a varString length
            // If there's enough bytes for len, advance past it; otherwise break
            if (varRegionStart + 1 + len <= bufsz) {
                offset = varRegionStart + 1 + len;
                // continue to next message
                continue;
            } else {
                // not enough bytes for var string, break
                break;
            }
        } else {
            // no var region -> we consumed header+fixed only
            break;
        }
    }
}

// -------------------- Producer thread --------------------
void producerThread(WebSocketClient& client, std::atomic<bool>& running) {
    pinThread(0);
    std::vector<uint8_t> tmp;
    while (running.load(std::memory_order_acquire)) {
        bool ok = client.readBinary(tmp);
        if (!ok) {
            running.store(false);
            break;
        }

        // Obtain buffer from pool
        BufferPtr bptr = nullptr;
        if (!freeQueue.pop(bptr)) {
            // Pool exhausted: try to allocate (rare); prefer to drop frame instead to avoid unbounded growth
            bptr = new RxBuffer();
        }

        if (tmp.size() > BUFFER_SIZE) {
            std::cerr << "Received frame too large: " << tmp.size() << " > " << BUFFER_SIZE << "\n";
            // recycle or free
            if (freeQueue.push(bptr)) {}
            else delete bptr;
            continue;
        }

        bptr->size = (uint32_t)tmp.size();
        std::memcpy(bptr->data, tmp.data(), tmp.size());

        while (!workQueue.push(bptr)) _mm_pause();
    }
}

// -------------------- Consumer thread --------------------
void consumerThread(std::atomic<bool>& running) {
    pinThread(1);
    size_t count = 0;
    while (running.load(std::memory_order_acquire) || !workQueue.empty()) {
        BufferPtr bptr = nullptr;
        if (workQueue.pop(bptr)) {
            processBuffer(bptr);
            // Return buffer to free pool
            if (!freeQueue.push(bptr)) {
                // if pool full, free
                delete bptr;
            }
            ++count;
        } else {
            _mm_pause();
        }
    }
    std::cout << "Consumer exiting. processed=" << count << "\n";
}

// -------------------- Main --------------------
int main(int argc, char** argv) {
    // Replace target & host with Binance SBE endpoint and path.
    std::string host = "stream.binance.com";
    std::string port = "9443";
    std::string target = "/ws/sbe?streams=..."; // <<< replace with correct path
    std::optional<std::string> subprotocol = std::nullopt; // set if Binance requires an SBE subprotocol

    // Preallocate pool buffers
    std::vector<RxBuffer*> pool;
    pool.reserve(POOL_SIZE);
    for (size_t i = 0; i < POOL_SIZE; ++i) {
        RxBuffer* p = new RxBuffer();
        p->size = 0;
        pool.push_back(p);
        // push to freeQueue; if fails (shouldn't), delete
        if (!freeQueue.push(p)) {
            delete p;
        }
    }

    WebSocketClient client(host, port, subprotocol);
    try {
        client.connect(target);
    } catch (const std::exception& ex) {
        std::cerr << "WebSocket connect failed: " << ex.what() << "\n";
        return 1;
    }

    std::atomic<bool> running(true);
    std::thread prod([&] { producerThread(client, running); });
    std::thread cons([&] { consumerThread(running); });

    std::cout << "Running. Press Enter to stop...\n";
    std::string dummy; std::getline(std::cin, dummy);

    running.store(false);
    client.close();

    prod.join();
    cons.join();

    // Cleanup pool (some buffers may still be allocated)
    BufferPtr ptr;
    while (freeQueue.pop(ptr)) delete ptr;
    while (workQueue.pop(ptr)) delete ptr;

    std::cout << "Shutdown complete.\n";
    return 0;
}