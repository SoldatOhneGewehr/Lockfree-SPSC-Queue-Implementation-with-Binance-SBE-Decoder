#ifndef LOCKFREE_HPP
#define LOCKFREE_HPP

#include "BinanceSBEWebSocket.hpp"
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

constexpr size_t BUFFER_SIZE = 4096;
struct alignas(64) RxBuffer
{
    uint32_t size;
    uint8_t data[BUFFER_SIZE];
};

constexpr size_t POOL_SIZE = 4096;
constexpr size_t WORK_Q_CAP = 8192;
using BufferPtr = RxBuffer *;
struct SbeHeader
{
    uint16_t blockLength;
    uint16_t templateId;
    uint16_t schemaId;
    uint16_t version;
};

class LockFree
{
public:

    LockFree(WebSocket& client) 
    : client_(client)
    {

        pool_.reserve(POOL_SIZE);
        for (size_t i = 0; i < POOL_SIZE; ++i)
        {
            RxBuffer* buf = new RxBuffer();
            buf->size = 0;
            pool_.push_back(buf);
            if (!freeQueue_.push(buf)) {
                delete buf;
            }
        }

        std::thread prod([&] { producerThread(client, running); });
        std::thread cons([&] { consumerThread(running); });
        prod.join();
        cons.join();
    }

    void producerThread(WebSocket& client, std::atomic<bool> & running);
    void consumerThread(std::atomic<bool> & running);

private:

    void pinThread(int cpu_id);

    bool parse_message_header(const uint8_t *buf, size_t bufSize, SbeHeader &out);
    void handleBestBidAsk(const uint8_t *fixedBlock, uint16_t blockLength, const uint8_t *varRegion, size_t varRegionSize);
    void handleTrades(const uint8_t *fixedBlock, uint16_t blockLength, const uint8_t *varRegion, size_t varRegionSize);
    void processBuffer(BufferPtr buf);
    
    static inline uint16_t read_u16_le(const uint8_t *p)
    {
        return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
    }
    static inline uint32_t read_u32_le(const uint8_t *p)
    {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }
    static inline int8_t read_i8(const uint8_t *p) { return int8_t(p[0]); }
    
    static inline int64_t read_i64_le(const uint8_t *p)
    {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
        v |= (uint64_t(p[i]) << (8 * i));
        return int64_t(v);
        
    }

    WebSocket &client_;
    std::atomic<bool> running{true};
    std::vector<RxBuffer*> pool_;
    boost::lockfree::queue<BufferPtr> freeQueue_{POOL_SIZE};
    boost::lockfree::queue<BufferPtr> workQueue_{WORK_Q_CAP};
};  

#endif // LOCKFREE_HPP