// usbmux_client.h
// iPhone USB Microphone - Windows
//
// Built-in usbmuxd protocol client - replaces the need for external iproxy.
//
// When iTunes or Apple Mobile Device Support is installed on Windows,
// the Apple Mobile Device Service (AMDS) listens on 127.0.0.1:27015.
// This client speaks the usbmuxd protocol to:
//   1. Discover connected iOS devices
//   2. Establish TCP tunnels to specific ports on the device
//
// After a successful tunnel, the socket behaves as a direct TCP connection
// to the iOS app's listening port.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace iphone_mic {

/// Information about a connected iOS device
struct DeviceInfo {
    int      device_id = 0;       // usbmuxd internal device ID
    std::string serial_number;    // UDID
    std::string product_id;       // USB product ID
    std::string connection_type;  // "USB" or "Network"
    
    bool is_valid() const { return device_id > 0 && !serial_number.empty(); }
};

/// Callback for device attach/detach events
using DeviceEventCallback = std::function<void(const DeviceInfo& device, bool attached)>;

/// UsbMux client - speaks the Apple usbmuxd protocol directly.
/// No iproxy or libimobiledevice dependency required.
/// Only requires Apple Mobile Device Support (installed with iTunes).
class UsbMuxClient {
public:
    /// usbmuxd service port on Windows (Apple Mobile Device Service)
    static constexpr uint16_t USBMUXD_PORT = 27015;
    
    /// usbmuxd protocol version
    static constexpr uint32_t USBMUX_VERSION = 1;  // Plist-based protocol
    
    /// usbmuxd message types
    enum class MessageType : uint32_t {
        Result   = 1,
        Connect  = 2,
        Listen   = 3,
        Plist    = 8,   // XML plist message (protocol version 1)
    };
    
    // ================================================================
    // Static convenience methods
    // ================================================================
    
    /// List all currently connected iOS devices.
    /// Returns an empty vector if no devices or usbmuxd unavailable.
    static std::vector<DeviceInfo> list_devices();
    
    /// Connect to a specific port on a connected iOS device.
    /// Returns a connected SOCKET on success, INVALID_SOCKET on failure.
    /// 
    /// If device_id is -1, automatically selects the first USB device found.
    /// After this call returns a valid socket, the socket is a transparent
    /// TCP tunnel to the iOS device's port - use recv/send as normal.
    ///
    /// @param device_port  The TCP port the iOS app is listening on (e.g. 8730)
    /// @param device_id    Specific device ID, or -1 for auto-detect
    /// @return Connected socket, or INVALID_SOCKET on error
    static SOCKET connect_to_device(uint16_t device_port, int device_id = -1);
    
    /// Check if Apple Mobile Device Service is running on this machine.
    static bool is_service_available();

    // ================================================================
    // Device monitoring (async)
    // ================================================================
    
    /// Start monitoring for device attach/detach events.
    /// The callback fires on a background thread.
    void start_monitoring(DeviceEventCallback callback);
    
    /// Stop device monitoring.
    void stop_monitoring();
    
    /// Check if currently monitoring
    bool is_monitoring() const { return monitoring_.load(); }
    
    ~UsbMuxClient();

private:
    // usbmuxd packet header (16 bytes)
    #pragma pack(push, 1)
    struct UsbMuxHeader {
        uint32_t length;    // Total packet length (header + payload)
        uint32_t version;   // Protocol version (1 for plist)
        uint32_t type;      // Message type
        uint32_t tag;       // Request/response tag for matching
    };
    #pragma pack(pop)
    
    static_assert(sizeof(UsbMuxHeader) == 16, "UsbMuxHeader must be 16 bytes");
    
    // Connect to the local usbmuxd service
    static SOCKET connect_to_usbmuxd();
    
    // Send a plist message to usbmuxd
    static bool send_plist(SOCKET sock, const std::string& plist_xml, uint32_t tag);
    
    // Receive a response from usbmuxd
    // Returns the XML plist body, or empty string on error
    static std::string recv_response(SOCKET sock, UsbMuxHeader& out_header);
    
    // Parse the Result response - returns the result code (0 = success)
    static int parse_result(const std::string& plist_xml);
    
    // Parse device list response
    static std::vector<DeviceInfo> parse_device_list(const std::string& plist_xml);
    
    // Parse a single device info from Attached event
    static DeviceInfo parse_device_attached(const std::string& plist_xml);
    
    // Extract a string value from plist XML by key
    static std::string plist_get_string(const std::string& xml, const std::string& key);
    
    // Extract an integer value from plist XML by key
    static int plist_get_integer(const std::string& xml, const std::string& key);
    
    // Build XML plist for ListDevices request
    static std::string build_list_devices_plist();
    
    // Build XML plist for Connect request
    static std::string build_connect_plist(int device_id, uint16_t port);
    
    // Build XML plist for Listen request
    static std::string build_listen_plist();
    
    // Monitoring thread
    void monitor_thread_func(DeviceEventCallback callback);
    
    std::thread monitor_thread_;
    std::atomic<bool> monitoring_{false};
    SOCKET monitor_socket_ = INVALID_SOCKET;
    std::mutex monitor_mutex_;
    
    // Tag counter for request/response matching
    static std::atomic<uint32_t> next_tag_;
};

} // namespace iphone_mic
