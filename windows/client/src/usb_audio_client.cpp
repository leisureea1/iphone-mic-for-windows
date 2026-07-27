// usb_audio_client.cpp
// iPhone USB Microphone - Windows
//
// TCP client implementation for receiving audio from iPhone.

// Must include winsock2 before windows.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "usb_audio_client.h"

#include <iostream>
#include <chrono>
#include <algorithm>

namespace iphone_mic {

// ============================================================================
// Winsock Initialization Helper
// ============================================================================

class WinsockInit {
public:
    static WinsockInit& instance() {
        static WinsockInit inst;
        return inst;
    }
    
    bool is_ok() const { return ok_; }
    
private:
    WinsockInit() {
        WSADATA wsa_data;
        int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        ok_ = (result == 0);
        if (!ok_) {
            std::cerr << "[WinsockInit] WSAStartup failed: " << result << std::endl;
        }
    }
    
    ~WinsockInit() {
        if (ok_) WSACleanup();
    }
    
    bool ok_ = false;
};

// ============================================================================
// UsbAudioClient Implementation
// ============================================================================

UsbAudioClient::UsbAudioClient(const std::string& host, uint16_t port,
                                 std::shared_ptr<RingBuffer<AudioFrame>> ring_buffer)
    : host_(host)
    , port_(port)
    , ring_buffer_(std::move(ring_buffer))
    , socket_(INVALID_SOCKET)
{
    // Ensure Winsock is initialized
    WinsockInit::instance();
}

UsbAudioClient::~UsbAudioClient() {
    stop();
}

bool UsbAudioClient::start() {
    if (running_.load()) return true;
    
    running_.store(true);
    
    // Launch receive thread
    receive_thread_ = std::thread([this]() {
        receive_thread_func();
    });
    
    std::cout << "[Client] Started, connecting to " << host_ << ":" << port_ << std::endl;
    return true;
}

void UsbAudioClient::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    disconnect();
    
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    
    std::cout << "[Client] Stopped" << std::endl;
}

ClientStats UsbAudioClient::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    ClientStats s = stats_;
    s.connected = connected_.load();
    if (ring_buffer_) {
        s.buffer_fill_ratio = ring_buffer_->fill_ratio();
    }
    return s;
}

AudioConfig UsbAudioClient::get_audio_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return audio_config_;
}

void UsbAudioClient::set_connection_callback(ConnectionCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    connection_callback_ = std::move(cb);
}

void UsbAudioClient::set_config_callback(ConfigCallback cb) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    config_callback_ = std::move(cb);
}

void UsbAudioClient::send_config_request(const AudioConfig& config) {
    std::string json = config.to_json();
    PacketHeader header = PacketHeader::create(PacketType::Config, 
                                                static_cast<uint32_t>(json.size()));
    
    // Send header
    send_data(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    // Send payload
    send_data(reinterpret_cast<const uint8_t*>(json.data()), json.size());
}

// ============================================================================
// Private: Connection
// ============================================================================

bool UsbAudioClient::connect_to_server() {
    if (!WinsockInit::instance().is_ok()) {
        std::cerr << "[Client] Winsock not initialized" << std::endl;
        return false;
    }
    
    // Create socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[Client] Failed to create socket: " << WSAGetLastError() << std::endl;
        return false;
    }
    
    // Set TCP_NODELAY for low latency
    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, 
               reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
    
    // Set receive buffer size
    int recv_buf = kReceiveBufferSize * 4;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&recv_buf), sizeof(recv_buf));
    
    // Set send buffer size
    int send_buf = 32768;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
               reinterpret_cast<const char*>(&send_buf), sizeof(send_buf));
    
    // Resolve address
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
    
    // Connect
    int result = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (result == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }
    
    // Set non-blocking mode with a timeout for receive
    DWORD timeout = 500;  // 500ms receive timeout
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    
    socket_ = static_cast<uintptr_t>(sock);
    connected_.store(true);
    
    // Notify callback
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (connection_callback_) {
            connection_callback_(true);
        }
    }
    
    std::cout << "[Client] Connected to " << host_ << ":" << port_ << std::endl;
    return true;
}

void UsbAudioClient::disconnect() {
    if (socket_ != static_cast<uintptr_t>(INVALID_SOCKET)) {
        closesocket(static_cast<SOCKET>(socket_));
        socket_ = static_cast<uintptr_t>(INVALID_SOCKET);
    }
    
    bool was_connected = connected_.exchange(false);
    
    if (was_connected) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (connection_callback_) {
            connection_callback_(false);
        }
    }
    
    parser_.reset();
}

// ============================================================================
// Private: Receive Thread
// ============================================================================

void UsbAudioClient::receive_thread_func() {
    // Set thread priority to high for audio
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    
    std::vector<uint8_t> recv_buffer(kReceiveBufferSize);
    
    while (running_.load()) {
        // Try to connect if not connected
        if (!connected_.load()) {
            if (!connect_to_server()) {
                // Wait before retry
                for (int i = 0; i < kReconnectDelayMs / 50 && running_.load(); ++i) {
                    Sleep(50);
                }
                {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.reconnect_count++;
                }
                continue;
            }
        }
        
        // Receive data
        SOCKET sock = static_cast<SOCKET>(socket_);
        int bytes_received = recv(sock, reinterpret_cast<char*>(recv_buffer.data()),
                                  static_cast<int>(recv_buffer.size()), 0);
        
        if (bytes_received > 0) {
            // Feed data to parser
            parser_.feed(recv_buffer.data(), static_cast<size_t>(bytes_received));
            
            // Process all complete packets
            PacketParser::ParsedPacket packet;
            while (parser_.try_parse(packet)) {
                handle_packet(packet);
            }
            
        } else if (bytes_received == 0) {
            // Connection closed gracefully
            std::cout << "[Client] Connection closed by server" << std::endl;
            disconnect();
            
        } else {
            int error = WSAGetLastError();
            if (error == WSAETIMEDOUT) {
                // Receive timeout - normal, just continue
                continue;
            }
            // Real error
            std::cerr << "[Client] Receive error: " << error << std::endl;
            disconnect();
        }
    }
    
    disconnect();
}

// ============================================================================
// Private: Packet Handling
// ============================================================================

void UsbAudioClient::handle_packet(const PacketParser::ParsedPacket& packet) {
    switch (packet.header.packet_type()) {
        case PacketType::AudioData:
            handle_audio_data(packet.payload);
            break;
            
        case PacketType::Config:
            handle_config(packet.payload);
            break;
            
        case PacketType::Heartbeat:
            handle_heartbeat();
            break;
            
        case PacketType::ConfigAck:
            // Config acknowledged
            break;
    }
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.packets_received++;
        stats_.bytes_received += PacketHeader::SIZE + packet.payload.size();
    }
}

void UsbAudioClient::handle_audio_data(const std::vector<uint8_t>& payload) {
    if (payload.empty() || !ring_buffer_) return;
    
    // Convert 16-bit PCM to Float32 AudioFrames
    std::vector<AudioFrame> frames;
    int channels = 1;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        channels = audio_config_.channels > 0 ? audio_config_.channels : 1;
    }
    audio_convert::pcm16_to_audio_frames(payload.data(), payload.size(), channels, frames);
    
    // Write AudioFrames to ring buffer
    size_t written = ring_buffer_->write(frames.data(), frames.size());
    
    if (written < frames.size()) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.packets_dropped++;
    }
    
    // Calculate latency estimate from timestamp
    // (This is approximate - real latency includes USB transit time)
}

void UsbAudioClient::handle_config(const std::vector<uint8_t>& payload) {
    std::string json(payload.begin(), payload.end());
    auto config = AudioConfig::from_json(json);
    
    if (config) {
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            audio_config_ = *config;
        }
        
        std::cout << "[Client] Config received: "
                  << config->sample_rate << "Hz, "
                  << config->bit_depth << "bit, "
                  << config->channels << "ch, "
                  << "buf=" << config->buffer_size << std::endl;
        
        // Send ACK
        auto ack = packet::build_config_ack();
        send_data(ack.data(), ack.size());
        
        // Notify callback
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (config_callback_) {
                config_callback_(*config);
            }
        }
    }
}

void UsbAudioClient::handle_heartbeat() {
    // Respond with heartbeat ACK (reuse heartbeat packet)
    auto hb = packet::build_heartbeat();
    send_data(hb.data(), hb.size());
}

bool UsbAudioClient::send_data(const uint8_t* data, size_t length) {
    if (!connected_.load()) return false;
    
    SOCKET sock = static_cast<SOCKET>(socket_);
    int sent = send(sock, reinterpret_cast<const char*>(data), 
                    static_cast<int>(length), 0);
    return sent == static_cast<int>(length);
}

} // namespace iphone_mic
