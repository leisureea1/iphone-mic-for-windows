// audio_format.h
// iPhone USB Microphone - Windows
//
// Audio format definitions and conversion utilities.

#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace iphone_mic {

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

/// Convert a single 24-bit LE sample (3 bytes) to 32-bit signed integer.
/// The 24-bit value is sign-extended and shifted left by 8 to fill the 
/// full 32-bit range, matching ASIO ASIOSTInt32LSB format.
inline int32_t int24_to_int32(const uint8_t* src) {
    // Read 3 bytes as a 24-bit value
    int32_t val = static_cast<int32_t>(src[0])
               | (static_cast<int32_t>(src[1]) << 8)
               | (static_cast<int32_t>(src[2]) << 16);
    
    // Sign-extend from 24-bit to 32-bit
    if (val & 0x800000) {
        val |= static_cast<int32_t>(0xFF000000);
    }
    
    // Shift left 8 bits to use full 32-bit range (for ASIO Int32)
    return val << 8;
}

/// Convert a single 32-bit signed integer to 24-bit LE (3 bytes).
/// Assumes the significant data is in the upper 24 bits.
inline void int32_to_int24(int32_t val, uint8_t* dst) {
    // Shift right 8 to get 24-bit value
    int32_t v24 = val >> 8;
    dst[0] = static_cast<uint8_t>(v24 & 0xFF);
    dst[1] = static_cast<uint8_t>((v24 >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>((v24 >> 16) & 0xFF);
}

/// Convert a single 24-bit LE sample to float32 [-1.0, 1.0)
inline float int24_to_float32(const uint8_t* src) {
    int32_t val = static_cast<int32_t>(src[0])
               | (static_cast<int32_t>(src[1]) << 8)
               | (static_cast<int32_t>(src[2]) << 16);
    
    if (val & 0x800000) {
        val |= static_cast<int32_t>(0xFF000000);
    }
    
    return static_cast<float>(val) / 8388608.0f;  // 2^23
}

/// Convert float32 [-1.0, 1.0] to 24-bit LE (3 bytes)
inline void float32_to_int24(float val, uint8_t* dst) {
    // Clamp
    val = std::clamp(val, -1.0f, 1.0f);
    int32_t i = static_cast<int32_t>(val * 8388607.0f);
    dst[0] = static_cast<uint8_t>(i & 0xFF);
    dst[1] = static_cast<uint8_t>((i >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>((i >> 16) & 0xFF);
}

// ============================================================================
// Bulk Conversion Functions
// ============================================================================

/// Convert a buffer of 24-bit LE samples to 32-bit signed integers.
/// @param src      Source buffer of 24-bit samples (3 bytes each)
/// @param dst      Destination buffer of int32_t values
/// @param samples  Number of samples to convert
inline void convert_int24_to_int32(const uint8_t* src, int32_t* dst, 
                                    size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        dst[i] = int24_to_int32(src + i * 3);
    }
}

/// Convert a buffer of 24-bit LE samples to float32.
/// @param src      Source buffer of 24-bit samples (3 bytes each)
/// @param dst      Destination buffer of float values
/// @param samples  Number of samples to convert
inline void convert_int24_to_float32(const uint8_t* src, float* dst,
                                      size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        dst[i] = int24_to_float32(src + i * 3);
    }
}

/// Convert a buffer of int32 samples to 24-bit LE.
inline void convert_int32_to_int24(const int32_t* src, uint8_t* dst,
                                    size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        int32_to_int24(src[i], dst + i * 3);
    }
}

// ============================================================================
// Level Metering
// ============================================================================

/// Calculate peak and RMS levels from 24-bit PCM data
/// @param src       Source buffer of 24-bit samples
/// @param samples   Number of samples
/// @param peak_out  Output: peak level in dBFS
/// @param rms_out   Output: RMS level in dBFS
inline void calculate_levels_int24(const uint8_t* src, size_t samples,
                                    float& peak_out, float& rms_out) {
    if (samples == 0) {
        peak_out = -160.0f;
        rms_out = -160.0f;
        return;
    }
    
    float peak = 0.0f;
    double sum_sq = 0.0;
    
    for (size_t i = 0; i < samples; ++i) {
        float s = int24_to_float32(src + i * 3);
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
