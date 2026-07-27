// protocol.cpp
// iPhone USB Microphone - Windows
//
// Protocol implementation: JSON config serialization and packet parsing.

#include "protocol.h"
#include <sstream>
#include <stdexcept>

namespace iphone_mic {

// ============================================================================
// AudioConfig JSON serialization
// ============================================================================

// Simple JSON serializer (no external dependency needed)
std::string AudioConfig::to_json() const {
    std::ostringstream ss;
    ss << "{"
       << "\"sampleRate\":" << sample_rate << ","
       << "\"bitDepth\":" << bit_depth << ","
       << "\"channels\":" << channels << ","
       << "\"bufferSize\":" << buffer_size
       << "}";
    return ss.str();
}

// Simple JSON parser for AudioConfig
// Handles the specific format: {"sampleRate":48000,"bitDepth":24,...}
static int parse_json_int(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        throw std::runtime_error("Key not found: " + key);
    }
    pos += search.length();
    
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }
    
    // Read integer
    std::string num;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        num += json[pos++];
    }
    
    if (num.empty()) {
        throw std::runtime_error("Invalid integer for key: " + key);
    }
    
    return std::stoi(num);
}

std::optional<AudioConfig> AudioConfig::from_json(const std::string& json) {
    try {
        AudioConfig config;
        config.sample_rate = parse_json_int(json, "sampleRate");
        config.bit_depth   = parse_json_int(json, "bitDepth");
        config.channels    = parse_json_int(json, "channels");
        config.buffer_size = parse_json_int(json, "bufferSize");
        return config;
    } catch (...) {
        return std::nullopt;
    }
}

// ============================================================================
// PacketParser
// ============================================================================

void PacketParser::feed(const uint8_t* data, size_t length) {
    buffer_.insert(buffer_.end(), data, data + length);
}

bool PacketParser::try_parse(ParsedPacket& out) {
    // Need at least a complete header
    if (buffer_.size() < PacketHeader::SIZE) {
        return false;
    }
    
    // Scan for valid magic (recover from corrupted streams)
    size_t offset = 0;
    while (offset + PacketHeader::SIZE <= buffer_.size()) {
        uint32_t magic;
        std::memcpy(&magic, buffer_.data() + offset, sizeof(magic));
        if (magic == PROTOCOL_MAGIC) {
            break;
        }
        offset++;
    }
    
    // Discard bytes before magic
    if (offset > 0) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + offset);
    }
    
    // Check if we have a full header after potential skip
    if (buffer_.size() < PacketHeader::SIZE) {
        return false;
    }
    
    // Parse header
    PacketHeader header;
    std::memcpy(&header, buffer_.data(), PacketHeader::SIZE);
    
    if (!header.is_valid()) {
        // Skip this byte and try again next time
        buffer_.erase(buffer_.begin());
        return false;
    }
    
    // Sanity check payload size (max 1MB to prevent memory issues)
    if (header.payload_size > 1024 * 1024) {
        buffer_.erase(buffer_.begin());
        return false;
    }
    
    // Check if we have the full packet
    size_t total_size = PacketHeader::SIZE + header.payload_size;
    if (buffer_.size() < total_size) {
        return false;  // Need more data
    }
    
    // Extract packet
    out.header = header;
    if (header.payload_size > 0) {
        out.payload.assign(
            buffer_.begin() + PacketHeader::SIZE,
            buffer_.begin() + total_size
        );
    } else {
        out.payload.clear();
    }
    
    // Remove consumed bytes
    buffer_.erase(buffer_.begin(), buffer_.begin() + total_size);
    
    return true;
}

void PacketParser::reset() {
    buffer_.clear();
}

} // namespace iphone_mic
