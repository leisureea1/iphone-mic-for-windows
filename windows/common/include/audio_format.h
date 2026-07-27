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

// ============================================================================
// Adaptive Linear Resampler for Clock Drift Compensation (ASRC)
// ============================================================================
//
// iPhone ADC clock and Windows DAC clock drift by ~0.2 samples/sec.
// Over hours this causes ring buffer overflow/starvation and audible glitches.
//
// This resampler uses linear interpolation with an adaptive ratio that tracks
// the ring buffer fill level. When the buffer starts filling up (iPhone faster),
// ratio > 1.0 consumes more input. When depleting (iPhone slower), ratio < 1.0.
//
// The adjustment is extremely gentle (max ±20 ppm) so it's inaudible.

class AdaptiveResampler {
public:
    AdaptiveResampler() = default;
    
    /// Reset resampler state (call when stream restarts)
    void reset() {
        phase_ = 0.0;
        prev_frame_ = {0.0f, 0.0f};
        ratio_ = 1.0;
        ratio_smoothed_ = 1.0;
    }
    
    /// Update the resample ratio based on ring buffer fill level.
    /// Call this once per ASIO callback period.
    ///
    /// @param fill_ratio  Current ring buffer fill level (0.0 = empty, 1.0 = full)
    /// @param target      Target fill ratio (typically 0.5)
    void update_ratio(double fill_ratio, double target = 0.5) {
        // Error: positive means buffer is overfull (iPhone faster)
        double error = fill_ratio - target;
        
        // Proportional control with very gentle gain
        // Max correction: ±20 ppm (±0.00002)
        // This means at most 0.96 samples/sec drift correction at 48kHz
        constexpr double kP = 0.00004;  // proportional gain
        constexpr double kMaxCorrection = 0.00002;  // ±20 ppm
        
        double correction = error * kP;
        correction = (correction > kMaxCorrection) ? kMaxCorrection :
                     (correction < -kMaxCorrection) ? -kMaxCorrection : correction;
        
        ratio_ = 1.0 + correction;
        
        // Smooth the ratio to avoid sudden jumps
        constexpr double kSmoothing = 0.001;  // very slow smoothing
        ratio_smoothed_ += (ratio_ - ratio_smoothed_) * kSmoothing;
    }
    
    /// Resample input frames to output frames using linear interpolation.
    /// The number of output frames is fixed (= ASIO buffer size).
    /// The number of input frames consumed varies based on the current ratio.
    ///
    /// @param input        Input audio frames
    /// @param input_count  Number of available input frames
    /// @param output       Output buffer (pre-allocated)
    /// @param output_count Number of output frames to produce
    /// @return Number of input frames actually consumed
    size_t process(const AudioFrame* input, size_t input_count,
                   AudioFrame* output, size_t output_count) {
        size_t in_idx = 0;
        
        for (size_t out = 0; out < output_count; ++out) {
            // Integer and fractional parts of the current phase
            size_t idx0 = static_cast<size_t>(phase_);
            double frac = phase_ - static_cast<double>(idx0);
            
            // Adjust idx0 relative to current input position
            size_t local_idx = idx0 - consumed_total_;
            
            if (local_idx + 1 < input_count) {
                // Normal case: interpolate between two input samples
                float frac_f = static_cast<float>(frac);
                output[out].left  = input[local_idx].left  * (1.0f - frac_f) + input[local_idx + 1].left  * frac_f;
                output[out].right = input[local_idx].right * (1.0f - frac_f) + input[local_idx + 1].right * frac_f;
            } else if (local_idx < input_count) {
                // Edge case: use last sample and previous frame
                float frac_f = static_cast<float>(frac);
                output[out].left  = prev_frame_.left  * (1.0f - frac_f) + input[local_idx].left  * frac_f;
                output[out].right = prev_frame_.right * (1.0f - frac_f) + input[local_idx].right * frac_f;
            } else {
                // Ran out of input - output silence for remaining
                output[out] = {0.0f, 0.0f};
            }
            
            phase_ += ratio_smoothed_;
        }
        
        // Calculate how many input frames were consumed
        size_t new_consumed = static_cast<size_t>(phase_);
        size_t frames_consumed = new_consumed - consumed_total_;
        
        // Clamp to available input
        if (frames_consumed > input_count) {
            frames_consumed = input_count;
        }
        
        // Save last frame for next call's interpolation
        if (input_count > 0) {
            prev_frame_ = input[input_count - 1];
        }
        
        // Reset phase to avoid precision loss over time
        consumed_total_ += frames_consumed;
        
        // Periodically reset to prevent floating point drift
        if (consumed_total_ > 1000000) {
            phase_ -= static_cast<double>(consumed_total_);
            consumed_total_ = 0;
        }
        
        return frames_consumed;
    }
    
    /// Get current smoothed ratio (for diagnostics)
    double current_ratio() const { return ratio_smoothed_; }

private:
    double phase_ = 0.0;           // Current fractional read position
    double ratio_ = 1.0;           // Target resample ratio
    double ratio_smoothed_ = 1.0;  // Smoothed ratio (actually used)
    size_t consumed_total_ = 0;    // Total input frames consumed (for phase tracking)
    AudioFrame prev_frame_ = {0.0f, 0.0f};  // Last frame from previous call
};

} // namespace iphone_mic
