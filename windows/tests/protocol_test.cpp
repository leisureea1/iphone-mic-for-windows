// protocol_test.cpp
// Unit tests for the protocol parser.

#include "protocol.h"

#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>

using namespace iphone_mic;

void test_header_serialization() {
    std::cout << "test_header_serialization... ";
    
    PacketHeader h = PacketHeader::create(PacketType::AudioData, 1024);
    
    assert(h.magic == PROTOCOL_MAGIC);
    assert(h.version == PROTOCOL_VERSION);
    assert(h.packet_type() == PacketType::AudioData);
    assert(h.payload_size == 1024);
    assert(h.is_valid());
    
    std::cout << "PASSED" << std::endl;
}

void test_header_size() {
    std::cout << "test_header_size... ";
    assert(sizeof(PacketHeader) == 24);
    assert(PacketHeader::SIZE == 24);
    std::cout << "PASSED" << std::endl;
}

void test_config_json() {
    std::cout << "test_config_json... ";
    
    AudioConfig config;
    config.sample_rate = 48000;
    config.bit_depth = 24;
    config.channels = 1;
    config.buffer_size = 256;
    
    std::string json = config.to_json();
    auto parsed = AudioConfig::from_json(json);
    
    assert(parsed.has_value());
    assert(parsed->sample_rate == 48000);
    assert(parsed->bit_depth == 24);
    assert(parsed->channels == 1);
    assert(parsed->buffer_size == 256);
    
    std::cout << "PASSED (JSON: " << json << ")" << std::endl;
}

void test_config_calculations() {
    std::cout << "test_config_calculations... ";
    
    AudioConfig config;
    config.sample_rate = 48000;
    config.bit_depth = 24;
    config.channels = 1;
    config.buffer_size = 256;
    
    assert(config.bytes_per_sample() == 3);
    assert(config.bytes_per_frame() == 3);
    assert(config.bytes_per_buffer() == 768);
    
    double latency = config.buffer_latency_ms();
    assert(latency > 5.3 && latency < 5.4);  // ~5.333ms
    
    config.channels = 2;
    assert(config.bytes_per_frame() == 6);
    assert(config.bytes_per_buffer() == 1536);
    
    std::cout << "PASSED" << std::endl;
}

void test_parser_basic() {
    std::cout << "test_parser_basic... ";
    
    // Build a heartbeat packet
    auto heartbeat = packet::build_heartbeat();
    
    PacketParser parser;
    parser.feed(heartbeat.data(), heartbeat.size());
    
    PacketParser::ParsedPacket pkt;
    assert(parser.try_parse(pkt));
    assert(pkt.header.is_valid());
    assert(pkt.header.packet_type() == PacketType::Heartbeat);
    assert(pkt.header.payload_size == 0);
    assert(pkt.payload.empty());
    
    std::cout << "PASSED" << std::endl;
}

void test_parser_with_payload() {
    std::cout << "test_parser_with_payload... ";
    
    // Simulate audio data packet
    std::vector<uint8_t> pcm_data(768);  // 256 samples * 3 bytes
    for (size_t i = 0; i < pcm_data.size(); i++) {
        pcm_data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    
    // Build packet manually
    PacketHeader header = PacketHeader::create(PacketType::AudioData, 
                                                static_cast<uint32_t>(pcm_data.size()));
    
    std::vector<uint8_t> packet(sizeof(header) + pcm_data.size());
    std::memcpy(packet.data(), &header, sizeof(header));
    std::memcpy(packet.data() + sizeof(header), pcm_data.data(), pcm_data.size());
    
    PacketParser parser;
    parser.feed(packet.data(), packet.size());
    
    PacketParser::ParsedPacket pkt;
    assert(parser.try_parse(pkt));
    assert(pkt.header.packet_type() == PacketType::AudioData);
    assert(pkt.payload.size() == 768);
    assert(pkt.payload[0] == 0);
    assert(pkt.payload[255] == 255);
    
    std::cout << "PASSED" << std::endl;
}

void test_parser_fragmented() {
    std::cout << "test_parser_fragmented... ";
    
    // Build a config ACK packet
    auto ack = packet::build_config_ack();
    
    PacketParser parser;
    
    // Feed in small chunks (simulates TCP fragmentation)
    for (size_t i = 0; i < ack.size(); i += 4) {
        size_t chunk = std::min<size_t>(4, ack.size() - i);
        parser.feed(ack.data() + i, chunk);
    }
    
    PacketParser::ParsedPacket pkt;
    assert(parser.try_parse(pkt));
    assert(pkt.header.packet_type() == PacketType::ConfigAck);
    
    std::cout << "PASSED" << std::endl;
}

void test_parser_multiple_packets() {
    std::cout << "test_parser_multiple_packets... ";
    
    auto hb1 = packet::build_heartbeat();
    auto hb2 = packet::build_heartbeat();
    auto ack = packet::build_config_ack();
    
    // Concatenate all packets
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), hb1.begin(), hb1.end());
    combined.insert(combined.end(), hb2.begin(), hb2.end());
    combined.insert(combined.end(), ack.begin(), ack.end());
    
    PacketParser parser;
    parser.feed(combined.data(), combined.size());
    
    PacketParser::ParsedPacket pkt;
    assert(parser.try_parse(pkt));
    assert(pkt.header.packet_type() == PacketType::Heartbeat);
    
    assert(parser.try_parse(pkt));
    assert(pkt.header.packet_type() == PacketType::Heartbeat);
    
    assert(parser.try_parse(pkt));
    assert(pkt.header.packet_type() == PacketType::ConfigAck);
    
    // No more packets
    assert(!parser.try_parse(pkt));
    
    std::cout << "PASSED" << std::endl;
}

void test_parser_garbage_recovery() {
    std::cout << "test_parser_garbage_recovery... ";
    
    // Prepend garbage bytes before a valid packet
    auto hb = packet::build_heartbeat();
    
    std::vector<uint8_t> data;
    data.push_back(0xFF);
    data.push_back(0xFE);
    data.push_back(0xFD);
    data.insert(data.end(), hb.begin(), hb.end());
    
    PacketParser parser;
    parser.feed(data.data(), data.size());
    
    PacketParser::ParsedPacket pkt;
    assert(parser.try_parse(pkt));
    assert(pkt.header.packet_type() == PacketType::Heartbeat);
    
    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "Protocol Tests" << std::endl;
    std::cout << "==============" << std::endl;
    
    test_header_serialization();
    test_header_size();
    test_config_json();
    test_config_calculations();
    test_parser_basic();
    test_parser_with_payload();
    test_parser_fragmented();
    test_parser_multiple_packets();
    test_parser_garbage_recovery();
    
    std::cout << "\nAll protocol tests PASSED!" << std::endl;
    return 0;
}
