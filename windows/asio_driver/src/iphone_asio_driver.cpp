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
#include "registry_utils.h"

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
    , input_ring_buffer_(std::make_shared<RingBuffer<AudioFrame>>(8192))
    , output_ring_buffer_(std::make_shared<RingBuffer<AudioFrame>>(8192))
{
    std::memset(error_message_, 0, sizeof(error_message_));
    usb_audio_frames_.reserve(1024);
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
    
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, CLSID_iPhoneAsioDriver)) {
        *ppv = static_cast<IASIO*>(this);
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
    
    // Cache high-resolution timer frequency
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    qpc_frequency_ = freq.QuadPart;
    
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
    is_prebuffered_ = false;
    consecutive_underruns_ = 0;
    
    // Reset ASRC & DSP state
    resampler_.reset();
    output_resampler_.reset();
    dsp_.reset();
    load_dsp_settings_from_registry();
    update_latency_offset_from_registry();
    
    // Drain any extreme backlog if needed
    if (input_ring_buffer_ && input_ring_buffer_->available_read() > 2048) {
        input_ring_buffer_->drain_to_latest(768);
    }
    if (output_ring_buffer_) {
        output_ring_buffer_->drain_all();
    }
    
    // Fix startup race condition: set running_ before launching threads
    running_ = true;
    
    // Start output playback engine
    start_playback_engine();
    
    // Start the timer that drives buffer callbacks
    start_timer();
    
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
    if (!output_ring_buffer_ || !out || frames == 0) return 0;
    
    size_t available = output_ring_buffer_->available_read();
    size_t target_cushion = static_cast<size_t>(buffer_size_ > 0 ? buffer_size_ * 2 : 256);
    
    // Drain excessive backlog if DAW output accumulates (> 6 buffers)
    if (available > target_cushion * 3) {
        output_ring_buffer_->drain_to_latest(target_cushion);
        available = target_cushion;
    }
    
    if (available >= frames) {
        // Apply output ASRC drift tracking
        output_resampler_.update_drift(available, target_cushion);
        
        size_t max_input_needed = static_cast<size_t>(frames * 1.02) + 4;
        if (work_playback_raw_.size() < max_input_needed) {
            work_playback_raw_.resize(max_input_needed, {0.0f, 0.0f});
        }
        
        size_t peek_count = output_ring_buffer_->peek(work_playback_raw_.data(), max_input_needed);
        size_t consumed = output_resampler_.process(work_playback_raw_.data(), peek_count, out, frames);
        output_ring_buffer_->advance_read(consumed);
        return frames;
    } else if (available > 0) {
        output_ring_buffer_->read(out, available);
        AudioFrame last = out[available - 1];
        size_t missing = frames - available;
        for (size_t i = 0; i < missing; ++i) {
            float fade = 1.0f - static_cast<float>(i + 1) / static_cast<float>(missing);
            out[available + i].left = last.left * fade;
            out[available + i].right = last.right * fade;
        }
        return frames;
    } else {
        std::memset(out, 0, frames * sizeof(AudioFrame));
        return 0;
    }
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
    
    // Total physical input latency calculation:
    // 1. ASIO buffer period: buffer_size_
    // 2. Ring buffer target cushion: max(buffer_size_ * 3, 768)
    // 3. iOS hardware capture buffer (AVAudioEngine IOBuffer @ 48kHz, ~128-256 samples): ~256 samples
    // 4. Lookahead Limiter latency: 48 samples (1ms) if limiter enabled, else 0
    // 5. User offset from registry (RecordOffsetSamples)
    long limiter_delay = dsp_.is_limiter_enabled() ? 48 : 0;
    long ios_capture_delay = 256; // ~5.3ms default on iOS
    size_t target_cushion = std::max<size_t>(static_cast<size_t>(buffer_size_ * 3), static_cast<size_t>(768));
    long base_input_latency = static_cast<long>(buffer_size_ + target_cushion + ios_capture_delay + limiter_delay);
    
    // Add user configured fine-tuning offset (can be positive or negative, clamped to >= buffer_size_)
    long total_in = base_input_latency + record_offset_samples_;
    if (total_in < buffer_size_) total_in = buffer_size_;
    
    *inputLatency = total_in;
    *outputLatency = buffer_size_ * 2;
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
    
    // System time in nanoseconds (using cached QPC frequency)
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    double freq = (qpc_frequency_ > 0) ? static_cast<double>(qpc_frequency_) : 1e7;
    double seconds = static_cast<double>(count.QuadPart) / freq;
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
    
    // Preallocate realtime working buffers to eliminate hot-path heap allocations
    size_t work_count = static_cast<size_t>(bufferSize) + 4;
    work_raw_input_.assign(work_count, {0.0f, 0.0f});
    work_resampled_.assign(static_cast<size_t>(bufferSize), {0.0f, 0.0f});
    work_output_frames_.assign(static_cast<size_t>(bufferSize), {0.0f, 0.0f});
    
    return ASE_OK;
}

ASIOError iPhoneAsioDriver::disposeBuffers() {
    if (running_) {
        stop();
    }
    
    channel_buffers_.clear();
    work_raw_input_.clear();
    work_resampled_.clear();
    work_output_frames_.clear();
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
    // Host has completed output writing for this buffer switch
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
    
    std::vector<uint8_t> payload_buf(65536);
    
    while (usb_client_running_.load()) {
        // Connect directly to iPhone via built-in usbmuxd protocol.
        SOCKET sock = UsbMuxClient::connect_to_device(DEFAULT_PORT);
        if (sock == INVALID_SOCKET) {
            Sleep(1000);
            continue;
        }
        
        // Set receive timeout and TCP optimizations
        DWORD timeout = 500;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        
        int flag = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&flag), sizeof(flag));
        
        int rcvbuf = 65536;
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));
        
        int current_channels = 1;
        
        while (usb_client_running_.load()) {
            PacketHeader header;
            int ret = recv(sock, reinterpret_cast<char*>(&header), sizeof(header), MSG_WAITALL);
            if (ret != sizeof(header)) {
                int err = WSAGetLastError();
                if (err == WSAETIMEDOUT) continue;
                break;
            }
            
            if (header.magic != PROTOCOL_MAGIC) {
                // Out of sync - break to reconnect
                break;
            }
            
            if (header.type == static_cast<uint16_t>(PacketType::AudioData)) {
                if (payload_buf.size() < header.payload_size) {
                    payload_buf.resize(header.payload_size);
                }
                ret = recv(sock, reinterpret_cast<char*>(payload_buf.data()), header.payload_size, MSG_WAITALL);
                if (ret != static_cast<int>(header.payload_size)) {
                    int err = WSAGetLastError();
                    if (err == WSAETIMEDOUT) continue;
                    break;
                }
                
                audio_convert::pcm16_to_audio_frames(payload_buf.data(), header.payload_size, current_channels, usb_audio_frames_);
                if (input_ring_buffer_) {
                    input_ring_buffer_->write(usb_audio_frames_.data(), usb_audio_frames_.size());
                }
            } else if (header.type == static_cast<uint16_t>(PacketType::Config)) {
                if (payload_buf.size() < header.payload_size) {
                    payload_buf.resize(header.payload_size);
                }
                ret = recv(sock, reinterpret_cast<char*>(payload_buf.data()), header.payload_size, MSG_WAITALL);
                if (ret != static_cast<int>(header.payload_size)) {
                    int err = WSAGetLastError();
                    if (err == WSAETIMEDOUT) continue;
                    break;
                }
                
                std::string json(payload_buf.data(), payload_buf.data() + header.payload_size);
                auto cfg = AudioConfig::from_json(json);
                if (cfg && cfg->channels > 0) {
                    current_channels = cfg->channels;
                }
                
                // Send ACK back
                auto ack = iphone_mic::packet::build_config_ack();
                send(sock, reinterpret_cast<const char*>(ack.data()),
                     static_cast<int>(ack.size()), 0);
            } else if (header.type == static_cast<uint16_t>(PacketType::Heartbeat)) {
                auto hb = iphone_mic::packet::build_heartbeat();
                send(sock, reinterpret_cast<const char*>(hb.data()),
                     static_cast<int>(hb.size()), 0);
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
    double freq = (qpc_frequency_ > 0) ? static_cast<double>(qpc_frequency_) : 1e7;
    
    LARGE_INTEGER start, now;
    QueryPerformanceCounter(&start);
    
    int64_t callback_count = 0;
    
    while (timer_running_.load() && running_) {
        callback_count++;
        
        // Calculate next callback time
        double target_time_ms = callback_count * interval_ms;
        
        // Process audio
        process_audio_buffer();
        
        // Precise wait until next callback time
        while (timer_running_.load() && running_) {
            QueryPerformanceCounter(&now);
            double elapsed_ms = static_cast<double>(now.QuadPart - start.QuadPart) 
                               / freq * 1000.0;
            
            double remaining = target_time_ms - elapsed_ms;
            
            if (remaining <= 0.0) break;
            
            if (remaining > 2.0) {
                Sleep(1);
            } else if (remaining > 0.5) {
                SwitchToThread();
            } else {
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
    
    // Ensure work buffers have sufficient capacity
    size_t max_input_needed = static_cast<size_t>(frames_needed * 1.02) + 8;
    if (work_raw_input_.size() < max_input_needed) {
        work_raw_input_.resize(max_input_needed, {0.0f, 0.0f});
    }
    if (work_resampled_.size() < frames_needed) {
        work_resampled_.resize(frames_needed, {0.0f, 0.0f});
    }
    if (work_output_frames_.size() < frames_needed) {
        work_output_frames_.resize(frames_needed, {0.0f, 0.0f});
    }
    
    // Periodically reload DSP & latency settings from registry (~every 100ms)
    if (++check_dsp_counter_ >= 32) {
        check_dsp_counter_ = 0;
        load_dsp_settings_from_registry();
        update_latency_offset_from_registry();
    }

    // ==========================================
    // 1. Process INPUT (iPhone -> RingBuffer -> ASRC -> DSP -> ASIO Buffer)
    // ==========================================
    
    if (input_ring_buffer_) {
        // Target cushion must safely absorb iOS CoreAudio burst deliveries (512 ~ 1024 frames)
        size_t target_cushion = std::max<size_t>(static_cast<size_t>(buffer_size_ * 3), static_cast<size_t>(768));
        size_t max_backlog = target_cushion + 1536;
        
        // High watermark anti-backlog: only drain if severe sustained network backlog (> 48ms)
        if (input_ring_buffer_->available_read() > max_backlog) {
            input_ring_buffer_->drain_to_latest(target_cushion);
        }

        // Initial jitter prebuffering cushion (~16ms cushion before starting playback stream)
        if (!is_prebuffered_) {
            if (input_ring_buffer_->available_read() >= target_cushion) {
                is_prebuffered_ = true;
                consecutive_underruns_ = 0;
            }
        }

        if (is_prebuffered_) {
            size_t available = input_ring_buffer_->available_read();
            
            if (available >= frames_needed) {
                // Adaptive Resampling: dynamically micro-adjust sample consumption to maintain target cushion
                resampler_.update_drift(available, target_cushion);
                
                size_t peek_count = input_ring_buffer_->peek(work_raw_input_.data(), max_input_needed);
                size_t consumed = resampler_.process(work_raw_input_.data(), peek_count, 
                                                    work_resampled_.data(), frames_needed);
                input_ring_buffer_->advance_read(consumed);
                consecutive_underruns_ = 0;
            } else if (available > 0) {
                // Partial read: read available frames, smoothly fade remainder to zero without click
                input_ring_buffer_->read(work_resampled_.data(), available);
                AudioFrame last_frame = work_resampled_[available - 1];
                size_t missing = frames_needed - available;
                for (size_t i = 0; i < missing; ++i) {
                    float fade = 1.0f - static_cast<float>(i + 1) / static_cast<float>(missing);
                    work_resampled_[available + i].left = last_frame.left * fade;
                    work_resampled_[available + i].right = last_frame.right * fade;
                }
                consecutive_underruns_ = 0;
            } else {
                // Temporary underrun: fill current buffer with silence
                std::memset(work_resampled_.data(), 0, frames_needed * sizeof(AudioFrame));
                consecutive_underruns_++;
                if (consecutive_underruns_ > 30) {
                    is_prebuffered_ = false;
                }
            }
        } else {
            std::memset(work_resampled_.data(), 0, frames_needed * sizeof(AudioFrame));
        }
    } else {
        std::memset(work_resampled_.data(), 0, frames_needed * sizeof(AudioFrame));
    }

    // Always run DSP pipeline so biquad filters and dynamic processors remain smooth
    dsp_.process(work_resampled_.data(), frames_needed);
    
    // De-interleave into per-channel ASIO buffers (Int32 format with strict clamping)
    for (long ch_idx = 0; ch_idx < num_active_channels_; ++ch_idx) {
        auto& cb = channel_buffers_[ch_idx];
        if (!cb.is_input) continue; // Only process input channels here
        
        int32_t* dst = reinterpret_cast<int32_t*>(
            buffer_index == 0 ? cb.buffer_a.data() : cb.buffer_b.data()
        );
        
        long src_channel = cb.channel_num;
        
        if (src_channel == 0) { // Left
            for (long s = 0; s < buffer_size_; ++s) {
                float sample = std::clamp(work_resampled_[s].left, -1.0f, 1.0f);
                dst[s] = static_cast<int32_t>(sample * 2147483647.0f);
            }
        } else if (src_channel == 1) { // Right
            for (long s = 0; s < buffer_size_; ++s) {
                float sample = std::clamp(work_resampled_[s].right, -1.0f, 1.0f);
                dst[s] = static_cast<int32_t>(sample * 2147483647.0f);
            }
        } else {
            std::memset(dst, 0, frames_needed * sizeof(int32_t));
        }
    }
    
    // ==========================================
    // 2. Call host's buffer switch callback
    // ==========================================
    
    sample_position_.fetch_add(buffer_size_);
    
    if (callbacks_->bufferSwitchTimeInfo) {
        ASIOTime time{};
        uint64_t pos = sample_position_.load();
        time.timeInfo.speed = 1.0;
        time.timeInfo.sampleRate = sample_rate_;
        time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid | kSpeedValid;
        time.timeInfo.samplePosition.hi = static_cast<unsigned long>(pos >> 32);
        time.timeInfo.samplePosition.lo = static_cast<unsigned long>(pos & 0xFFFFFFFF);
        
        LARGE_INTEGER count;
        QueryPerformanceCounter(&count);
        double freq = (qpc_frequency_ > 0) ? static_cast<double>(qpc_frequency_) : 1e7;
        uint64_t sysTimeNs = static_cast<uint64_t>(
            static_cast<double>(count.QuadPart) / freq * 1e9
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
                work_output_frames_[s].left = static_cast<float>(src[s]) / 2147483648.0f;
            }
        } else if (dst_channel == 1) { // Right
            for (long s = 0; s < buffer_size_; ++s) {
                work_output_frames_[s].right = static_cast<float>(src[s]) / 2147483648.0f;
            }
        }
    }
    
    if (has_output && output_ring_buffer_) {
        output_ring_buffer_->write(work_output_frames_.data(), frames_needed);
    }
    
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
    registry::apply_dsp_settings(dsp_);
}

void iPhoneAsioDriver::update_latency_offset_from_registry() {
    int32_t val = 0;
    if (registry::load_int32("RecordOffsetSamples", val)) {
        record_offset_samples_ = static_cast<long>(val);
    } else {
        record_offset_samples_ = 0;
    }
}

} // namespace iphone_mic
