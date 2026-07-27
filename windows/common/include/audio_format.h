// audio_format.h
// iPhone USB Microphone - Windows
//
// Audio format definitions and conversion utilities.

#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

namespace iphone_mic {

// Represents a standard interleaved stereo float audio frame
struct AudioFrame {
    float left;
    float right;
};

// Audio sample format types used internally
enum class SampleFormat {
    Int24LE,     // 24-bit signed integer, little-endian (wire format)
    Int32LE,     // 32-bit signed integer, little-endian (ASIO ASIOSTInt32LSB)
    Float32,     // 32-bit IEEE float (ASIO ASIOSTFloat32LSB)
    Float64,     // 64-bit IEEE float (ASIO ASIOSTFloat64LSB)
};

namespace audio_convert {

// ============================================================================
// 24-bit PCM ↔ 32-bit Integer Conversion
// ============================================================================

/// Convert a single 16-bit LE sample (2 bytes) to 32-bit signed integer.
/// The 16-bit value is sign-extended and shifted left by 16 to fill the 
/// full 32-bit range, matching ASIO ASIOSTInt32LSB format.
inline int32_t int16_to_int32(const uint8_t* src) {
    // Read 2 bytes as a 16-bit value
    int16_t val16 = static_cast<int16_t>(src[0] | (src[1] << 8));
    
    // Shift left 16 bits to use full 32-bit range (for ASIO Int32)
    return static_cast<int32_t>(val16) << 16;
}

/// Convert a single 32-bit signed integer to 16-bit LE (2 bytes).
/// Assumes the significant data is in the upper 16 bits (or 32 bits, we just shift down).
inline void int32_to_int16(int32_t val, uint8_t* dst) {
    // Shift right 16 to get 16-bit value
    int16_t v16 = static_cast<int16_t>(val >> 16);
    dst[0] = static_cast<uint8_t>(v16 & 0xFF);
    dst[1] = static_cast<uint8_t>((v16 >> 8) & 0xFF);
}

/// Convert a single 16-bit LE sample to float32 [-1.0, 1.0)
inline float int16_to_float32(const uint8_t* src) {
    int16_t val = static_cast<int16_t>(src[0] | (src[1] << 8));
    return static_cast<float>(val) / 32768.0f;
}

inline void pcm16_to_audio_frames(const uint8_t* src, size_t num_bytes, int channels, std::vector<AudioFrame>& out_frames) {
    if (channels == 0) return;
    size_t num_samples = num_bytes / 2;
    size_t num_frames = num_samples / channels;
    out_frames.clear();
    out_frames.reserve(num_frames);
    
    const int16_t* pcm16 = reinterpret_cast<const int16_t*>(src);
    for (size_t i = 0; i < num_frames; ++i) {
        AudioFrame frame;
        if (channels == 1) {
            float val = static_cast<float>(pcm16[i]) / 32768.0f;
            frame.left = val;
            frame.right = val;
        } else {
            frame.left = static_cast<float>(pcm16[i * channels]) / 32768.0f;
            frame.right = static_cast<float>(pcm16[i * channels + 1]) / 32768.0f;
        }
        out_frames.push_back(frame);
    }
}

// ============================================================================
// Bulk Conversion Functions
// ============================================================================

/// Convert a buffer of 16-bit LE samples to 32-bit signed integers.
/// @param src      Source buffer of 16-bit samples (2 bytes each)
/// @param dst      Destination buffer of int32_t values
/// @param samples  Number of samples to convert
inline void convert_int16_to_int32(const uint8_t* src, int32_t* dst, 
                                    size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        dst[i] = int16_to_int32(src + i * 2);
    }
}

/// Convert a buffer of 16-bit LE samples to float32.
/// @param src      Source buffer of 16-bit samples (2 bytes each)
/// @param dst      Destination buffer of float values
/// @param samples  Number of samples to convert
inline void convert_int16_to_float32(const uint8_t* src, float* dst,
                                      size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        dst[i] = int16_to_float32(src + i * 2);
    }
}

/// Convert a buffer of int32 samples to 16-bit LE.
inline void convert_int32_to_int16(const int32_t* src, uint8_t* dst,
                                    size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        int32_to_int16(src[i], dst + i * 2);
    }
}

// ============================================================================
// Level Metering
// ============================================================================

/// Calculate peak and RMS levels from 16-bit PCM data
/// @param src       Source buffer of 16-bit samples
/// @param samples   Number of samples
/// @param peak_out  Output: peak level in dBFS
/// @param rms_out   Output: RMS level in dBFS
inline void calculate_levels_int16(const uint8_t* src, size_t samples,
                                    float& peak_out, float& rms_out) {
    if (samples == 0) {
        peak_out = -160.0f;
        rms_out = -160.0f;
        return;
    }
    
    float peak = 0.0f;
    double sum_sq = 0.0;
    
    for (size_t i = 0; i < samples; ++i) {
        float s = int16_to_float32(src + i * 2);
        float abs_s = std::abs(s);
        if (abs_s > peak) peak = abs_s;
        sum_sq += static_cast<double>(s) * static_cast<double>(s);
    }
    
    float rms = static_cast<float>(std::sqrt(sum_sq / samples));
    
    peak_out = (peak > 0.0f) ? 20.0f * std::log10(peak) : -160.0f;
    rms_out  = (rms > 0.0f)  ? 20.0f * std::log10(rms)  : -160.0f;
}

} // namespace audio_convert
} // namespace iphone_mic
