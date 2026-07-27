#include <windows.h>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <string>
#include <sstream>
#include <algorithm>
#include <cmath>

#include "usbmux_client.h"
#include "protocol.h"
#include "ring_buffer.h"
#include "audio_format.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define EXPORT extern "C" __declspec(dllexport)

using namespace iphone_mic;

// iPhone Mic Application State
std::atomic<bool> g_AppRunning = false;
std::atomic<bool> g_IsConnected = false;
std::atomic<float> g_PeakL = 0.0f;
std::atomic<float> g_PeakR = 0.0f;
std::thread g_UsbThread;

// Audio Playback
std::atomic<bool> g_MonitorAudio = false;
std::atomic<int> g_AudioChannels = 1;
std::unique_ptr<RingBuffer<iphone_mic::AudioFrame>> g_AudioRingBuffer;
ma_device g_AudioDevice;
bool g_AudioDeviceReady = false;

bool g_IsPrebuffered = false;
const size_t PREBUFFER_FRAMES = 48000 * 20 / 1000; // 20ms of audio (960 frames)

// Output device enumeration
struct OutputDeviceInfo {
    ma_device_id id;
    std::string name;
    bool is_default;
};
std::vector<OutputDeviceInfo> g_OutputDevices;
int g_SelectedDeviceIndex = 0;  // 0 = system default
std::string g_SavedDeviceName = "";

// Forward declaration for miniaudio callback
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
void SwitchOutputDevice(int deviceIndex);

// Registry helper for sharing config with ASIO driver
void SaveOutputDeviceToRegistry(const std::string& deviceName) {
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\iPhoneMic", 0, NULL, 
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "ASIOOutputDevice", 0, REG_SZ, 
            reinterpret_cast<const BYTE*>(deviceName.c_str()), 
            static_cast<DWORD>(deviceName.length() + 1));
        RegCloseKey(hKey);
    }
}

void LoadOutputDeviceFromRegistry() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\iPhoneMic", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[256] = {0};
        DWORD bufferSize = sizeof(buffer);
        if (RegQueryValueExA(hKey, "ASIOOutputDevice", NULL, NULL, 
            reinterpret_cast<LPBYTE>(buffer), &bufferSize) == ERROR_SUCCESS) {
            // Null-terminate explicitly
            if (bufferSize < sizeof(buffer)) {
                buffer[bufferSize] = '\0';
            } else {
                buffer[sizeof(buffer) - 1] = '\0';
            }
            g_SavedDeviceName = buffer;
        }
        RegCloseKey(hKey);
    }
}

void EnumerateOutputDevices() {
    g_OutputDevices.clear();
    
    // First entry: system default
    OutputDeviceInfo defaultDev;
    defaultDev.name = "System Default";
    defaultDev.is_default = true;
    g_OutputDevices.push_back(defaultDev);
    
    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) return;
    
    ma_device_info* pPlaybackDevices;
    ma_uint32 playbackCount;
    ma_device_info* pCaptureDevices;
    ma_uint32 captureCount;
    
    if (ma_context_get_devices(&context, &pPlaybackDevices, &playbackCount, &pCaptureDevices, &captureCount) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < playbackCount; i++) {
            OutputDeviceInfo info;
            info.id = pPlaybackDevices[i].id;
            info.name = pPlaybackDevices[i].name;
            info.is_default = pPlaybackDevices[i].isDefault != 0;
            g_OutputDevices.push_back(info);
            
            // If this matches our saved device, select it
            if (!g_SavedDeviceName.empty() && info.name == g_SavedDeviceName) {
                g_SelectedDeviceIndex = static_cast<int>(g_OutputDevices.size()) - 1;
            }
        }
    }
    
    ma_context_uninit(&context);
}

void SwitchOutputDevice(int deviceIndex) {
    // Stop and uninit current device
    if (g_AudioDeviceReady) {
        ma_device_uninit(&g_AudioDevice);
        g_AudioDeviceReady = false;
    }
    
    g_SelectedDeviceIndex = deviceIndex;
    
    // Save to registry
    if (deviceIndex >= 0 && deviceIndex < (int)g_OutputDevices.size()) {
        SaveOutputDeviceToRegistry(g_OutputDevices[deviceIndex].name);
    }
    
    g_IsPrebuffered = false;
    
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = 48000;
    config.dataCallback      = data_callback;
    config.pUserData         = nullptr;
    
    // Set specific device if not "System Default"
    if (deviceIndex > 0 && deviceIndex < (int)g_OutputDevices.size()) {
        config.playback.pDeviceID = &g_OutputDevices[deviceIndex].id;
    }
    
    if (ma_device_init(NULL, &config, &g_AudioDevice) == MA_SUCCESS) {
        ma_device_start(&g_AudioDevice);
        g_AudioDeviceReady = true;
    }
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pInput; // Unused
    if (g_MonitorAudio && g_AudioRingBuffer) {
        if (!g_IsPrebuffered) {
            if (g_AudioRingBuffer->available_read() >= PREBUFFER_FRAMES) {
                g_IsPrebuffered = true;
            } else {
                std::memset(pOutput, 0, frameCount * sizeof(iphone_mic::AudioFrame));
                return;
            }
        }
        
        size_t framesRead = g_AudioRingBuffer->read(reinterpret_cast<iphone_mic::AudioFrame*>(pOutput), frameCount);
        if (framesRead < frameCount) {
            // Fill remainder with zeros (silence) to avoid audio glitches
            std::memset(reinterpret_cast<iphone_mic::AudioFrame*>(pOutput) + framesRead, 0, (frameCount - framesRead) * sizeof(iphone_mic::AudioFrame));
            g_IsPrebuffered = false; // Enter prebuffering mode due to underrun
        }
    } else {
        // Output silence
        std::memset(pOutput, 0, frameCount * sizeof(iphone_mic::AudioFrame));
        // Keep clearing the buffer so old audio doesn't queue up when disabled
        if (g_AudioRingBuffer) {
            iphone_mic::AudioFrame dummy[1024];
            while (g_AudioRingBuffer->read(dummy, 1024) > 0) {}
        }
        g_IsPrebuffered = false;
    }
}

void BackgroundUSBThread() {
    while (g_AppRunning) {
        g_IsConnected = false;
        g_PeakL = 0.0f;
        g_PeakR = 0.0f;
        
        // Wait for device
        SOCKET sock = UsbMuxClient::connect_to_device(8730);
        if (sock == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        g_IsConnected = true;

        while (g_AppRunning && g_IsConnected) {
            PacketHeader header;
            int ret = recv(sock, reinterpret_cast<char*>(&header), sizeof(header), MSG_WAITALL);
            if (ret != sizeof(header)) {
                g_IsConnected = false;
                break;
            }

            if (header.magic == PROTOCOL_MAGIC && header.type == static_cast<uint16_t>(PacketType::AudioData)) {
                std::vector<uint8_t> payload(header.payload_size);
                ret = recv(sock, reinterpret_cast<char*>(payload.data()), header.payload_size, MSG_WAITALL);
                if (ret != header.payload_size) {
                    g_IsConnected = false;
                    break;
                }

                std::vector<iphone_mic::AudioFrame> audioFrames;
                iphone_mic::audio_convert::pcm16_to_audio_frames(payload.data(), payload.size(), g_AudioChannels.load(), audioFrames);
                
                float maxL = 0.0f;
                float maxR = 0.0f;
                
                for (const auto& frame : audioFrames) {
                    maxL = std::max(maxL, std::abs(frame.left));
                    maxR = std::max(maxR, std::abs(frame.right));
                }
                
                // Decay peaks slightly for smoother animation or just set direct
                g_PeakL = maxL;
                g_PeakR = maxR;

                // Push to playback buffer if monitoring
                if (g_MonitorAudio && g_AudioRingBuffer) {
                    g_AudioRingBuffer->write(audioFrames.data(), audioFrames.size());
                }
                
            } else if (header.magic == PROTOCOL_MAGIC && header.type == static_cast<uint16_t>(PacketType::Config)) {
                std::vector<uint8_t> payload(header.payload_size);
                ret = recv(sock, reinterpret_cast<char*>(payload.data()), header.payload_size, MSG_WAITALL);
                if (ret != header.payload_size) {
                    g_IsConnected = false;
                    break;
                }
                
                std::string json(payload.begin(), payload.end());
                auto cfg = AudioConfig::from_json(json);
                if (cfg && cfg->channels > 0) {
                    g_AudioChannels.store(cfg->channels);
                }
                
                // Send ConfigAck
                PacketHeader ack_header;
                ack_header.magic = PROTOCOL_MAGIC;
                ack_header.version = PROTOCOL_VERSION;
                ack_header.type = static_cast<uint16_t>(PacketType::ConfigAck);
                ack_header.payload_size = 0;
                ack_header.reserved = 0;
                ack_header.timestamp = 0;
                
                send(sock, reinterpret_cast<const char*>(&ack_header), sizeof(ack_header), 0);
                
            } else {
                // Unknown packet, skip payload
                if (header.payload_size > 0) {
                    std::vector<uint8_t> dummy(header.payload_size);
                    recv(sock, reinterpret_cast<char*>(dummy.data()), header.payload_size, MSG_WAITALL);
                }
            }
        }
        closesocket(sock);
    }
}

// ============================================================================
// EXPORTED C FUNCTIONS FOR WPF
// ============================================================================

EXPORT void Backend_Init() {
    if (g_AppRunning) return;
    
    // Initialize Winsock
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
    
    // Initialize audio ring buffer
    g_AudioRingBuffer = std::make_unique<RingBuffer<iphone_mic::AudioFrame>>(96000);
    
    // Load saved device preference
    LoadOutputDeviceFromRegistry();

    // Enumerate and initialize audio output device
    EnumerateOutputDevices();
    SwitchOutputDevice(g_SelectedDeviceIndex);
    
    g_AppRunning = true;
    g_UsbThread = std::thread(BackgroundUSBThread);
}

EXPORT void Backend_Shutdown() {
    if (!g_AppRunning) return;
    
    g_AppRunning = false;
    
    // Create a dummy socket connection to unblock the recv call if it's blocked
    SOCKET dummy = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (dummy != INVALID_SOCKET) {
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(27015);
        connect(dummy, (SOCKADDR*)&addr, sizeof(addr));
        closesocket(dummy);
    }

    if (g_UsbThread.joinable()) {
        g_UsbThread.join();
    }
    
    if (g_AudioDeviceReady) {
        ma_device_uninit(&g_AudioDevice);
        g_AudioDeviceReady = false;
    }
    g_AudioRingBuffer.reset();
    
    WSACleanup();
}

// Returns a comma-separated list of devices. Caller should not free the pointer, it is static.
EXPORT const char* Backend_GetOutputDevicesCSV() {
    static std::string result;
    result.clear();
    
    EnumerateOutputDevices(); // refresh
    for (size_t i = 0; i < g_OutputDevices.size(); ++i) {
        result += g_OutputDevices[i].name;
        if (i < g_OutputDevices.size() - 1) {
            result += "|";
        }
    }
    return result.c_str();
}

EXPORT int Backend_GetSelectedDeviceIndex() {
    return g_SelectedDeviceIndex;
}

EXPORT void Backend_SetOutputDevice(int index) {
    if (index >= 0 && index < (int)g_OutputDevices.size()) {
        SwitchOutputDevice(index);
    }
}

EXPORT void Backend_SetMonitorAudio(bool enable) {
    g_MonitorAudio = enable;
}

EXPORT bool Backend_GetMonitorAudio() {
    return g_MonitorAudio.load();
}

EXPORT bool Backend_GetConnectionStatus() {
    return g_IsConnected.load();
}

EXPORT void Backend_GetAudioLevels(float* left, float* right) {
    if (left) *left = g_PeakL.load();
    if (right) *right = g_PeakR.load();
}

// Main DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
