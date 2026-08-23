// iphone_asio_driver.cpp
// iPhone USB Microphone - ASIO Driver Implementation
//
// Implements the full IASIO interface for DAW integration.
// Internally manages a TCP client to receive audio from iPhone.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "iphone_asio_driver.h"
#include "usbmux_client.h"

#include <cstring>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <timeapi.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

#define MINIAUDIO_IMPLEMENTATION
#include "../../third_party/miniaudio/miniaudio.h"

namespace iphone_mic {


// ============================================================================
// Constructor / Destructor
// ============================================================================

iPhoneAsioDriver::iPhoneAsioDriver()
    : ref_count_(1)
    , input_ring_buffer_(std::make_shared<RingBuffer<AudioFrame>>(96000))
    , output_ring_buffer_(std::make_shared<RingBuffer<AudioFrame>>(96000))
{
    std::memset(error_message_, 0, sizeof(error_message_));
}

iPhoneAsioDriver::~iPhoneAsioDriver() {
    stop();
    disposeBuffers();
    stop_usb_client();
}

// ============================================================================
// IUnknown Implementation
// ============================================================================

HRESULT STDMETHODCALLTYPE iPhoneAsioDriver::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    
    *ppv = nullptr;
    
    if (IsEqualIID(riid, IID_IUnknown)) {
        *ppv = static_cast<IUnknown*>(this);
    } else {
        // ASIO uses a custom interface, not a standard COM IID
        *ppv = static_cast<IASIO*>(this);
    }
    
    if (*ppv) {
        AddRef();
        return S_OK;
    }
    
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE iPhoneAsioDriver::AddRef() {
    return InterlockedIncrement(&ref_count_);
}

ULONG STDMETHODCALLTYPE iPhoneAsioDriver::Release() {
    ULONG count = InterlockedDecrement(&ref_count_);
    if (count == 0) {
        delete this;
    }
    return count;
}

// ============================================================================
// IASIO: Initialization
// ============================================================================

ASIOBool iPhoneAsioDriver::init(void* sysHandle) {
    if (initialized_) return ASIOTrue;
    
    sys_handle_ = static_cast<HWND>(sysHandle);
    
    // Start USB client to receive audio from iPhone
    start_usb_client();
    
    initialized_ = true;
    strcpy_s(error_message_, "No error");
    
    return ASIOTrue;
}

void iPhoneAsioDriver::getDriverName(char* name) {
    strcpy_s(name, 32, DRIVER_NAME);
}

long iPhoneAsioDriver::getDriverVersion() {
    return DRIVER_VERSION;
}

void iPhoneAsioDriver::getErrorMessage(char* string) {
    strcpy_s(string, 128, error_message_);
}

// ============================================================================
// IASIO: Start / Stop
// ============================================================================

ASIOError iPhoneAsioDriver::start() {
    if (!initialized_) return ASE_NotPresent;
    if (running_) return ASE_OK;
    if (!callbacks_) return ASE_InvalidMode;
    
    // Reset sample position
    sample_position_.store(0);
    current_buffer_index_ = 0;
    
    // Reset ASRC & DSP state
    resampler_.reset();
    dsp_.reset();
    load_dsp_settings_from_registry();
    
    // Start output playback engine
    start_playback_engine();
    
    // Start the timer that drives buffer callbacks
    start_timer();
    
    running_ = true;
    return ASE_OK;
}

ASIOError iPhoneAsioDriver::stop() {
    if (!running_) return ASE_OK;
    
    running_ = false;
    stop_timer();
    stop_playback_engine();
    
    return ASE_OK;
}

// ============================================================================
// Output Playback Engine (miniaudio)
// ============================================================================

static void asio_playback_data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    iPhoneAsioDriver* driver = static_cast<iPhoneAsioDriver*>(pDevice->pUserData);
    if (!driver) return;
    
    size_t framesRead = driver->read_output_buffer(reinterpret_cast<AudioFrame*>(pOutput), frameCount);
    if (framesRead < frameCount) {
        // Fill remainder with zeros to avoid audio glitches
        std::memset(reinterpret_cast<AudioFrame*>(pOutput) + framesRead, 0, (frameCount - framesRead) * sizeof(AudioFrame));
    }
}

size_t iPhoneAsioDriver::read_output_buffer(AudioFrame* out, size_t frames) {
    if (output_ring_buffer_) {
        return output_ring_buffer_->read(out, frames);
    }
    return 0;
}

void iPhoneAsioDriver::start_playback_engine() {
    if (playback_device_) return;
    
    // Read the saved output device from the registry
    std::string savedDeviceName;
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\iPhoneMic", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[256] = {0};
        DWORD bufferSize = sizeof(buffer);
        if (RegQueryValueExA(hKey, "ASIOOutputDevice", NULL, NULL, 
            reinterpret_cast<LPBYTE>(buffer), &bufferSize) == ERROR_SUCCESS) {
            savedDeviceName = buffer;
        }
        RegCloseKey(hKey);
    }
    
    playback_device_ = new ma_device;
    
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = 48000;
    config.dataCallback      = asio_playback_data_callback;
    config.pUserData         = this;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.periodSizeInFrames = static_cast<ma_uint32>(buffer_size_ > 0 ? buffer_size_ : 128);
    config.periods           = 2;
    config.wasapi.usage      = ma_wasapi_usage_pro_audio;
    config.wasapi.noAutoConvertSRC = 1;
    
    // If a specific device was saved, we need to find its ID
    ma_device_id deviceId;
    bool foundDevice = false;
    
    if (!savedDeviceName.empty()) {
        ma_context context;
        if (ma_context_init(NULL, 0, NULL, &context) == MA_SUCCESS) {
            ma_device_info* pPlaybackDevices;
            ma_uint32 playbackCount;
            if (ma_context_get_devices(&context, &pPlaybackDevices, &playbackCount, NULL, NULL) == MA_SUCCESS) {
                for (ma_uint32 i = 0; i < playbackCount; i++) {
                    if (savedDeviceName == pPlaybackDevices[i].name) {
                        deviceId = pPlaybackDevices[i].id;
                        foundDevice = true;
                        break;
                    }
                }
            }
            ma_context_uninit(&context);
        }
    }
    
    if (foundDevice) {
        config.playback.pDeviceID = &deviceId;
    }
    
    if (ma_device_init(NULL, &config, playback_device_) == MA_SUCCESS) {
        ma_device_start(playback_device_);
    } else {
        delete playback_device_;
        playback_device_ = nullptr;
    }
}

void iPhoneAsioDriver::stop_playback_engine() {
    if (playback_device_) {
        ma_device_uninit(playback_device_);
        delete playback_device_;
        playback_device_ = nullptr;
    }
}

// ============================================================================
// IASIO: Channel & Buffer Info
// ============================================================================

ASIOError iPhoneAsioDriver::getChannels(long* numInputChannels, 
                                          long* numOutputChannels) {
    if (!numInputChannels || !numOutputChannels) return ASE_InvalidParameter;
    
    *numInputChannels = NUM_INPUT_CHANNELS;
    *numOutputChannels = NUM_OUTPUT_CHANNELS;
    return ASE_OK;
}

ASIOError iPhoneAsioDriver::getLatencies(long* inputLatency, long* outputLatency) {
    if (!inputLatency || !outputLatency) return ASE_InvalidParameter;
    
    // Input latency = buffer size (we're filling from ring buffer)
    // Add a safety margin for USB transit
    *inputLatency = buffer_size_ * 2;
    *outputLatency = 0;
    return ASE_OK;
}

ASIOError iPhoneAsioDriver::getBufferSize(long* minSize, long* maxSize,
                                            long* preferredSize, long* granularity) {
    if (!minSize || !maxSize || !preferredSize || !granularity)
        return ASE_InvalidParameter;
    
    *minSize = MIN_BUFFER_SIZE;
    *maxSize = MAX_BUFFER_SIZE;
    *preferredSize = PREFERRED_BUFFER_SIZE;

    // Check registry for user preference
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\iPhoneMic", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwVal = 0;
        DWORD dwSize = sizeof(dwVal);
        if (RegQueryValueExA(hKey, "BufferSize", NULL, NULL, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS) {
            if (is_valid_buffer_size(static_cast<long>(dwVal))) {
                *preferredSize = static_cast<long>(dwVal);
            }
        }
        RegCloseKey(hKey);
    }

    *granularity = -1;  // -1 = power of 2 steps
    return ASE_OK;
}

// ============================================================================
// IASIO: Sample Rate
// ============================================================================

ASIOError iPhoneAsioDriver::canSampleRate(ASIOSampleRate sampleRate) {
    // Only support 48000 Hz
    if (std::abs(sampleRate - SUPPORTED_SAMPLE_RATE) < 1.0) {
        return ASE_OK;
    }
    return ASE_NoClock;
}

ASIOError iPhoneAsioDriver::getSampleRate(ASIOSampleRate* sampleRate) {
    if (!sampleRate) return ASE_InvalidParameter;
    *sampleRate = sample_rate_;
    return ASE_OK;
}

ASIOError iPhoneAsioDriver::setSampleRate(ASIOSampleRate sampleRate) {
    if (std::abs(sampleRate - SUPPORTED_SAMPLE_RATE) < 1.0) {
        sample_rate_ = SUPPORTED_SAMPLE_RATE;
        return ASE_OK;
    }
    return ASE_NoClock;
}

// ============================================================================
// IASIO: Clock Source
// ============================================================================

ASIOError iPhoneAsioDriver::getClockSources(ASIOClockSource* clocks, 
                                              long* numSources) {
    if (!clocks || !numSources) return ASE_InvalidParameter;
    
    clocks[0].index = 0;
    clocks[0].associatedChannel = -1;
    clocks[0].associatedGroup = -1;
    clocks[0].isCurrentSource = ASIOTrue;
    strcpy_s(clocks[0].name, "iPhone USB");
    
    *numSources = 1;
    return ASE_OK;
}

ASIOError iPhoneAsioDriver::setClockSource(long reference) {
    if (reference != 0) return ASE_InvalidParameter;
    return ASE_OK;
}

// ============================================================================
// IASIO: Sample Position
// ============================================================================

ASIOError iPhoneAsioDriver::getSamplePosition(ASIOSamples* sPos, 
                                                ASIOTimeStamp* tStamp) {
    if (!sPos || !tStamp) return ASE_InvalidParameter;
    
    uint64_t pos = sample_position_.load();
    sPos->hi = static_cast<unsigned long>(pos >> 32);
    sPos->lo = static_cast<unsigned long>(pos & 0xFFFFFFFF);
    
    // System time in nanoseconds (using QPC)
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    double seconds = static_cast<double>(count.QuadPart) / 
                     static_cast<double>(freq.QuadPart);
    uint64_t sysTimeNs = static_cast<uint64_t>(seconds * 1e9);
    tStamp->hi = static_cast<unsigned long>(sysTimeNs >> 32);
    tStamp->lo = static_cast<unsigned long>(sysTimeNs & 0xFFFFFFFF);
    
    return ASE_OK;
}

// ============================================================================
// IASIO: Channel Info
// ============================================================================

ASIOError iPhoneAsioDriver::getChannelInfo(ASIOChannelInfo* info) {
    if (!info) return ASE_InvalidParameter;
    
    if (info->isInput) {
        if (info->channel < 0 || info->channel >= NUM_INPUT_CHANNELS) {
            return ASE_InvalidParameter;
        }
        
        info->type = ASIOSTInt32LSB;
        info->channelGroup = 0;
        info->isActive = ASIOTrue;
        
        if (info->channel == 0) {
            strcpy_s(info->name, "iPhone Mic L");
        } else {
            strcpy_s(info->name, "iPhone Mic R");
        }
    } else {
        if (info->channel < 0 || info->channel >= NUM_OUTPUT_CHANNELS) {
            return ASE_InvalidParameter;
        }
        
        info->type = ASIOSTInt32LSB;
        info->channelGroup = 1;
        info->isActive = ASIOTrue;
        
        if (info->channel == 0) {
            strcpy_s(info->name, "iPhone Output L");
        } else {
            strcpy_s(info->name, "iPhone Output R");
        }
    }
    
    return ASE_OK;
}

// ============================================================================
// IASIO: Buffer Management
// ============================================================================

ASIOError iPhoneAsioDriver::createBuffers(ASIOBufferInfo* bufferInfos, 
                                            long numChannels,
                                            long bufferSize, 
                                            ASIOCallbacks* callbacks) {
    if (!bufferInfos || !callbacks) return ASE_InvalidParameter;
    if (numChannels <= 0 || numChannels > (NUM_INPUT_CHANNELS + NUM_OUTPUT_CHANNELS)) 
        return ASE_InvalidParameter;
    
    // Validate buffer size
    if (!is_valid_buffer_size(bufferSize)) {
        strcpy_s(error_message_, "Invalid buffer size");
        return ASE_InvalidParameter;
    }
    
    // Dispose existing buffers
    disposeBuffers();
    
    buffer_size_ = bufferSize;
    callbacks_ = callbacks;
    num_active_channels_ = numChannels;
    host_buffer_infos_ = bufferInfos;
    
    // Allocate channel buffers (Int32 format, double-buffered)
    // Each buffer holds bufferSize samples of int32_t
    channel_buffers_.resize(numChannels);
    
    for (long i = 0; i < numChannels; ++i) {
        auto& cb = channel_buffers_[i];
        cb.buffer_a.resize(static_cast<size_t>(bufferSize), 0);
        cb.buffer_b.resize(static_cast<size_t>(bufferSize), 0);
        cb.is_input = bufferInfos[i].isInput != 0;
        cb.channel_num = bufferInfos[i].channelNum;
        
        // Set buffer pointers for the host
        bufferInfos[i].buffers[0] = cb.buffer_a.data();
        bufferInfos[i].buffers[1] = cb.buffer_b.data();
    }
    
    return ASE_OK;
}

ASIOError iPhoneAsioDriver::disposeBuffers() {
    if (running_) {
        stop();
    }
    
    channel_buffers_.clear();
    host_buffer_infos_ = nullptr;
    callbacks_ = nullptr;
    num_active_channels_ = 0;
    
    return ASE_OK;
}

// ============================================================================
// IASIO: Control Panel & Future
// ============================================================================

// Static helper to resolve our own DLL path
static const char* get_self_address() {
    return reinterpret_cast<const char*>(&get_self_address);
}

ASIOError iPhoneAsioDriver::controlPanel() {
    // 1. Try registry path first
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\iPhoneMic", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char regExe[MAX_PATH] = {};
        DWORD regSize = sizeof(regExe);
        if (RegQueryValueExA(hKey, "InstallPath", NULL, NULL, reinterpret_cast<LPBYTE>(regExe), &regSize) == ERROR_SUCCESS) {
            if (GetFileAttributesA(regExe) != INVALID_FILE_ATTRIBUTES) {
                HINSTANCE result = ShellExecuteA(sys_handle_, "open", regExe, nullptr, nullptr, SW_SHOWNORMAL);
                RegCloseKey(hKey);
                if (reinterpret_cast<intptr_t>(result) > 32) return ASE_OK;
            }
        }
        RegCloseKey(hKey);
    }

    // 2. Try candidate exe names next to driver DLL or in common paths
    char dll_dir[MAX_PATH] = {};
    HMODULE hModule = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        get_self_address(),
        &hModule);
    
    if (hModule) {
        GetModuleFileNameA(hModule, dll_dir, MAX_PATH);
        char* last_slash = strrchr(dll_dir, '\\');
        if (last_slash) {
            *last_slash = '\0';
        }
    }

    const char* candidates[] = {
        "\\iPhone Mic.exe",
        "\\iPhoneMic.exe",
        "\\iphone_mic.exe",
        "\\iphone_mic_gui.exe",
        "\\..\\..\\..\\dist\\win-unpacked\\iPhone Mic.exe",
        "\\..\\..\\..\\dist\\iPhoneMic_Setup.exe"
    };

    for (const char* candidate : candidates) {
        char target_path[MAX_PATH] = {};
        strcpy_s(target_path, dll_dir);
        strcat_s(target_path, candidate);
        
        char resolved[MAX_PATH] = {};
        if (GetFullPathNameA(target_path, MAX_PATH, resolved, nullptr)) {
            if (GetFileAttributesA(resolved) != INVALID_FILE_ATTRIBUTES) {
                HINSTANCE result = ShellExecuteA(sys_handle_, "open", resolved, nullptr, nullptr, SW_SHOWNORMAL);
                if (reinterpret_cast<intptr_t>(result) > 32) {
                    return ASE_OK;
                }
            }
        }
    }
    
    // Fallback: show info dialog
    if (sys_handle_) {
        MessageBoxA(sys_handle_, 
            "iPhone USB Microphone ASIO v1.0\n\n"
            "请启动 iPhoneMic 控制中心 (iPhone Mic.exe) 进行设备与音频设置。\n\n"
            "支持特性：\n"
            "• 48000 Hz / 32-bit 低延迟输出\n"
            "• 原生 USB 耳机与声卡直通\n"
            "• ASRC 硬件时钟自适应同步",
            DRIVER_NAME,
            MB_OK | MB_ICONINFORMATION);
    }
    return ASE_OK;
}

ASIOError iPhoneAsioDriver::future(long selector, void* opt) {
    switch (selector) {
        case kAsioCanInputMonitor:
            return ASE_OK;  // We support input monitoring
        case kAsioCanTimeInfo:
            return ASE_OK;
        case kAsioCanTimeCode:
            return ASE_OK;
        default:
            return ASE_InvalidParameter;
    }
}

ASIOError iPhoneAsioDriver::outputReady() {
    // We have no outputs, but return OK
    return ASE_OK;
}

// ============================================================================
// Private: USB Client Thread
// ============================================================================

void iPhoneAsioDriver::start_usb_client() {
    if (usb_client_running_.load()) return;
    
    usb_client_running_.store(true);
    usb_client_thread_ = std::thread([this]() {
        usb_client_thread_func();
    });
}

void iPhoneAsioDriver::stop_usb_client() {
    usb_client_running_.store(false);
    if (usb_client_thread_.joinable()) {
        usb_client_thread_.join();
    }
}

void iPhoneAsioDriver::usb_client_thread_func() {
    // Initialize Winsock
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
    
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    
    PacketParser parser;
    std::vector<uint8_t> recv_buffer(65536);
    
    while (usb_client_running_.load()) {
        // Connect directly to iPhone via built-in usbmuxd protocol.
        // No iproxy needed! UsbMuxClient speaks the Apple Mobile Device
        // Service protocol to establish a USB tunnel.
        SOCKET sock = UsbMuxClient::connect_to_device(DEFAULT_PORT);
        if (sock == INVALID_SOCKET) {
            // No device found or connection refused - retry
            Sleep(1000);
            continue;
        }
        
        // Set receive timeout
        DWORD timeout = 500;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        
        // Receive loop
        parser.reset();
        int current_channels = 1;
        
        while (usb_client_running_.load()) {
            int bytes = recv(sock, reinterpret_cast<char*>(recv_buffer.data()),
                            static_cast<int>(recv_buffer.size()), 0);
            
            if (bytes > 0) {
                parser.feed(recv_buffer.data(), static_cast<size_t>(bytes));
                
                PacketParser::ParsedPacket packet;
                while (parser.try_parse(packet)) {
                    if (packet.header.packet_type() == PacketType::AudioData) {
                        // Convert PCM16 to Float32 AudioFrames
                        std::vector<AudioFrame> frames;
                        audio_convert::pcm16_to_audio_frames(packet.payload.data(), packet.payload.size(), current_channels, frames);
                        
                        // Write to ring buffer
                        if (input_ring_buffer_) {
                            input_ring_buffer_->write(frames.data(), frames.size());
                        }
                    } else if (packet.header.packet_type() == PacketType::Config) {
                        std::string json(packet.payload.begin(), packet.payload.end());
                        auto cfg = AudioConfig::from_json(json);
                        if (cfg && cfg->channels > 0) {
                            current_channels = cfg->channels;
                        }
                        
                        // Send ACK back
                        auto ack = iphone_mic::packet::build_config_ack();
                        send(sock, reinterpret_cast<const char*>(ack.data()),
                             static_cast<int>(ack.size()), 0);
                    } else if (packet.header.packet_type() == PacketType::Heartbeat) {
                        // Respond
                        auto hb = iphone_mic::packet::build_heartbeat();
                        send(sock, reinterpret_cast<const char*>(hb.data()),
                             static_cast<int>(hb.size()), 0);
                    }
                }
            } else if (bytes == 0) {
                break;  // Disconnected
            } else {
                int err = WSAGetLastError();
                if (err == WSAETIMEDOUT) continue;
                break;  // Error
            }
        }
        
        closesocket(sock);
        parser.reset();
        
        // Brief delay before reconnect
        if (usb_client_running_.load()) {
            Sleep(500);
        }
    }
    
    WSACleanup();
}

// ============================================================================
// Private: Timer Thread (drives ASIO callbacks)
// ============================================================================

void iPhoneAsioDriver::start_timer() {
    if (timer_running_.load()) return;
    
    timer_running_.store(true);
    timer_thread_ = std::thread([this]() {
        timer_thread_func();
    });
}

void iPhoneAsioDriver::stop_timer() {
    timer_running_.store(false);
    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }
}

void iPhoneAsioDriver::timer_thread_func() {
    // Use high-resolution timer for accurate buffer callbacks
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    
    // Set multimedia timer resolution to 1ms
    timeBeginPeriod(1);
    
    // Calculate callback interval based on buffer size and sample rate
    double interval_ms = (static_cast<double>(buffer_size_) / sample_rate_) * 1000.0;
    
    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    
    int64_t callback_count = 0;
    
    while (timer_running_.load() && running_) {
        callback_count++;
        
        // Calculate next callback time
        double target_time_ms = callback_count * interval_ms;
        
        // Process audio
        process_audio_buffer();
        
        // Precise wait until next callback time
        while (true) {
            QueryPerformanceCounter(&now);
            double elapsed_ms = static_cast<double>(now.QuadPart - start.QuadPart) 
                               / static_cast<double>(freq.QuadPart) * 1000.0;
            
            double remaining = target_time_ms - elapsed_ms;
            
            if (remaining <= 0) break;
            
            if (remaining > 2.0) {
                Sleep(1);
            } else {
                // Spin-wait for precision
                YieldProcessor();
            }
        }
    }
    
    timeEndPeriod(1);
}

// ============================================================================
// Private: Audio Processing
// ============================================================================

void iPhoneAsioDriver::process_audio_buffer() {
    if (!callbacks_ || !running_) return;
    
    long buffer_index = current_buffer_index_;
    size_t frames_needed = static_cast<size_t>(buffer_size_);
    
    // ==========================================
    // 1. Process INPUT (iPhone -> DAW) with ASRC
    // ==========================================
    
    // Update ASRC ratio based on ring buffer fill level
    if (input_ring_buffer_) {
        double fill = input_ring_buffer_->fill_ratio();
        resampler_.update_ratio(fill, 0.5);
    }
    
    // Read more frames than needed from ring buffer (resampler may consume more or fewer)
    // We peek up to frames_needed + 2 extra to give the interpolator room
    size_t read_count = frames_needed + 2;
    std::vector<AudioFrame> raw_input(read_count, {0.0f, 0.0f});
    size_t frames_available = 0;
    
    if (input_ring_buffer_) {
        // Peek first to see how much is available, then read what we need without consuming
        frames_available = input_ring_buffer_->available_read();
        if (frames_available > read_count) frames_available = read_count;
        frames_available = input_ring_buffer_->peek(raw_input.data(), frames_available);
    }
    
    // Periodically reload DSP settings from registry (~every 100ms)
    static int check_dsp_counter = 0;
    if (++check_dsp_counter >= 32) {
        check_dsp_counter = 0;
        load_dsp_settings_from_registry();
    }

    // Run ASRC: produce exactly frames_needed output frames
    std::vector<AudioFrame> resampled(frames_needed, {0.0f, 0.0f});
    size_t frames_consumed = 0;
    if (frames_available > 0) {
        frames_consumed = resampler_.process(raw_input.data(), frames_available,
                                             resampled.data(), frames_needed);
        // Apply Audio DSP Pipeline (80Hz HPF Low-Cut, Noise Gate, Gain, AGC / Limiter)
        dsp_.process(resampled.data(), frames_needed);
    } else {
        // Output silence if no data
        std::memset(resampled.data(), 0, frames_needed * sizeof(AudioFrame));
    }
    
    // Now consume EXACTLY the number of frames the resampler used
    if (input_ring_buffer_ && frames_consumed > 0) {
        input_ring_buffer_->advance_read(frames_consumed);
    }
    
    // De-interleave into per-channel ASIO buffers (Int32 format)
    for (long ch_idx = 0; ch_idx < num_active_channels_; ++ch_idx) {
        auto& cb = channel_buffers_[ch_idx];
        if (!cb.is_input) continue; // Only process input channels here
        
        int32_t* dst = reinterpret_cast<int32_t*>(
            buffer_index == 0 ? cb.buffer_a.data() : cb.buffer_b.data()
        );
        
        long src_channel = cb.channel_num;
        
        if (src_channel == 0) { // Left
            for (long s = 0; s < buffer_size_; ++s) {
                dst[s] = static_cast<int32_t>(resampled[s].left * 2147483520.0f);
            }
        } else if (src_channel == 1) { // Right
            for (long s = 0; s < buffer_size_; ++s) {
                dst[s] = static_cast<int32_t>(resampled[s].right * 2147483520.0f);
            }
        } else {
            std::memset(dst, 0, frames_needed * sizeof(int32_t));
        }
    }
    
    // ==========================================
    // 2. Call host's buffer switch callback
    // ==========================================
    
    // Update sample position
    sample_position_.fetch_add(buffer_size_);
    
    if (callbacks_->bufferSwitchTimeInfo) {
        ASIOTime time{};
        uint64_t pos = sample_position_.load();
        time.timeInfo.samplePosition.hi = static_cast<unsigned long>(pos >> 32);
        time.timeInfo.samplePosition.lo = static_cast<unsigned long>(pos & 0xFFFFFFFF);
        time.timeInfo.sampleRate = sample_rate_;
        time.timeInfo.flags = 0x03;  // kSystemTimeValid | kSamplePositionValid
        
        LARGE_INTEGER freq, count;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&count);
        uint64_t sysTimeNs = static_cast<uint64_t>(
            static_cast<double>(count.QuadPart) / 
            static_cast<double>(freq.QuadPart) * 1e9
        );
        time.timeInfo.systemTime.hi = static_cast<unsigned long>(sysTimeNs >> 32);
        time.timeInfo.systemTime.lo = static_cast<unsigned long>(sysTimeNs & 0xFFFFFFFF);
        
        callbacks_->bufferSwitchTimeInfo(&time, buffer_index, ASIOTrue);
    } else if (callbacks_->bufferSwitch) {
        callbacks_->bufferSwitch(buffer_index, ASIOTrue);
    }
    
    // ==========================================
    // 3. Process OUTPUT (DAW -> Output Device)
    // ==========================================
    
    std::vector<AudioFrame> output_frames(frames_needed, {0.0f, 0.0f});
    
    bool has_output = false;
    for (long ch_idx = 0; ch_idx < num_active_channels_; ++ch_idx) {
        auto& cb = channel_buffers_[ch_idx];
        if (cb.is_input) continue; // Only process output channels here
        
        has_output = true;
        
        // Studio One ASIO output is int32_t (ASIOSTInt32LSB), not float!
        const int32_t* src = reinterpret_cast<const int32_t*>(
            buffer_index == 0 ? cb.buffer_a.data() : cb.buffer_b.data()
        );
        
        long dst_channel = cb.channel_num;
        
        if (dst_channel == 0) { // Left
            for (long s = 0; s < buffer_size_; ++s) {
                output_frames[s].left = static_cast<float>(src[s]) / 2147483648.0f;
            }
        } else if (dst_channel == 1) { // Right
            for (long s = 0; s < buffer_size_; ++s) {
                output_frames[s].right = static_cast<float>(src[s]) / 2147483648.0f;
            }
        }
    }
    
    if (has_output && output_ring_buffer_) {
        // Write to output ring buffer. If it overflows, it drops frames.
        output_ring_buffer_->write(output_frames.data(), frames_needed);
    }
    
    // Toggle buffer index
    current_buffer_index_ = 1 - buffer_index;
}

void iPhoneAsioDriver::fill_silence(long buffer_index) {
    for (long ch = 0; ch < num_active_channels_; ++ch) {
        auto& cb = channel_buffers_[ch];
        void* buf = (buffer_index == 0) ? cb.buffer_a.data() : cb.buffer_b.data();
        std::memset(buf, 0, static_cast<size_t>(buffer_size_) * sizeof(int32_t));
    }
}

bool iPhoneAsioDriver::is_valid_buffer_size(long size) const {
    for (int i = 0; i < NUM_VALID_BUFFER_SIZES; ++i) {
        if (VALID_BUFFER_SIZES[i] == size) return true;
    }
    return false;
}

void iPhoneAsioDriver::load_dsp_settings_from_registry() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\iPhoneMic", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwVal = 0;
        DWORD dwSize = sizeof(dwVal);

        // GainPercent (0 - 100)
        if (RegQueryValueExA(hKey, "GainPercent", NULL, NULL, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS) {
            dsp_.set_gain_percent(static_cast<int>(dwVal));
        }

        // IsMuted (0 or 1)
        dwSize = sizeof(dwVal);
        if (RegQueryValueExA(hKey, "IsMuted", NULL, NULL, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS) {
            dsp_.set_muted(dwVal != 0);
        }

        // HighPassFilter (0 or 1)
        dwSize = sizeof(dwVal);
        if (RegQueryValueExA(hKey, "HighPassFilter", NULL, NULL, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS) {
            dsp_.set_high_pass_filter(dwVal != 0);
        }

        // AGC (0 or 1)
        dwSize = sizeof(dwVal);
        if (RegQueryValueExA(hKey, "AGC", NULL, NULL, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS) {
            dsp_.set_agc(dwVal != 0);
        }

        // NoiseGate (0, 1, 2, 3)
        dwSize = sizeof(dwVal);
        if (RegQueryValueExA(hKey, "NoiseGate", NULL, NULL, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS) {
            dsp_.set_noise_gate(static_cast<int>(dwVal));
        }

        RegCloseKey(hKey);
    }
}

} // namespace iphone_mic
