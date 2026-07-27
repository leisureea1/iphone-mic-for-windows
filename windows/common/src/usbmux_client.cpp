// usbmux_client.cpp
// iPhone USB Microphone - Windows
//
// Implementation of the built-in usbmuxd protocol client.
// Speaks XML plist protocol directly to Apple Mobile Device Service.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "usbmux_client.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <chrono>

namespace iphone_mic {

// Static tag counter
std::atomic<uint32_t> UsbMuxClient::next_tag_{1};

// ============================================================================
// Winsock Helper (shared with usb_audio_client.cpp)
// ============================================================================

namespace {
    struct WinsockGuard {
        WinsockGuard() {
            WSADATA wsa;
            WSAStartup(MAKEWORD(2, 2), &wsa);
        }
        ~WinsockGuard() { WSACleanup(); }
    };
    
    void ensure_winsock() {
        static WinsockGuard guard;
    }
}

// ============================================================================
// Public Static Methods
// ============================================================================

bool UsbMuxClient::is_service_available() {
    ensure_winsock();
    SOCKET sock = connect_to_usbmuxd();
    if (sock == INVALID_SOCKET) return false;
    closesocket(sock);
    return true;
}

std::vector<DeviceInfo> UsbMuxClient::list_devices() {
    ensure_winsock();
    
    SOCKET sock = connect_to_usbmuxd();
    if (sock == INVALID_SOCKET) {
        std::cerr << "[UsbMux] Cannot connect to Apple Mobile Device Service.\n"
                  << "[UsbMux] Make sure iTunes or Apple Mobile Device Support is installed.\n";
        return {};
    }
    
    // Send ListDevices request
    uint32_t tag = next_tag_++;
    std::string plist = build_list_devices_plist();
    
    if (!send_plist(sock, plist, tag)) {
        closesocket(sock);
        return {};
    }
    
    // Receive response
    UsbMuxHeader resp_header;
    std::string response = recv_response(sock, resp_header);
    closesocket(sock);
    
    if (response.empty()) {
        return {};
    }
    
    return parse_device_list(response);
}

SOCKET UsbMuxClient::connect_to_device(uint16_t device_port, int device_id) {
    ensure_winsock();
    
    // If no device_id specified, find the first connected device
    if (device_id < 0) {
        auto devices = list_devices();
        if (devices.empty()) {
            std::cerr << "[UsbMux] No iOS devices connected via USB.\n";
            return INVALID_SOCKET;
        }
        
        // Prefer USB connections over Network
        for (const auto& dev : devices) {
            if (dev.connection_type == "USB" || dev.connection_type.empty()) {
                device_id = dev.device_id;
                std::cout << "[UsbMux] Found device: " << dev.serial_number.substr(0, 12) 
                          << "... (ID=" << dev.device_id << ")\n";
                break;
            }
        }
        
        if (device_id < 0) {
            // Use first device regardless of connection type
            device_id = devices[0].device_id;
            std::cout << "[UsbMux] Using device: " << devices[0].serial_number.substr(0, 12)
                      << "... (ID=" << device_id << ")\n";
        }
    }
    
    // Connect to usbmuxd
    SOCKET sock = connect_to_usbmuxd();
    if (sock == INVALID_SOCKET) {
        std::cerr << "[UsbMux] Cannot connect to Apple Mobile Device Service.\n";
        return INVALID_SOCKET;
    }
    
    // Send Connect request
    uint32_t tag = next_tag_++;
    std::string plist = build_connect_plist(device_id, device_port);
    
    if (!send_plist(sock, plist, tag)) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    
    // Receive response
    UsbMuxHeader resp_header;
    std::string response = recv_response(sock, resp_header);
    
    if (response.empty()) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    
    // Check result
    int result_code = parse_result(response);
    if (result_code != 0) {
        std::cerr << "[UsbMux] Connect failed with code " << result_code << "\n";
        if (result_code == 3) {
            std::cerr << "[UsbMux] Connection refused - is the iPhone app running?\n";
        } else if (result_code == 2) {
            std::cerr << "[UsbMux] Device not found (ID=" << device_id << ")\n";
        }
        closesocket(sock);
        return INVALID_SOCKET;
    }
    
    std::cout << "[UsbMux] USB tunnel established to iPhone port " << device_port << "\n";
    
    // At this point, the socket is a transparent TCP tunnel to the iOS device.
    // Set TCP_NODELAY for low latency audio
    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, 
               reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
    
    return sock;
}

// ============================================================================
// Device Monitoring
// ============================================================================

UsbMuxClient::~UsbMuxClient() {
    stop_monitoring();
}

void UsbMuxClient::start_monitoring(DeviceEventCallback callback) {
    if (monitoring_.load()) return;
    
    monitoring_.store(true);
    monitor_thread_ = std::thread([this, cb = std::move(callback)]() {
        monitor_thread_func(cb);
    });
}

void UsbMuxClient::stop_monitoring() {
    monitoring_.store(false);
    
    {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        if (monitor_socket_ != INVALID_SOCKET) {
            closesocket(monitor_socket_);
            monitor_socket_ = INVALID_SOCKET;
        }
    }
    
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

void UsbMuxClient::monitor_thread_func(DeviceEventCallback callback) {
    ensure_winsock();
    
    while (monitoring_.load()) {
        SOCKET sock = connect_to_usbmuxd();
        if (sock == INVALID_SOCKET) {
            Sleep(2000);
            continue;
        }
        
        {
            std::lock_guard<std::mutex> lock(monitor_mutex_);
            monitor_socket_ = sock;
        }
        
        // Send Listen request
        uint32_t tag = next_tag_++;
        if (!send_plist(sock, build_listen_plist(), tag)) {
            closesocket(sock);
            continue;
        }
        
        // Receive events in a loop
        while (monitoring_.load()) {
            UsbMuxHeader header;
            std::string response = recv_response(sock, header);
            
            if (response.empty()) break;
            
            // Check if it's an Attached or Detached event
            if (response.find("<string>Attached</string>") != std::string::npos) {
                DeviceInfo dev = parse_device_attached(response);
                if (dev.is_valid() && callback) {
                    callback(dev, true);
                }
            } else if (response.find("<string>Detached</string>") != std::string::npos) {
                DeviceInfo dev;
                dev.device_id = plist_get_integer(response, "DeviceID");
                if (callback) {
                    callback(dev, false);
                }
            }
            // Result messages (from Listen) are just acknowledgements
        }
        
        {
            std::lock_guard<std::mutex> lock(monitor_mutex_);
            if (monitor_socket_ != INVALID_SOCKET) {
                closesocket(monitor_socket_);
                monitor_socket_ = INVALID_SOCKET;
            }
        }
        
        if (monitoring_.load()) {
            Sleep(1000);  // Reconnect delay
        }
    }
}

// ============================================================================
// Private: usbmuxd Connection
// ============================================================================

SOCKET UsbMuxClient::connect_to_usbmuxd() {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(USBMUXD_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    // Set a connection timeout (3 seconds)
    DWORD timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    
    return sock;
}

// ============================================================================
// Private: Protocol Communication
// ============================================================================

bool UsbMuxClient::send_plist(SOCKET sock, const std::string& plist_xml, uint32_t tag) {
    UsbMuxHeader header;
    header.length = static_cast<uint32_t>(sizeof(UsbMuxHeader) + plist_xml.size());
    header.version = USBMUX_VERSION;
    header.type = static_cast<uint32_t>(MessageType::Plist);
    header.tag = tag;
    
    // Send header
    int sent = send(sock, reinterpret_cast<const char*>(&header), sizeof(header), 0);
    if (sent != sizeof(header)) return false;
    
    // Send plist body
    sent = send(sock, plist_xml.c_str(), static_cast<int>(plist_xml.size()), 0);
    if (sent != static_cast<int>(plist_xml.size())) return false;
    
    return true;
}

std::string UsbMuxClient::recv_response(SOCKET sock, UsbMuxHeader& out_header) {
    // Receive header (16 bytes)
    char header_buf[sizeof(UsbMuxHeader)];
    int total_received = 0;
    
    while (total_received < sizeof(UsbMuxHeader)) {
        int r = recv(sock, header_buf + total_received, 
                     sizeof(UsbMuxHeader) - total_received, 0);
        if (r <= 0) return "";
        total_received += r;
    }
    
    std::memcpy(&out_header, header_buf, sizeof(UsbMuxHeader));
    
    // Sanity check
    if (out_header.length < sizeof(UsbMuxHeader) || out_header.length > 1024 * 1024) {
        return "";
    }
    
    // Receive body
    uint32_t body_length = out_header.length - sizeof(UsbMuxHeader);
    if (body_length == 0) return "";
    
    std::string body(body_length, '\0');
    total_received = 0;
    
    while (total_received < static_cast<int>(body_length)) {
        int r = recv(sock, &body[total_received], 
                     static_cast<int>(body_length) - total_received, 0);
        if (r <= 0) return "";
        total_received += r;
    }
    
    return body;
}

// ============================================================================
// Private: XML Plist Building
// ============================================================================

std::string UsbMuxClient::build_list_devices_plist() {
    return 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "\t<key>MessageType</key>\n"
        "\t<string>ListDevices</string>\n"
        "\t<key>ClientVersionString</key>\n"
        "\t<string>iPhoneMic-1.0</string>\n"
        "\t<key>ProgName</key>\n"
        "\t<string>iPhoneMic</string>\n"
        "</dict>\n"
        "</plist>\n";
}

std::string UsbMuxClient::build_connect_plist(int device_id, uint16_t port) {
    // IMPORTANT: usbmuxd expects the port in network byte order (big-endian),
    // stored as a plain integer in the plist.
    // htons() converts from host byte order to network byte order.
    uint16_t port_ne = htons(port);
    
    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
       << "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
       << "<plist version=\"1.0\">\n"
       << "<dict>\n"
       << "\t<key>MessageType</key>\n"
       << "\t<string>Connect</string>\n"
       << "\t<key>DeviceID</key>\n"
       << "\t<integer>" << device_id << "</integer>\n"
       << "\t<key>PortNumber</key>\n"
       << "\t<integer>" << port_ne << "</integer>\n"
       << "\t<key>ClientVersionString</key>\n"
       << "\t<string>iPhoneMic-1.0</string>\n"
       << "\t<key>ProgName</key>\n"
       << "\t<string>iPhoneMic</string>\n"
       << "</dict>\n"
       << "</plist>\n";
    return ss.str();
}

std::string UsbMuxClient::build_listen_plist() {
    return 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "\t<key>MessageType</key>\n"
        "\t<string>Listen</string>\n"
        "\t<key>ClientVersionString</key>\n"
        "\t<string>iPhoneMic-1.0</string>\n"
        "\t<key>ProgName</key>\n"
        "\t<string>iPhoneMic</string>\n"
        "</dict>\n"
        "</plist>\n";
}

// ============================================================================
// Private: XML Plist Parsing (simple, no external dependency)
// ============================================================================

std::string UsbMuxClient::plist_get_string(const std::string& xml, const std::string& key) {
    // Find <key>KEY</key> followed by <string>VALUE</string>
    std::string search = "<key>" + key + "</key>";
    auto pos = xml.find(search);
    if (pos == std::string::npos) return "";
    
    pos += search.length();
    
    // Skip whitespace/newlines
    while (pos < xml.size() && (xml[pos] == ' ' || xml[pos] == '\t' || 
           xml[pos] == '\n' || xml[pos] == '\r')) {
        pos++;
    }
    
    // Look for <string>
    std::string str_open = "<string>";
    std::string str_close = "</string>";
    
    auto str_start = xml.find(str_open, pos);
    if (str_start == std::string::npos || str_start > pos + 20) return "";
    str_start += str_open.length();
    
    auto str_end = xml.find(str_close, str_start);
    if (str_end == std::string::npos) return "";
    
    return xml.substr(str_start, str_end - str_start);
}

int UsbMuxClient::plist_get_integer(const std::string& xml, const std::string& key) {
    std::string search = "<key>" + key + "</key>";
    auto pos = xml.find(search);
    if (pos == std::string::npos) return -1;
    
    pos += search.length();
    
    // Skip whitespace
    while (pos < xml.size() && (xml[pos] == ' ' || xml[pos] == '\t' || 
           xml[pos] == '\n' || xml[pos] == '\r')) {
        pos++;
    }
    
    // Look for <integer>
    std::string int_open = "<integer>";
    std::string int_close = "</integer>";
    
    auto int_start = xml.find(int_open, pos);
    if (int_start == std::string::npos || int_start > pos + 20) return -1;
    int_start += int_open.length();
    
    auto int_end = xml.find(int_close, int_start);
    if (int_end == std::string::npos) return -1;
    
    try {
        return std::stoi(xml.substr(int_start, int_end - int_start));
    } catch (...) {
        return -1;
    }
}

int UsbMuxClient::parse_result(const std::string& plist_xml) {
    return plist_get_integer(plist_xml, "Number");
}

std::vector<DeviceInfo> UsbMuxClient::parse_device_list(const std::string& plist_xml) {
    std::vector<DeviceInfo> devices;
    
    // The ListDevices response contains a DeviceList array with device dicts.
    // Each device dict contains Properties with SerialNumber, DeviceID, etc.
    
    // Find all DeviceID entries (each one represents a device)
    std::string::size_type search_pos = 0;
    
    while (true) {
        auto dev_pos = plist_xml.find("<key>DeviceID</key>", search_pos);
        if (dev_pos == std::string::npos) break;
        
        // Extract a reasonable chunk around this device entry
        // Look backward for the enclosing <dict>
        auto dict_start = plist_xml.rfind("<dict>", dev_pos);
        if (dict_start == std::string::npos) dict_start = dev_pos;
        
        // Look forward for the closing </dict>
        auto dict_end = plist_xml.find("</dict>", dev_pos);
        if (dict_end == std::string::npos) break;
        dict_end += 7;
        
        // Also check for a Properties sub-dict
        auto props_end = plist_xml.find("</dict>", dict_end);
        if (props_end != std::string::npos) {
            dict_end = props_end + 7;
        }
        
        std::string device_xml = plist_xml.substr(dict_start, dict_end - dict_start);
        
        DeviceInfo dev;
        dev.device_id = plist_get_integer(device_xml, "DeviceID");
        dev.serial_number = plist_get_string(device_xml, "SerialNumber");
        dev.connection_type = plist_get_string(device_xml, "ConnectionType");
        
        if (dev.serial_number.empty()) {
            // Try within Properties sub-dict
            auto props_pos = plist_xml.find("<key>Properties</key>", dict_start);
            if (props_pos != std::string::npos && props_pos < dict_end + 500) {
                auto props_dict_end = plist_xml.find("</dict>", props_pos + 50);
                if (props_dict_end != std::string::npos) {
                    std::string props_xml = plist_xml.substr(props_pos, 
                        props_dict_end + 7 - props_pos);
                    dev.serial_number = plist_get_string(props_xml, "SerialNumber");
                    dev.connection_type = plist_get_string(props_xml, "ConnectionType");
                }
            }
        }
        
        if (dev.device_id > 0) {
            devices.push_back(dev);
        }
        
        search_pos = dev_pos + 10;
    }
    
    return devices;
}

DeviceInfo UsbMuxClient::parse_device_attached(const std::string& plist_xml) {
    DeviceInfo dev;
    dev.device_id = plist_get_integer(plist_xml, "DeviceID");
    dev.serial_number = plist_get_string(plist_xml, "SerialNumber");
    dev.connection_type = plist_get_string(plist_xml, "ConnectionType");
    return dev;
}

} // namespace iphone_mic
