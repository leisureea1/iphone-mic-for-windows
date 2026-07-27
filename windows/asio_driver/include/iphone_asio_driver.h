// iphone_asio_driver.h
// iPhone USB Microphone - ASIO Driver
//
// Custom ASIO driver that exposes iPhone microphone audio as an ASIO device.
// This is a COM DLL (in-process server) that DAWs load directly.
//
// Device name: "iPhone USB Microphone ASIO"
// Input channels: 1 (mono) or 2 (stereo)
// Output channels: 0 (input-only device)
// Sample rate: 48000 Hz
// Buffer sizes: 64, 128, 256, 512 samples

#pragma once


#include "ring_buffer.h"
#include "protocol.h"
#include "audio_format.h"

#include <memory>
#include <thread>
#include <cstdint>
#include <atomic>

#include <windows.h>
#include <Unknwn.h>

// Official ASIO SDK
#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"

#include <mutex>
#include <string>
#include <vector>

namespace iphone_mic {

// ASIO Driver CLSID - unique GUID for COM registration
// {A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
// IMPORTANT: Generate a new GUID for production use
static const CLSID CLSID_iPhoneAsioDriver = {
    0xA1B2C3D4, 0xE5F6, 0x7890,
    { 0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x78, 0x90 }
};

// Driver name as it appears in DAWs
constexpr const char* DRIVER_NAME = "iPhone USB Microphone ASIO";
constexpr long DRIVER_VERSION = 1;

// Audio parameters
constexpr long NUM_INPUT_CHANNELS  = 2;  // Stereo (can use mono from 1st channel)
constexpr long NUM_OUTPUT_CHANNELS = 0;  // Input only
constexpr ASIOSampleRate SUPPORTED_SAMPLE_RATE = 48000.0;

// Buffer sizes (in samples)
constexpr long MIN_BUFFER_SIZE       = 64;
constexpr long MAX_BUFFER_SIZE       = 512;
constexpr long PREFERRED_BUFFER_SIZE = 256;
constexpr long BUFFER_SIZE_GRANULARITY = 0;  // Only specific sizes

// Valid buffer sizes
constexpr long VALID_BUFFER_SIZES[] = { 64, 128, 256, 512 };
constexpr int NUM_VALID_BUFFER_SIZES = 4;

/// iPhone ASIO Driver Implementation
class iPhoneAsioDriver : public IASIO {
public:
    iPhoneAsioDriver();
    virtual ~iPhoneAsioDriver();
    
    // ================================================================
    // IUnknown
    // ================================================================
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    
    // ================================================================
    // IASIO
    // ================================================================
    ASIOBool init(void* sysHandle) override;
    void getDriverName(char* name) override;
    long getDriverVersion() override;
    void getErrorMessage(char* string) override;
    
    ASIOError start() override;
    ASIOError stop() override;
    
    ASIOError getChannels(long* numInputChannels, long* numOutputChannels) override;
    ASIOError getLatencies(long* inputLatency, long* outputLatency) override;
    ASIOError getBufferSize(long* minSize, long* maxSize,
                            long* preferredSize, long* granularity) override;
    
    ASIOError canSampleRate(ASIOSampleRate sampleRate) override;
    ASIOError getSampleRate(ASIOSampleRate* sampleRate) override;
    ASIOError setSampleRate(ASIOSampleRate sampleRate) override;
    
    ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) override;
    ASIOError setClockSource(long reference) override;
    
    ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) override;
    
    ASIOError getChannelInfo(ASIOChannelInfo* info) override;
    
    ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                            long bufferSize, ASIOCallbacks* callbacks) override;
    ASIOError disposeBuffers() override;
    
    ASIOError controlPanel() override;
    ASIOError future(long selector, void* opt) override;
    ASIOError outputReady() override;
    
private:
    // COM reference count
    long ref_count_ = 1;
    
    // Initialization state
    bool initialized_ = false;
    bool running_ = false;
    HWND sys_handle_ = nullptr;
    
    // Audio configuration
    ASIOSampleRate sample_rate_ = SUPPORTED_SAMPLE_RATE;
    long buffer_size_ = PREFERRED_BUFFER_SIZE;
    
    // ASIO buffers (double-buffered)
    struct ChannelBuffer {
        std::vector<uint8_t> buffer_a;
        std::vector<uint8_t> buffer_b;
        bool is_input;
        long channel_num;
    };
    std::vector<ChannelBuffer> channel_buffers_;
    ASIOBufferInfo* host_buffer_infos_ = nullptr;
    long num_active_channels_ = 0;
    
    // ASIO callbacks (provided by the host/DAW)
    ASIOCallbacks* callbacks_ = nullptr;
    
    // Double buffer index (0 or 1)
    long current_buffer_index_ = 0;
    
    // Sample position counter
    std::atomic<int64_t> sample_position_{0};
    
    // Ring buffer for receiving audio from USB client
    std::shared_ptr<RingBuffer> ring_buffer_;
    
    // USB client thread
    std::thread usb_client_thread_;
    std::atomic<bool> usb_client_running_{false};
    
    // Audio processing timer thread
    std::thread timer_thread_;
    std::atomic<bool> timer_running_{false};
    
    // Temporary buffer for format conversion
    std::vector<uint8_t> temp_pcm_buffer_;
    std::vector<int32_t> temp_int32_buffer_;
    
    // Error message
    char error_message_[256] = {};
    
    // Private methods
    void start_usb_client();
    void stop_usb_client();
    void usb_client_thread_func();
    
    void start_timer();
    void stop_timer();
    void timer_thread_func();
    
    void process_audio_buffer();
    void fill_silence(long buffer_index);
    
    bool is_valid_buffer_size(long size) const;
};

} // namespace iphone_mic
