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
#include "audio_dsp.h"
#include "registry_utils.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define EXPORT extern "C" __declspec(dllexport)

using namespace iphone_mic;

// iPhone Mic Application State
std::atomic<bool> g_AppRunning = false;
std::atomic<bool> g_IsConnected = false;
std::atomic<float> g_PeakL = 0.0f;
std::atomic<float> g_PeakR = 0.0f;
std::atomic<uint64_t> g_DroppedFrames = 0;
std::thread g_UsbThread;
AudioDSPPipeline g_Dsp;

// Audio Playback
std::atomic<bool> g_MonitorAudio = false;
std::atomic<int> g_AudioChannels = 1;
std::unique_ptr<RingBuffer<iphone_mic::AudioFrame>> g_AudioRingBuffer;
ma_device g_AudioDevice;
bool g_AudioDeviceReady = false;

bool g_IsPrebuffered = false;
const size_t PREBUFFER_FRAMES = 128; // ~2.6ms ultra low latency prebuffer @ 48kHz

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

void LoadOutputDeviceFromRegistry() {
    g_SavedDeviceName = registry::get_string("ASIOOutputDevice", "");
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
    if (deviceIndex >= 0 && deviceIndex < static_cast<int>(g_OutputDevices.size())) {
        registry::save_string("ASIOOutputDevice", g_OutputDevices[deviceIndex].name);
    }
    
    g_IsPrebuffered = false;
    
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = 48000;
    config.dataCallback      = data_callback;
    config.pUserData         = nullptr;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.periodSizeInFrames = 128;
    config.periods           = 2;
    config.wasapi.usage      = ma_wasapi_usage_pro_audio;
    config.wasapi.noAutoConvertSRC = 1;
    
    // Set specific device if not "System Default"
    if (deviceIndex > 0 && deviceIndex < static_cast<int>(g_OutputDevices.size())) {
        config.playback.pDeviceID = &g_OutputDevices[deviceIndex].id;
    }
    
    if (ma_device_init(NULL, &config, &g_AudioDevice) == MA_SUCCESS) {
        ma_device_start(&g_AudioDevice);
        g_AudioDeviceReady = true;
    }
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pDevice;
    (void)pInput;
    if (g_MonitorAudio && g_AudioRingBuffer) {
        if (!g_IsPrebuffered) {
            if (g_AudioRingBuffer->available_read() >= PREBUFFER_FRAMES) {
                g_IsPrebuffered = true;
            } else {
                std::memset(pOutput, 0, frameCount * sizeof(iphone_mic::AudioFrame));
                return;
            }
        }
        
        size_t available = g_AudioRingBuffer->available_read();
        iphone_mic::AudioFrame* outPtr = reinterpret_cast<iphone_mic::AudioFrame*>(pOutput);
        
        if (available >= frameCount) {
            g_AudioRingBuffer->read(outPtr, frameCount);
        } else if (available > 0) {
            g_AudioRingBuffer->read(outPtr, available);
            iphone_mic::AudioFrame last = outPtr[available - 1];
            size_t missing = frameCount - available;
            for (size_t i = 0; i < missing; ++i) {
                float fade = 1.0f - static_cast<float>(i + 1) / static_cast<float>(missing);
                outPtr[available + i].left = last.left * fade;
                outPtr[available + i].right = last.right * fade;
            }
        } else {
            std::memset(outPtr, 0, frameCount * sizeof(iphone_mic::AudioFrame));
        }
    } else {
        std::memset(pOutput, 0, frameCount * sizeof(iphone_mic::AudioFrame));
        if (g_AudioRingBuffer) {
            g_AudioRingBuffer->drain_all();
        }
        g_IsPrebuffered = false;
    }
}

void BackgroundUSBThread() {
    std::vector<uint8_t> payload_buf;
    payload_buf.reserve(65536);
    std::vector<iphone_mic::AudioFrame> audio_frames_buf;
    audio_frames_buf.reserve(1024);

    while (g_AppRunning) {
        g_IsConnected = false;
        g_PeakL = 0.0f;
        g_PeakR = 0.0f;
        
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
                if (payload_buf.size() < header.payload_size) {
                    payload_buf.resize(header.payload_size);
                }
                ret = recv(sock, reinterpret_cast<char*>(payload_buf.data()), header.payload_size, MSG_WAITALL);
                if (ret != static_cast<int>(header.payload_size)) {
                    g_IsConnected = false;
                    break;
                }

                iphone_mic::audio_convert::pcm16_to_audio_frames(payload_buf.data(), header.payload_size, g_AudioChannels.load(), audio_frames_buf);
                
                // Apply DSP Pipeline (80Hz HPF, Noise Gate, Gain, AGC / Limiter)
                g_Dsp.process(audio_frames_buf.data(), audio_frames_buf.size());

                float maxL = 0.0f;
                float maxR = 0.0f;
                
                for (const auto& frame : audio_frames_buf) {
                    maxL = std::max(maxL, std::abs(frame.left));
                    maxR = std::max(maxR, std::abs(frame.right));
                }
                
                g_PeakL = maxL;
                g_PeakR = maxR;

                // Push to playback buffer if monitoring
                if (g_MonitorAudio && g_AudioRingBuffer) {
                    // Keep buffer under 512 frames (~10.6ms) for immediate monitoring response
                    if (g_AudioRingBuffer->available_read() > 512) {
                        g_AudioRingBuffer->drain_to_latest(256);
                    }
                    size_t written = g_AudioRingBuffer->write(audio_frames_buf.data(), audio_frames_buf.size());
                    if (written < audio_frames_buf.size()) {
                        g_DroppedFrames += (audio_frames_buf.size() - written);
                    }
                }
                
            } else if (header.magic == PROTOCOL_MAGIC && header.type == static_cast<uint16_t>(PacketType::Config)) {
                if (payload_buf.size() < header.payload_size) {
                    payload_buf.resize(header.payload_size);
                }
                ret = recv(sock, reinterpret_cast<char*>(payload_buf.data()), header.payload_size, MSG_WAITALL);
                if (ret != static_cast<int>(header.payload_size)) {
                    g_IsConnected = false;
                    break;
                }
                
                std::string json(payload_buf.data(), payload_buf.data() + header.payload_size);
                auto cfg = AudioConfig::from_json(json);
                if (cfg && cfg->channels > 0) {
                    g_AudioChannels.store(cfg->channels);
                }
                
                PacketHeader ack_header;
                ack_header.magic = PROTOCOL_MAGIC;
                ack_header.version = PROTOCOL_VERSION;
                ack_header.type = static_cast<uint16_t>(PacketType::ConfigAck);
                ack_header.payload_size = 0;
                ack_header.reserved = 0;
                ack_header.timestamp = 0;
                
                send(sock, reinterpret_cast<const char*>(&ack_header), sizeof(ack_header), 0);
                
            } else {
                if (header.payload_size > 0) {
                    if (payload_buf.size() < header.payload_size) {
                        payload_buf.resize(header.payload_size);
                    }
                    recv(sock, reinterpret_cast<char*>(payload_buf.data()), header.payload_size, MSG_WAITALL);
                }
            }
        }
        closesocket(sock);
    }
}

// ============================================================================
// EXPORTED C FUNCTIONS
// ============================================================================

EXPORT void Backend_Init() {
    if (g_AppRunning) return;
    
    // Initialize Winsock
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
    
    // Initialize audio ring buffer (8192 frames = ~170ms max cushion)
    g_AudioRingBuffer = std::make_unique<RingBuffer<iphone_mic::AudioFrame>>(8192);
    
    // Load saved device preference and DSP settings
    LoadOutputDeviceFromRegistry();
    registry::apply_dsp_settings(g_Dsp);

    // Enumerate and initialize audio output device
    EnumerateOutputDevices();
    SwitchOutputDevice(g_SelectedDeviceIndex);
    
    g_AppRunning = true;
    g_UsbThread = std::thread(BackgroundUSBThread);
}

EXPORT void Backend_Shutdown() {
    if (!g_AppRunning) return;
    
    g_AppRunning = false;
    
    SOCKET dummy = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (dummy != INVALID_SOCKET) {
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(27015);
        connect(dummy, reinterpret_cast<SOCKADDR*>(&addr), sizeof(addr));
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

EXPORT const char* Backend_GetOutputDevicesCSV() {
    static std::string result;
    result.clear();
    
    EnumerateOutputDevices();
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
    if (index >= 0 && index < static_cast<int>(g_OutputDevices.size())) {
        SwitchOutputDevice(index);
    }
}

EXPORT void Backend_SetMonitorAudio(bool enable) {
    if (enable && g_AudioRingBuffer) {
        g_AudioRingBuffer->drain_all();
    }
    g_MonitorAudio = enable;
    g_IsPrebuffered = false;
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

// DSP Controls
EXPORT void Backend_SetGainPercent(int percent) {
    g_Dsp.set_gain_percent(percent);
    registry::save_dword("GainPercent", static_cast<DWORD>(percent));
}

EXPORT void Backend_SetMuted(bool mute) {
    g_Dsp.set_muted(mute);
    registry::save_dword("IsMuted", mute ? 1 : 0);
}

EXPORT void Backend_SetHighPassFilter(bool enable) {
    g_Dsp.set_high_pass_filter(enable);
    registry::save_dword("HighPassFilter", enable ? 1 : 0);
}

EXPORT void Backend_SetAGC(bool enable) {
    g_Dsp.set_agc(enable);
    registry::save_dword("AGC", enable ? 1 : 0);
}

EXPORT void Backend_SetLimiter(bool enable) {
    g_Dsp.set_limiter(enable);
    registry::save_dword("Limiter", enable ? 1 : 0);
}

EXPORT void Backend_SetNoiseGate(int level) {
    g_Dsp.set_noise_gate(level);
    registry::save_dword("NoiseGate", static_cast<DWORD>(level));
}

EXPORT void Backend_SetBufferSize(int size) {
    registry::save_dword("BufferSize", static_cast<DWORD>(size));
}

EXPORT void Backend_SetRecordOffset(int samples) {
    registry::save_dword("RecordOffsetSamples", static_cast<DWORD>(samples));
}

EXPORT int Backend_GetRecordOffset() {
    return static_cast<int>(registry::get_dword("RecordOffsetSamples", 0));
}

EXPORT uint64_t Backend_GetDroppedFrames() {
    return g_DroppedFrames.load();
}

EXPORT int Backend_GetSampleRate() {
    if (g_AudioDeviceReady) {
        return static_cast<int>(g_AudioDevice.sampleRate);
    }
    return 0;
}

EXPORT int Backend_GetBufferSize() {
    return static_cast<int>(registry::get_dword("BufferSize", 256));
}

// Main DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    (void)hModule;
    (void)lpReserved;
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

