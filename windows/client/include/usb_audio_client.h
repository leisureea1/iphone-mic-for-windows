// usb_audio_client.h
// iPhone USB Microphone - Windows
//
// TCP client that connects to iPhone via usbmuxd/iproxy tunnel.
// Receives audio data and writes to a shared ring buffer.

#pragma once

#include "protocol.h"
#include "ring_buffer.h"
#include "audio_format.h"

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

// Forward declare Winsock types to avoid header pollution
struct sockaddr_in;

namespace iphone_mic {

/// Statistics reported by the client
struct ClientStats {
    uint64_t bytes_received = 0;
    uint64_t packets_received = 0;
    uint64_t packets_dropped = 0;
    uint64_t reconnect_count = 0;
    double   latency_ms = 0.0;
    bool     connected = false;
    float    buffer_fill_ratio = 0.0f;
};

/// Callback for connection state changes
using ConnectionCallback = std::function<void(bool connected)>;

/// Callback for config received from iOS
using ConfigCallback = std::function<void(const AudioConfig& config)>;

/// USB Audio Client - receives PCM audio from iPhone over TCP
class UsbAudioClient {
public:
    /// @param host  Host to connect to (default: "127.0.0.1" for iproxy)
    /// @param port  Port to connect to (default: 8730)
    /// @param ring_buffer  Shared ring buffer for audio data output
    UsbAudioClient(const std::string& host, uint16_t port,
                   std::shared_ptr<RingBuffer> ring_buffer);
    
    ~UsbAudioClient();
    
    // Non-copyable
    UsbAudioClient(const UsbAudioClient&) = delete;
    UsbAudioClient& operator=(const UsbAudioClient&) = delete;
    
    /// Start the client (launches receive thread)
    bool start();
    
    /// Stop the client
    void stop();
    
    /// Check if client is running
    bool is_running() const { return running_.load(); }
    
    /// Check if connected
    bool is_connected() const { return connected_.load(); }
    
    /// Get current statistics
    ClientStats get_stats() const;
    
    /// Get current audio config (received from iOS)
    AudioConfig get_audio_config() const;
    
    /// Set connection state callback
    void set_connection_callback(ConnectionCallback cb);
    
    /// Set config received callback
    void set_config_callback(ConfigCallback cb);
    
    /// Send a config request to the iOS device
    void send_config_request(const AudioConfig& config);
    
private:
    // Connection management
    bool connect_to_server();
    void disconnect();
    
    // Receive thread
    void receive_thread_func();
    
    // Packet handling
    void handle_packet(const PacketParser::ParsedPacket& packet);
    void handle_audio_data(const std::vector<uint8_t>& payload);
    void handle_config(const std::vector<uint8_t>& payload);
    void handle_heartbeat();
    
    // Send data
    bool send_data(const uint8_t* data, size_t length);
    
    // Configuration
    std::string host_;
    uint16_t port_;
    
    // Ring buffer (shared with ASIO driver)
    std::shared_ptr<RingBuffer> ring_buffer_;
    
    // Network
    uintptr_t socket_ = ~0ULL;  // INVALID_SOCKET
    
    // Threading
    std::thread receive_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    
    // Callbacks
    ConnectionCallback connection_callback_;
    ConfigCallback config_callback_;
    std::mutex callback_mutex_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    ClientStats stats_;
    
    // Audio config (received from iOS)
    mutable std::mutex config_mutex_;
    AudioConfig audio_config_;
    
    // Packet parser
    PacketParser parser_;
    
    // Reconnection
    static constexpr int kReconnectDelayMs = 1000;
    static constexpr int kReceiveBufferSize = 65536;
};

} // namespace iphone_mic
