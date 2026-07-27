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
#pragma comment(lib, "winmm.lib")

namespace iphone_mic {

// ============================================================================
// Constructor / Destructor
// ============================================================================

iPhoneAsioDriver::iPhoneAsioDriver()
    : ref_count_(1)
    , ring_buffer_(std::make_shared<RingBuffer>(2 * 1024 * 1024))  // 2MB ring buffer
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
    
    // Start the timer that drives buffer callbacks
    start_timer();
    
    running_ = true;
    return ASE_OK;
}

ASIOError iPhoneAsioDriver::stop() {
    if (!running_) return ASE_OK;
    
    running_ = false;
    stop_timer();
    
    return ASE_OK;
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
        
        // We output 32-bit integers (24-bit data shifted to fill 32-bit)
        info->type = ASIOSTInt32LSB;
        info->channelGroup = 0;
        
        if (info->channel == 0) {
            strcpy_s(info->name, "iPhone Mic L");
        } else {
            strcpy_s(info->name, "iPhone Mic R");
        }
    } else {
        // No output channels
        return ASE_InvalidParameter;
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
    if (numChannels <= 0 || numChannels > NUM_INPUT_CHANNELS) 
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
    size_t buffer_bytes = static_cast<size_t>(bufferSize) * sizeof(int32_t);
    
    channel_buffers_.resize(numChannels);
    
    for (long i = 0; i < numChannels; ++i) {
        auto& cb = channel_buffers_[i];
        cb.buffer_a.resize(buffer_bytes, 0);
        cb.buffer_b.resize(buffer_bytes, 0);
        cb.is_input = bufferInfos[i].isInput != 0;
        cb.channel_num = bufferInfos[i].channelNum;
        
        // Set buffer pointers for the host
        bufferInfos[i].buffers[0] = cb.buffer_a.data();
        bufferInfos[i].buffers[1] = cb.buffer_b.data();
    }
    
    // Prepare temp buffers for format conversion
    // Worst case: stereo, 24-bit, max buffer size
    size_t max_pcm_bytes = static_cast<size_t>(bufferSize) * 3 * NUM_INPUT_CHANNELS;
    temp_pcm_buffer_.resize(max_pcm_bytes, 0);
    temp_int32_buffer_.resize(static_cast<size_t>(bufferSize) * NUM_INPUT_CHANNELS, 0);
    
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
    
    temp_pcm_buffer_.clear();
    temp_int32_buffer_.clear();
    
    return ASE_OK;
}

// ============================================================================
// IASIO: Control Panel & Future
// ============================================================================

ASIOError iPhoneAsioDriver::controlPanel() {
    // Show a simple message box with driver info
    if (sys_handle_) {
        MessageBoxA(sys_handle_, 
            "iPhone USB Microphone ASIO v1.0\n\n"
            "1. Connect iPhone via USB\n"
            "2. Start iPhoneMic app on iPhone\n"
            "3. Run: iproxy 8730 8730\n"
            "4. Select this driver in your DAW\n\n"
            "Settings:\n"
            "  Sample Rate: 48000 Hz\n"
            "  Bit Depth: 24-bit\n"
            "  Buffer: 64 / 128 / 256 / 512 samples",
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
        
        while (usb_client_running_.load()) {
            int bytes = recv(sock, reinterpret_cast<char*>(recv_buffer.data()),
                            static_cast<int>(recv_buffer.size()), 0);
            
            if (bytes > 0) {
                parser.feed(recv_buffer.data(), static_cast<size_t>(bytes));
                
                PacketParser::ParsedPacket packet;
                while (parser.try_parse(packet)) {
                    if (packet.header.packet_type() == PacketType::AudioData) {
                        // Write to ring buffer
                        ring_buffer_->write(packet.payload.data(), 
                                          packet.payload.size());
                    } else if (packet.header.packet_type() == PacketType::Config) {
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
    
    // Calculate how many PCM bytes we need from the ring buffer
    // Ring buffer contains 16-bit interleaved PCM (2 bytes per sample per channel)
    // We need buffer_size_ samples × channels × 2 bytes
    size_t channels = static_cast<size_t>(NUM_INPUT_CHANNELS);
    size_t pcm_bytes_needed = static_cast<size_t>(buffer_size_) * 2 * channels;
    
    // Try to read from ring buffer
    size_t bytes_read = ring_buffer_->read(temp_pcm_buffer_.data(), pcm_bytes_needed);
    
    if (bytes_read < pcm_bytes_needed) {
        // Not enough data - fill what we have and zero the rest
        if (bytes_read > 0) {
            std::memset(temp_pcm_buffer_.data() + bytes_read, 0, 
                       pcm_bytes_needed - bytes_read);
        } else {
            // Complete silence
            fill_silence(buffer_index);
            goto callback;
        }
    }
    
    // Convert 16-bit interleaved PCM to 32-bit per-channel ASIO buffers
    {
        size_t total_samples = static_cast<size_t>(buffer_size_) * channels;
        
        // First convert all interleaved samples to int32
        audio_convert::convert_int16_to_int32(temp_pcm_buffer_.data(), 
                                               temp_int32_buffer_.data(), 
                                               total_samples);
        
        // De-interleave into per-channel ASIO buffers
        for (long ch_idx = 0; ch_idx < num_active_channels_; ++ch_idx) {
            auto& cb = channel_buffers_[ch_idx];
            int32_t* dst = reinterpret_cast<int32_t*>(
                buffer_index == 0 ? cb.buffer_a.data() : cb.buffer_b.data()
            );
            
            long src_channel = cb.channel_num;
            
            // Extract this channel from interleaved data
            if (src_channel < static_cast<long>(channels)) {
                for (long s = 0; s < buffer_size_; ++s) {
                    dst[s] = temp_int32_buffer_[s * channels + src_channel];
                }
            } else {
                // Channel not available, fill with silence
                std::memset(dst, 0, static_cast<size_t>(buffer_size_) * sizeof(int32_t));
            }
        }
    }
    
callback:
    // Update sample position
    sample_position_.fetch_add(buffer_size_);
    
    // Toggle buffer index
    current_buffer_index_ = 1 - buffer_index;
    
    // Call host's buffer switch callback
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

} // namespace iphone_mic
