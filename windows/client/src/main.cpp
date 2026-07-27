// main.cpp
// iPhone USB Microphone - Windows Client
//
// Standalone test client that connects to iPhone DIRECTLY over USB
// (no iproxy needed). Receives audio data and optionally saves to
// a .raw file for verification with Audacity.
//
// Usage:
//   iphone_mic_client.exe [options]
//     --save <file.raw>  Save received audio to raw file
//     --duration <sec>   Recording duration in seconds (default: 10)
//     --verbose          Enable verbose logging
//     --list-devices     List connected iOS devices and exit
//     --iproxy           Use legacy iproxy mode (connect to 127.0.0.1:8730)
//     --host <ip>        Host address (only with --iproxy, default: 127.0.0.1)
//     --port <port>      Port number (default: 8730)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <csignal>
#include <chrono>
#include <iomanip>

#include "usb_audio_client.h"
#include "usbmux_client.h"
#include "ring_buffer.h"
#include "audio_format.h"

using namespace iphone_mic;

// Global flag for Ctrl+C handling
static std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running.store(false);
}

// Parse command line arguments
struct Options {
    std::string host = "127.0.0.1";
    uint16_t port = DEFAULT_PORT;
    std::string save_file;
    int duration_sec = 10;
    bool verbose = false;
    bool use_iproxy = false;    // false = direct USB (default)
    bool list_devices = false;
};

Options parse_args(int argc, char* argv[]) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            opts.host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            opts.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--save" && i + 1 < argc) {
            opts.save_file = argv[++i];
        } else if (arg == "--duration" && i + 1 < argc) {
            opts.duration_sec = std::stoi(argv[++i]);
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "--iproxy") {
            opts.use_iproxy = true;
        } else if (arg == "--list-devices") {
            opts.list_devices = true;
        } else if (arg == "--help") {
            std::cout << "iPhone USB Microphone Client v1.1\n"
                      << "Usage: iphone_mic_client.exe [options]\n"
                      << "\n"
                      << "Connection (default: direct USB, no iproxy needed):\n"
                      << "  --list-devices     List connected iOS devices and exit\n"
                      << "  --iproxy           Use legacy iproxy mode\n"
                      << "  --host <ip>        Host (with --iproxy, default: 127.0.0.1)\n"
                      << "  --port <port>      Port (default: 8730)\n"
                      << "\n"
                      << "Recording:\n"
                      << "  --save <file.raw>  Save audio to raw file\n"
                      << "  --duration <sec>   Duration in seconds (default: 10)\n"
                      << "  --verbose          Verbose logging\n"
                      << "\nPrerequisites:\n"
                      << "  - iTunes or Apple Mobile Device Support installed\n"
                      << "  - iPhone connected via USB cable\n"
                      << "  - iPhoneMic app running on iPhone\n"
                      << "\nTo verify in Audacity:\n"
                      << "  File > Import > Raw Data\n"
                      << "  48000 Hz, Signed 24-bit PCM, Little-endian, Mono\n"
                      << std::endl;
            exit(0);
        }
    }
    return opts;
}

int main(int argc, char* argv[]) {
    Options opts = parse_args(argc, argv);
    
    // Set console output to UTF-8
    SetConsoleOutputCP(65001);
    
    std::cout << "========================================\n"
              << "  iPhone USB Microphone Client v1.1\n"
              << "========================================\n";
    
    // --list-devices: show connected devices and exit
    if (opts.list_devices) {
        std::cout << "\nChecking Apple Mobile Device Service...\n";
        
        if (!UsbMuxClient::is_service_available()) {
            std::cerr << "ERROR: Apple Mobile Device Service not available.\n"
                      << "Install iTunes from https://www.apple.com/itunes/\n";
            return 1;
        }
        
        std::cout << "Service available. Scanning for devices...\n\n";
        
        auto devices = UsbMuxClient::list_devices();
        if (devices.empty()) {
            std::cout << "No iOS devices found. Make sure:\n"
                      << "  1. iPhone is connected via USB\n"
                      << "  2. You tapped 'Trust This Computer' on iPhone\n";
        } else {
            std::cout << "Found " << devices.size() << " device(s):\n\n";
            for (const auto& dev : devices) {
                std::cout << "  Device ID:   " << dev.device_id << "\n"
                          << "  Serial:      " << dev.serial_number << "\n"
                          << "  Connection:  " << dev.connection_type << "\n"
                          << "  ---\n";
            }
        }
        return 0;
    }
    
    // Normal operation
    if (opts.use_iproxy) {
        std::cout << "Mode: iproxy (legacy)\n"
                  << "Host: " << opts.host << ":" << opts.port << "\n";
    } else {
        std::cout << "Mode: Direct USB (built-in usbmuxd)\n"
                  << "Port: " << opts.port << "\n";
        
        // Check service availability
        if (!UsbMuxClient::is_service_available()) {
            std::cerr << "\nERROR: Apple Mobile Device Service not available.\n"
                      << "Install iTunes from https://www.apple.com/itunes/\n";
            return 1;
        }
    }
    
    std::cout << "Duration: " << opts.duration_sec << "s\n";
    if (!opts.save_file.empty()) {
        std::cout << "Save to: " << opts.save_file << "\n";
    }
    std::cout << "========================================\n" << std::endl;
    
    // Install signal handler
    signal(SIGINT, signal_handler);
    
    // Create ring buffer (4 MB)
    auto ring_buffer = std::make_shared<RingBuffer>(4 * 1024 * 1024);
    
    // Connection strategy
    if (opts.use_iproxy) {
        // Legacy: connect to iproxy at host:port
        UsbAudioClient client(opts.host, opts.port, ring_buffer);
        
        client.set_connection_callback([](bool connected) {
            std::cout << (connected ? "[Connected to iPhone via iproxy]" : 
                                     "[Disconnected]") << std::endl;
        });
        
        client.set_config_callback([](const AudioConfig& config) {
            std::cout << "[Config] " << config.sample_rate << "Hz, "
                      << config.bit_depth << "bit, " << config.channels << "ch, "
                      << "buf=" << config.buffer_size << std::endl;
        });
        
        client.start();
        
        std::cout << "Waiting for iPhone (make sure iproxy " << opts.port 
                  << " " << opts.port << " is running)...\n";
        
        // Main loop
        auto start_time = std::chrono::steady_clock::now();
        std::ofstream save_file;
        if (!opts.save_file.empty()) {
            save_file.open(opts.save_file, std::ios::binary);
        }
        
        std::vector<uint8_t> read_buffer(4096);
        uint64_t total_bytes_read = 0;
        
        while (g_running.load()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time);
            if (elapsed.count() >= opts.duration_sec) break;
            
            size_t bytes_read = ring_buffer->read(read_buffer.data(), read_buffer.size());
            if (bytes_read > 0) {
                total_bytes_read += bytes_read;
                if (save_file.is_open()) {
                    save_file.write(reinterpret_cast<const char*>(read_buffer.data()), 
                                   bytes_read);
                }
            } else {
                Sleep(1);
            }
        }
        
        client.stop();
        
        if (save_file.is_open()) {
            save_file.close();
            std::cout << "\nSaved " << total_bytes_read << " bytes to " 
                      << opts.save_file << std::endl;
        }
        
    } else {
        // Direct USB mode: use built-in UsbMuxClient
        std::cout << "Connecting to iPhone via USB...\n"
                  << "Press Ctrl+C to stop\n\n";
        
        std::ofstream save_file;
        if (!opts.save_file.empty()) {
            save_file.open(opts.save_file, std::ios::binary);
            if (!save_file.is_open()) {
                std::cerr << "Failed to open save file: " << opts.save_file << "\n";
            }
        }
        
        PacketParser parser;
        std::vector<uint8_t> recv_buffer(65536);
        std::vector<uint8_t> read_buffer(4096);
        uint64_t total_bytes_read = 0;
        uint64_t packets_received = 0;
        
        auto start_time = std::chrono::steady_clock::now();
        auto last_stats_time = start_time;
        
        while (g_running.load()) {
            // Check duration
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time);
            if (elapsed.count() >= opts.duration_sec) {
                std::cout << "\nDuration reached (" << opts.duration_sec << "s)\n";
                break;
            }
            
            // Connect to iPhone
            SOCKET sock = UsbMuxClient::connect_to_device(opts.port);
            if (sock == INVALID_SOCKET) {
                std::cout << "\rWaiting for iPhone...    " << std::flush;
                Sleep(2000);
                continue;
            }
            
            std::cout << "\rConnected! Receiving audio...              \n";
            
            // Set receive timeout
            DWORD timeout = 500;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            
            parser.reset();
            
            // Receive loop
            while (g_running.load()) {
                auto now = std::chrono::steady_clock::now();
                auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - start_time);
                if (total_elapsed.count() >= opts.duration_sec) break;
                
                int bytes = recv(sock, reinterpret_cast<char*>(recv_buffer.data()),
                                static_cast<int>(recv_buffer.size()), 0);
                
                if (bytes > 0) {
                    parser.feed(recv_buffer.data(), static_cast<size_t>(bytes));
                    
                    PacketParser::ParsedPacket packet;
                    while (parser.try_parse(packet)) {
                        if (packet.header.packet_type() == PacketType::AudioData) {
                            packets_received++;
                            total_bytes_read += packet.payload.size();
                            
                            // Save to file
                            if (save_file.is_open()) {
                                save_file.write(
                                    reinterpret_cast<const char*>(packet.payload.data()),
                                    packet.payload.size());
                            }
                            
                            // Write to ring buffer
                            ring_buffer->write(packet.payload.data(), 
                                             packet.payload.size());
                            
                            // Display levels
                            if (opts.verbose && packet.payload.size() >= 2) {
                                float peak, rms;
                                audio_convert::calculate_levels_int16(
                                    packet.payload.data(),
                                    packet.header.payload_size / 2,
                                    peak, rms);
                                std::cout << "\r[Level] Peak: " << std::fixed 
                                          << std::setprecision(1) << peak 
                                          << " dBFS  RMS: " << rms << " dBFS  "
                                          << "Pkts: " << packets_received << "    " 
                                          << std::flush;
                            }
                            
                        } else if (packet.header.packet_type() == PacketType::Config) {
                            std::string json(packet.payload.begin(), 
                                           packet.payload.end());
                            auto config = AudioConfig::from_json(json);
                            if (config) {
                                std::cout << "[Config] " << config->sample_rate 
                                          << "Hz, " << config->bit_depth << "bit, "
                                          << config->channels << "ch, buf=" 
                                          << config->buffer_size << "\n";
                            }
                            // Send ACK
                            auto ack = iphone_mic::packet::build_config_ack();
                            send(sock, reinterpret_cast<const char*>(ack.data()),
                                 static_cast<int>(ack.size()), 0);
                                 
                        } else if (packet.header.packet_type() == PacketType::Heartbeat) {
                            auto hb = iphone_mic::packet::build_heartbeat();
                            send(sock, reinterpret_cast<const char*>(hb.data()),
                                 static_cast<int>(hb.size()), 0);
                        }
                    }
                } else if (bytes == 0) {
                    std::cout << "\nDisconnected.\n";
                    break;
                } else {
                    int err = WSAGetLastError();
                    if (err == WSAETIMEDOUT) continue;
                    std::cout << "\nConnection error: " << err << "\n";
                    break;
                }
                
                // Print stats periodically
                auto stats_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - last_stats_time);
                if (!opts.verbose && stats_elapsed.count() >= 2) {
                    std::cout << "\r[Stats] Received: " 
                              << (total_bytes_read / 1024) << " KB"
                              << "  Packets: " << packets_received
                              << "      " << std::flush;
                    last_stats_time = now;
                }
            }
            
            closesocket(sock);
        }
        
        if (save_file.is_open()) {
            save_file.close();
            std::cout << "\n========================================\n"
                      << "Audio saved to: " << opts.save_file << "\n"
                      << "Total bytes: " << total_bytes_read << "\n"
                      << "Total packets: " << packets_received << "\n"
                      << "\nTo verify in Audacity:\n"
                      << "  File > Import > Raw Data\n"
                      << "  Encoding: Signed 24-bit PCM\n"
                      << "  Byte order: Little-endian\n"
                      << "  Channels: 1 (Mono)\n"
                      << "  Sample rate: 48000 Hz\n"
                      << "========================================\n";
        }
    }
    
    std::cout << "\nDone.\n";
    return 0;
}
