// ring_buffer.h
// iPhone USB Microphone - Windows
//
// Lock-free Single-Producer Single-Consumer (SPSC) ring buffer.
// Producer: TCP receive thread writing PCM data
// Consumer: ASIO callback thread reading PCM data

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cassert>

namespace iphone_mic {

/// Lock-free SPSC ring buffer for real-time audio.
/// 
/// Thread safety guarantee:
///   - Exactly ONE thread may call write()
///   - Exactly ONE thread may call read()
///   - No locks are used; relies on atomic operations with appropriate
///     memory ordering for safe cross-thread communication.
///
/// The buffer stores raw bytes. For audio, this is typically 24-bit PCM
/// samples. The consumer (ASIO) reads in multiples of the audio frame size.
class RingBuffer {
public:
    /// @param capacity  Buffer capacity in bytes. Will be rounded up to
    ///                  the next power of 2 for efficient masking.
    explicit RingBuffer(size_t capacity) {
        // Round up to next power of 2
        capacity_ = next_power_of_2(capacity);
        buffer_ = new uint8_t[capacity_];
        std::memset(buffer_, 0, capacity_);
        write_pos_.store(0, std::memory_order_relaxed);
        read_pos_.store(0, std::memory_order_relaxed);
    }
    
    ~RingBuffer() {
        delete[] buffer_;
    }
    
    // Non-copyable, non-movable
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;
    
    /// Write data into the ring buffer (producer side).
    /// @param data   Pointer to data to write
    /// @param length Number of bytes to write
    /// @return Number of bytes actually written (may be less if buffer full)
    size_t write(const uint8_t* data, size_t length) {
        const size_t w = write_pos_.load(std::memory_order_relaxed);
        const size_t r = read_pos_.load(std::memory_order_acquire);
        
        const size_t available = capacity_ - (w - r);
        const size_t to_write = std::min(length, available);
        
        if (to_write == 0) return 0;
        
        const size_t w_masked = w & (capacity_ - 1);
        const size_t first_chunk = std::min(to_write, capacity_ - w_masked);
        const size_t second_chunk = to_write - first_chunk;
        
        std::memcpy(buffer_ + w_masked, data, first_chunk);
        if (second_chunk > 0) {
            std::memcpy(buffer_, data + first_chunk, second_chunk);
        }
        
        write_pos_.store(w + to_write, std::memory_order_release);
        return to_write;
    }
    
    /// Read data from the ring buffer (consumer side).
    /// @param dest   Destination buffer
    /// @param length Number of bytes to read
    /// @return Number of bytes actually read (may be less if not enough data)
    size_t read(uint8_t* dest, size_t length) {
        const size_t r = read_pos_.load(std::memory_order_relaxed);
        const size_t w = write_pos_.load(std::memory_order_acquire);
        
        const size_t available = w - r;
        const size_t to_read = std::min(length, available);
        
        if (to_read == 0) return 0;
        
        const size_t r_masked = r & (capacity_ - 1);
        const size_t first_chunk = std::min(to_read, capacity_ - r_masked);
        const size_t second_chunk = to_read - first_chunk;
        
        std::memcpy(dest, buffer_ + r_masked, first_chunk);
        if (second_chunk > 0) {
            std::memcpy(dest + first_chunk, buffer_, second_chunk);
        }
        
        read_pos_.store(r + to_read, std::memory_order_release);
        return to_read;
    }
    
    /// Read data without advancing the read position (peek).
    size_t peek(uint8_t* dest, size_t length) const {
        const size_t r = read_pos_.load(std::memory_order_relaxed);
        const size_t w = write_pos_.load(std::memory_order_acquire);
        
        const size_t available = w - r;
        const size_t to_read = std::min(length, available);
        
        if (to_read == 0) return 0;
        
        const size_t r_masked = r & (capacity_ - 1);
        const size_t first_chunk = std::min(to_read, capacity_ - r_masked);
        const size_t second_chunk = to_read - first_chunk;
        
        std::memcpy(dest, buffer_ + r_masked, first_chunk);
        if (second_chunk > 0) {
            std::memcpy(dest + first_chunk, buffer_, second_chunk);
        }
        
        return to_read;
    }
    
    /// Number of bytes available for reading
    size_t available_read() const {
        const size_t w = write_pos_.load(std::memory_order_acquire);
        const size_t r = read_pos_.load(std::memory_order_relaxed);
        return w - r;
    }
    
    /// Number of bytes available for writing
    size_t available_write() const {
        const size_t w = write_pos_.load(std::memory_order_relaxed);
        const size_t r = read_pos_.load(std::memory_order_acquire);
        return capacity_ - (w - r);
    }
    
    /// Total capacity in bytes
    size_t capacity() const { return capacity_; }
    
    /// Reset the buffer (NOT thread-safe - call only when no reads/writes)
    void reset() {
        write_pos_.store(0, std::memory_order_relaxed);
        read_pos_.store(0, std::memory_order_relaxed);
        std::memset(buffer_, 0, capacity_);
    }
    
    /// Check if buffer is empty
    bool empty() const { return available_read() == 0; }
    
    /// Check if buffer is full
    bool full() const { return available_write() == 0; }

    /// Get fill ratio (0.0 = empty, 1.0 = full)
    float fill_ratio() const {
        return static_cast<float>(available_read()) / 
               static_cast<float>(capacity_);
    }

private:
    static size_t next_power_of_2(size_t v) {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        v++;
        return v;
    }
    
    uint8_t* buffer_ = nullptr;
    size_t capacity_ = 0;
    
    // Cache-line aligned to prevent false sharing
    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};
};

} // namespace iphone_mic
