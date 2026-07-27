// ring_buffer_test.cpp
// Unit tests for the lock-free SPSC ring buffer.

#include "ring_buffer.h"

#include <iostream>
#include <cassert>
#include <thread>
#include <vector>
#include <cstring>
#include <numeric>
#include <chrono>

using namespace iphone_mic;

#define TEST(name) \
    void test_##name(); \
    struct test_reg_##name { test_reg_##name() { tests.push_back({#name, test_##name}); } }; \
    static test_reg_##name reg_##name; \
    void test_##name()

struct TestEntry {
    const char* name;
    void (*func)();
};
static std::vector<TestEntry> tests;

// ============================================================================
// Tests
// ============================================================================

TEST(basic_write_read) {
    RingBuffer rb(1024);
    
    uint8_t data[] = {1, 2, 3, 4, 5};
    assert(rb.write(data, 5) == 5);
    assert(rb.available_read() == 5);
    
    uint8_t out[5] = {};
    assert(rb.read(out, 5) == 5);
    assert(std::memcmp(data, out, 5) == 0);
    assert(rb.available_read() == 0);
}

TEST(empty_read) {
    RingBuffer rb(1024);
    uint8_t out[10];
    assert(rb.read(out, 10) == 0);
    assert(rb.empty());
}

TEST(full_write) {
    RingBuffer rb(64);  // Will be rounded to 64 (already power of 2)
    
    std::vector<uint8_t> data(64);
    std::iota(data.begin(), data.end(), 0);
    
    assert(rb.write(data.data(), 64) == 64);
    assert(rb.full());
    
    // Should not write more when full
    uint8_t extra = 0xFF;
    assert(rb.write(&extra, 1) == 0);
}

TEST(wraparound) {
    RingBuffer rb(64);
    
    // Write 48 bytes
    std::vector<uint8_t> data1(48, 0xAA);
    assert(rb.write(data1.data(), 48) == 48);
    
    // Read 32 bytes (advances read pointer)
    std::vector<uint8_t> out(32);
    assert(rb.read(out.data(), 32) == 32);
    
    // Write 48 more (wraps around)
    std::vector<uint8_t> data2(48, 0xBB);
    assert(rb.write(data2.data(), 48) == 48);
    
    // Read all remaining (16 from data1 + 48 from data2 = 64)
    std::vector<uint8_t> out2(64);
    size_t read = rb.read(out2.data(), 64);
    assert(read == 64);
    
    // First 16 should be 0xAA, next 48 should be 0xBB
    for (size_t i = 0; i < 16; i++) assert(out2[i] == 0xAA);
    for (size_t i = 16; i < 64; i++) assert(out2[i] == 0xBB);
}

TEST(peek) {
    RingBuffer rb(1024);
    
    uint8_t data[] = {10, 20, 30};
    rb.write(data, 3);
    
    uint8_t peek_out[3];
    assert(rb.peek(peek_out, 3) == 3);
    assert(rb.available_read() == 3);  // peek shouldn't consume
    
    uint8_t read_out[3];
    assert(rb.read(read_out, 3) == 3);
    assert(rb.available_read() == 0);
    assert(std::memcmp(peek_out, read_out, 3) == 0);
}

TEST(reset) {
    RingBuffer rb(1024);
    
    uint8_t data[] = {1, 2, 3};
    rb.write(data, 3);
    assert(rb.available_read() == 3);
    
    rb.reset();
    assert(rb.available_read() == 0);
    assert(rb.empty());
}

TEST(fill_ratio) {
    RingBuffer rb(1024);
    assert(rb.fill_ratio() == 0.0f);
    
    std::vector<uint8_t> data(512);
    rb.write(data.data(), 512);
    float ratio = rb.fill_ratio();
    assert(ratio > 0.49f && ratio < 0.51f);  // ~50%
}

TEST(concurrent_spsc) {
    // Stress test: one producer, one consumer, verify no corruption
    static constexpr size_t TOTAL_BYTES = 10 * 1024 * 1024;  // 10 MB
    static constexpr size_t CHUNK_SIZE = 768;  // Intentionally not power of 2
    
    RingBuffer rb(65536);
    std::atomic<bool> done{false};
    size_t total_read = 0;
    bool corruption_detected = false;
    
    // Producer thread
    std::thread producer([&]() {
        size_t written = 0;
        uint8_t counter = 0;
        std::vector<uint8_t> chunk(CHUNK_SIZE);
        
        while (written < TOTAL_BYTES) {
            // Fill chunk with sequential pattern
            for (size_t i = 0; i < CHUNK_SIZE; i++) {
                chunk[i] = counter++;
            }
            
            size_t w = rb.write(chunk.data(), CHUNK_SIZE);
            written += w;
            
            if (w < CHUNK_SIZE) {
                // Buffer full, yield and retry
                std::this_thread::yield();
            }
        }
        done.store(true);
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        uint8_t expected = 0;
        std::vector<uint8_t> chunk(CHUNK_SIZE);
        
        while (!done.load() || !rb.empty()) {
            size_t r = rb.read(chunk.data(), CHUNK_SIZE);
            
            if (r > 0) {
                // Verify sequential pattern
                for (size_t i = 0; i < r; i++) {
                    if (chunk[i] != expected) {
                        corruption_detected = true;
                        return;
                    }
                    expected++;
                }
                total_read += r;
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    assert(!corruption_detected);
    assert(total_read == TOTAL_BYTES);
    
    std::cout << "  SPSC stress test: " << (TOTAL_BYTES / 1024 / 1024) 
              << " MB transferred without corruption" << std::endl;
}

TEST(audio_buffer_sizes) {
    // Test with typical audio buffer sizes
    for (int buf_size : {64, 128, 256, 512}) {
        // 24-bit stereo: 3 bytes * 2 channels * buffer_size
        size_t frame_bytes = 3 * 2 * buf_size;
        
        RingBuffer rb(frame_bytes * 16);  // Hold 16 buffers
        
        std::vector<uint8_t> write_buf(frame_bytes, 0x42);
        std::vector<uint8_t> read_buf(frame_bytes);
        
        // Write 4 buffers
        for (int i = 0; i < 4; i++) {
            assert(rb.write(write_buf.data(), frame_bytes) == frame_bytes);
        }
        
        // Read 4 buffers
        for (int i = 0; i < 4; i++) {
            assert(rb.read(read_buf.data(), frame_bytes) == frame_bytes);
            assert(read_buf[0] == 0x42);
        }
        
        std::cout << "  Buffer size " << buf_size << " samples: OK" << std::endl;
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "Ring Buffer Tests" << std::endl;
    std::cout << "=================" << std::endl;
    
    int passed = 0, failed = 0;
    
    for (auto& test : tests) {
        std::cout << "Running: " << test.name << "..." << std::endl;
        try {
            test.func();
            std::cout << "  PASSED" << std::endl;
            passed++;
        } catch (const std::exception& e) {
            std::cout << "  FAILED: " << e.what() << std::endl;
            failed++;
        } catch (...) {
            std::cout << "  FAILED: unknown exception" << std::endl;
            failed++;
        }
    }
    
    std::cout << "\n=================" << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    
    return failed > 0 ? 1 : 0;
}
