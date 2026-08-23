// protocol.h
// iPhone USB Microphone - Windows
//
// Binary wire protocol definitions for audio data transmission.
// Must stay in sync with iOS Protocol.swift.

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <chrono>
#include <optional>

namespace iphone_mic {

// Protocol constants
constexpr uint32_t PROTOCOL_MAGIC   = 0x4D485049;  // "IPHM" little-endian
constexpr uint16_t PROTOCOL_VERSION = 1;
constexpr uint16_t DEFAULT_PORT     = 8730;

// Packet types
enum class PacketType : uint16_t {
    AudioData  = 0x01,
    Config     = 0x02,
    Heartbeat  = 0x03,
    ConfigAck  = 0x04,
};

// 24-byte packet header (all fields little-endian)
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;         // "IPHM" = 0x4D485049
    uint16_t version;       // Protocol version
    uint16_t type;          // PacketType
    uint32_t payload_size;  // Payload byte count
    uint32_t reserved;      // Reserved (0)
    uint64_t timestamp;     // Microsecond timestamp
    
    static constexpr size_t SIZE = 24;
    
    // Create a header for outgoing packets
    static PacketHeader create(PacketType ptype, uint32_t payload_size) {
        PacketHeader h{};
        h.magic = PROTOCOL_MAGIC;
        h.version = PROTOCOL_VERSION;
        h.type = static_cast<uint16_t>(ptype);
        h.payload_size = payload_size;
        h.reserved = 0;
        h.timestamp = current_timestamp_micros();
        return h;
    }
    
    // Validate header magic and version
    bool is_valid() const {
        return magic == PROTOCOL_MAGIC && version == PROTOCOL_VERSION;
    }
    
    PacketType packet_type() const {
        return static_cast<PacketType>(type);
    }
    
    static uint64_t current_timestamp_micros() {
        auto now = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch());
        return static_cast<uint64_t>(us.count());
    }
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == PacketHeader::SIZE, 
              "PacketHeader must be exactly 24 bytes");

// Audio configuration (matches JSON payload in config packets)
struct AudioConfig {
    int sample_rate  = 48000;
    int bit_depth    = 16;
    int channels     = 1;
    int buffer_size  = 256;
    
    // Bytes per sample (24-bit = 3 bytes)
    int bytes_per_sample() const { return bit_depth / 8; }
    
    // Bytes per frame (all channels)
    int bytes_per_frame() const { return bytes_per_sample() * channels; }
    
    // Bytes per buffer
    int bytes_per_buffer() const { return bytes_per_frame() * buffer_size; }
    
    // Buffer latency in milliseconds
    double buffer_latency_ms() const {
        return static_cast<double>(buffer_size) / 
               static_cast<double>(sample_rate) * 1000.0;
    }
    
    // Serialize to JSON string
    std::string to_json() const;
    
    // Deserialize from JSON string
    static std::optional<AudioConfig> from_json(const std::string& json);
};

// Packet parser - reads packets from a byte stream
class PacketParser {
public:
    // Feed raw bytes into the parser
    void feed(const uint8_t* data, size_t length);
    
    // Try to extract a complete packet
    // Returns true if a packet was extracted
    struct ParsedPacket {
        PacketHeader header;
        std::vector<uint8_t> payload;
    };
    
    bool try_parse(ParsedPacket& out);
    
    // Reset parser state
    void reset();
    
    // Bytes currently buffered
    size_t buffered_bytes() const { return buffer_.size(); }
    
private:
    std::vector<uint8_t> buffer_;
};

// Packet builder utilities
namespace packet {
    
    // Build a heartbeat packet (header only)
    inline std::vector<uint8_t> build_heartbeat() {
        PacketHeader h = PacketHeader::create(PacketType::Heartbeat, 0);
        std::vector<uint8_t> data(sizeof(h));
        std::memcpy(data.data(), &h, sizeof(h));
        return data;
    }
    
    // Build a config ACK packet
    inline std::vector<uint8_t> build_config_ack() {
        PacketHeader h = PacketHeader::create(PacketType::ConfigAck, 0);
        std::vector<uint8_t> data(sizeof(h));
        std::memcpy(data.data(), &h, sizeof(h));
        return data;
    }
    
} // namespace packet

} // namespace iphone_mic
