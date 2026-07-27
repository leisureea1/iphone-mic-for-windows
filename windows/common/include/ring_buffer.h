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
template <typename T>
class RingBuffer {
public:
    /// @param capacity  Buffer capacity in elements (e.g. frames). Will be rounded up to
    ///                  the next power of 2 for efficient masking.
    explicit RingBuffer(size_t capacity) {
        // Round up to next power of 2
        capacity_ = next_power_of_2(capacity);
        buffer_ = new T[capacity_];
        std::memset(buffer_, 0, capacity_ * sizeof(T));
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
    /// @param data   Pointer to elements to write
    /// @param count  Number of elements to write
    /// @return Number of elements actually written (may be less if buffer full)
    size_t write(const T* data, size_t count) {
        const size_t w = write_pos_.load(std::memory_order_relaxed);
        const size_t r = read_pos_.load(std::memory_order_acquire);
        
        const size_t available = capacity_ - (w - r);
        const size_t to_write = std::min(count, available);
        
        if (to_write == 0) return 0;
        
        const size_t w_masked = w & (capacity_ - 1);
        const size_t first_chunk = std::min(to_write, capacity_ - w_masked);
        const size_t second_chunk = to_write - first_chunk;
        
        std::memcpy(buffer_ + w_masked, data, first_chunk * sizeof(T));
        if (second_chunk > 0) {
            std::memcpy(buffer_, data + first_chunk, second_chunk * sizeof(T));
        }
        
        write_pos_.store(w + to_write, std::memory_order_release);
        return to_write;
    }
    
    /// Read data from the ring buffer (consumer side).
    /// @param dest   Destination buffer
    /// @param length Number of bytes to read
    /// @return Number of bytes actually read (may be less if not enough data)
    size_t read(T* dest, size_t count) {
        const size_t r = read_pos_.load(std::memory_order_relaxed);
        const size_t w = write_pos_.load(std::memory_order_acquire);
        
        const size_t available = w - r;
        const size_t to_read = std::min(count, available);
        
        if (to_read == 0) return 0;
        
        const size_t r_masked = r & (capacity_ - 1);
        const size_t first_chunk = std::min(to_read, capacity_ - r_masked);
        const size_t second_chunk = to_read - first_chunk;
        
        std::memcpy(dest, buffer_ + r_masked, first_chunk * sizeof(T));
        if (second_chunk > 0) {
            std::memcpy(dest + first_chunk, buffer_, second_chunk * sizeof(T));
        }
        
        read_pos_.store(r + to_read, std::memory_order_release);
        return to_read;
    }
    
    /// Read data without advancing the read position (peek).
    size_t peek(T* dest, size_t count) const {
        const size_t r = read_pos_.load(std::memory_order_relaxed);
        const size_t w = write_pos_.load(std::memory_order_acquire);
        
        const size_t available = w - r;
        const size_t to_read = std::min(count, available);
        
        if (to_read == 0) return 0;
        
        const size_t r_masked = r & (capacity_ - 1);
        const size_t first_chunk = std::min(to_read, capacity_ - r_masked);
        const size_t second_chunk = to_read - first_chunk;
        
        std::memcpy(dest, buffer_ + r_masked, first_chunk * sizeof(T));
        if (second_chunk > 0) {
            std::memcpy(dest + first_chunk, buffer_, second_chunk * sizeof(T));
        }
        
        return to_read;
    }
    
    /// Number of elements available for reading
    size_t available_read() const {
        const size_t w = write_pos_.load(std::memory_order_acquire);
        const size_t r = read_pos_.load(std::memory_order_relaxed);
        return w - r;
    }
    
    /// Number of elements available for writing
    size_t available_write() const {
        const size_t w = write_pos_.load(std::memory_order_relaxed);
        const size_t r = read_pos_.load(std::memory_order_acquire);
        return capacity_ - (w - r);
    }
    
    /// Total capacity in elements
    size_t capacity() const { return capacity_; }
    
    /// Reset the buffer (NOT thread-safe - call only when no reads/writes)
    void reset() {
        write_pos_.store(0, std::memory_order_relaxed);
        read_pos_.store(0, std::memory_order_relaxed);
        std::memset(buffer_, 0, capacity_ * sizeof(T));
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
    
    T* buffer_ = nullptr;
    size_t capacity_ = 0;
    
    // Cache-line aligned to prevent false sharing
    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};
};

} // namespace iphone_mic
