// audio_dsp.h
// iPhone USB Microphone - Studio Grade Realtime Vocal DSP Pipeline v2
//
// High-performance, zero-allocation, realtime thread-safe DSP pipeline:
// 1. 80Hz 2nd-order Butterworth High-Pass Filter (Low-Cut, Q=0.707)
// 2. Downwards Expander / Noise Suppressor (Natural vocal envelope, 2:1 slope)
// 3. Hardware Digital Gain (-12 dB to +12 dB, 50% = 0 dB Unity)
// 4. Vocal Dynamic Compressor (Threshold -18dBFS, Ratio 3:1, Attack 8ms, Release 80ms)
// 5. Lookahead Peak Limiter (1ms delay buffer, Ceiling -1.0 dBFS, Zero-Overshoot)

#pragma once

#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include "audio_format.h"

namespace iphone_mic {

// ============================================================================
// Helper: Time constant coefficient calculator
// ============================================================================
inline float time_to_alpha(float time_seconds, float sample_rate) {
    if (time_seconds <= 0.0f) return 1.0f;
    return 1.0f - std::exp(-1.0f / (time_seconds * sample_rate));
}

// ============================================================================
// 1. 2nd-Order Butterworth High-Pass Filter (80Hz @ fs)
// ============================================================================

class HighPassFilter {
public:
    HighPassFilter(float cutoff_hz = 80.0f, float sample_rate = 48000.0f) {
        set_sample_rate(sample_rate, cutoff_hz);
        reset();
    }

    void reset() {
        x1_l = x2_l = y1_l = y2_l = 0.0f;
        x1_r = x2_r = y1_r = y2_r = 0.0f;
    }

    void set_sample_rate(float sample_rate, float cutoff_hz = 80.0f) {
        sample_rate_ = sample_rate;
        cutoff_hz_ = cutoff_hz;
        
        float w0 = 2.0f * 3.14159265358979323846f * cutoff_hz_ / sample_rate_;
        float cos_w0 = std::cos(w0);
        float sin_w0 = std::sin(w0);
        float alpha = sin_w0 / (2.0f * 0.7071067811865475f); // Q = 0.707

        float a0 = 1.0f + alpha;
        b0 = (1.0f + cos_w0) / 2.0f / a0;
        b1 = -(1.0f + cos_w0) / a0;
        b2 = (1.0f + cos_w0) / 2.0f / a0;
        a1 = (-2.0f * cos_w0) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    inline void process_frame(float& l, float& r) {
        // Direct Form I Filter - Left
        float out_l = b0 * l + b1 * x1_l + b2 * x2_l - a1 * y1_l - a2 * y2_l;
        x2_l = x1_l; x1_l = l;
        y2_l = y1_l; y1_l = out_l;
        l = out_l;

        // Direct Form I Filter - Right
        float out_r = b0 * r + b1 * x1_r + b2 * x2_r - a1 * y1_r - a2 * y2_r;
        x2_r = x1_r; x1_r = r;
        y2_r = y1_r; y1_r = out_r;
        r = out_r;
    }

private:
    float sample_rate_ = 48000.0f;
    float cutoff_hz_ = 80.0f;
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1_l = 0.0f, x2_l = 0.0f, y1_l = 0.0f, y2_l = 0.0f;
    float x1_r = 0.0f, x2_r = 0.0f, y1_r = 0.0f, y2_r = 0.0f;
};

// ============================================================================
// 2. Downwards Noise Expander (Smooth natural vocal decay)
// ============================================================================

enum class NoiseGateLevel {
    Off = 0,
    Low = 1,     // Threshold -42 dBFS (Gentle, for quiet studios)
    Medium = 2,  // Threshold -35 dBFS (Standard, for normal rooms)
    High = 3     // Threshold -28 dBFS (Aggressive, for noisy environments)
};

class DownwardsExpander {
public:
    DownwardsExpander(float sample_rate = 48000.0f) {
        set_sample_rate(sample_rate);
        set_level(NoiseGateLevel::Medium);
        reset();
    }

    void reset() {
        envelope_ = 0.0f;
        gain_smooth_ = 1.0f;
    }

    void set_sample_rate(float sample_rate) {
        sample_rate_ = sample_rate;
        // Attack: 5ms, Release: 80ms
        attack_alpha_ = time_to_alpha(0.005f, sample_rate_);
        release_alpha_ = time_to_alpha(0.080f, sample_rate_);
    }

    void set_level(NoiseGateLevel level) {
        level_ = level;
        switch (level) {
            case NoiseGateLevel::Low:
                threshold_db_ = -42.0f;
                ratio_ = 2.0f;
                floor_gain_ = 0.125f; // -18 dB floor
                break;
            case NoiseGateLevel::Medium:
                threshold_db_ = -35.0f;
                ratio_ = 2.5f;
                floor_gain_ = 0.08f;  // -22 dB floor
                break;
            case NoiseGateLevel::High:
                threshold_db_ = -28.0f;
                ratio_ = 3.0f;
                floor_gain_ = 0.05f;  // -26 dB floor
                break;
            case NoiseGateLevel::Off:
            default:
                threshold_db_ = -100.0f;
                ratio_ = 1.0f;
                floor_gain_ = 1.0f;
                break;
        }
        threshold_linear_ = std::pow(10.0f, threshold_db_ / 20.0f);
    }

    inline void process_frame(float& l, float& r) {
        if (level_ == NoiseGateLevel::Off) return;

        float peak = std::max(std::abs(l), std::abs(r));
        
        // True exponential envelope follower (5ms attack, 80ms release)
        float alpha = (peak > envelope_) ? attack_alpha_ : release_alpha_;
        envelope_ += (peak - envelope_) * alpha;

        // Downwards Expander continuous curve
        float target_gain = 1.0f;
        if (envelope_ < threshold_linear_) {
            if (envelope_ <= 1e-6f) {
                target_gain = floor_gain_;
            } else {
                // Slope expansion below threshold
                float env_db = 20.0f * std::log10(envelope_);
                float diff_db = env_db - threshold_db_; // negative
                float atten_db = diff_db * (ratio_ - 1.0f);
                target_gain = std::pow(10.0f, atten_db / 20.0f);
                target_gain = std::clamp(target_gain, floor_gain_, 1.0f);
            }
        }

        // Smooth gain smoothing filter to prevent clicks
        float smooth_alpha = (target_gain < gain_smooth_) ? release_alpha_ : attack_alpha_;
        gain_smooth_ += (target_gain - gain_smooth_) * smooth_alpha;

        l *= gain_smooth_;
        r *= gain_smooth_;
    }

private:
    float sample_rate_ = 48000.0f;
    NoiseGateLevel level_ = NoiseGateLevel::Medium;
    float threshold_db_ = -35.0f;
    float threshold_linear_ = 0.01778f;
    float ratio_ = 2.5f;
    float floor_gain_ = 0.08f;
    float attack_alpha_ = 0.00416f;
    float release_alpha_ = 0.00026f;
    float envelope_ = 0.0f;
    float gain_smooth_ = 1.0f;
};

// ============================================================================
// 3. Vocal Dynamic Compressor (Leveler)
// ============================================================================

class VocalCompressor {
public:
    VocalCompressor(float sample_rate = 48000.0f) {
        set_sample_rate(sample_rate);
        set_parameters(-18.0f, 3.0f, 0.008f, 0.080f, 2.0f); // -18dBFS, 3:1, 8ms, 80ms, +2dB makeup
        reset();
    }

    void reset() {
        envelope_ = 0.0f;
        gain_reduction_ = 1.0f;
    }

    void set_sample_rate(float sample_rate) {
        sample_rate_ = sample_rate;
        attack_alpha_ = time_to_alpha(0.008f, sample_rate_);
        release_alpha_ = time_to_alpha(0.080f, sample_rate_);
    }

    void set_parameters(float threshold_db, float ratio, float attack_sec, float release_sec, float makeup_gain_db) {
        threshold_db_ = threshold_db;
        threshold_linear_ = std::pow(10.0f, threshold_db_ / 20.0f);
        ratio_ = std::max(1.0f, ratio);
        slope_ = 1.0f - (1.0f / ratio_);
        makeup_linear_ = std::pow(10.0f, makeup_gain_db / 20.0f);
        attack_alpha_ = time_to_alpha(attack_sec, sample_rate_);
        release_alpha_ = time_to_alpha(release_sec, sample_rate_);
    }

    inline void process_frame(float& l, float& r) {
        float peak = std::max(std::abs(l), std::abs(r));
        
        // Envelope detection
        float alpha = (peak > envelope_) ? attack_alpha_ : release_alpha_;
        envelope_ += (peak - envelope_) * alpha;

        float target_gr = 1.0f;
        if (envelope_ > threshold_linear_ && envelope_ > 1e-6f) {
            float env_db = 20.0f * std::log10(envelope_);
            float over_db = env_db - threshold_db_;
            float gr_db = -over_db * slope_;
            target_gr = std::pow(10.0f, gr_db / 20.0f);
        }

        // Smooth gain reduction
        float gr_alpha = (target_gr < gain_reduction_) ? attack_alpha_ : release_alpha_;
        gain_reduction_ += (target_gr - gain_reduction_) * gr_alpha;

        float final_gain = gain_reduction_ * makeup_linear_;
        l *= final_gain;
        r *= final_gain;
    }

private:
    float sample_rate_ = 48000.0f;
    float threshold_db_ = -18.0f;
    float threshold_linear_ = 0.12589f;
    float ratio_ = 3.0f;
    float slope_ = 0.66667f;
    float makeup_linear_ = 1.2589f;
    float attack_alpha_ = 0.0026f;
    float release_alpha_ = 0.00026f;
    float envelope_ = 0.0f;
    float gain_reduction_ = 1.0f;
};

// ============================================================================
// 4. Lookahead Peak Limiter (1ms True-Peak Zero-Overshoot)
// ============================================================================

class LookaheadLimiter {
public:
    static constexpr size_t kMaxDelay = 96; // 2ms @ 48kHz (we use 48 samples = 1ms)

    LookaheadLimiter(float sample_rate = 48000.0f) {
        set_sample_rate(sample_rate);
        reset();
    }

    void reset() {
        std::memset(delay_buf_l_, 0, sizeof(delay_buf_l_));
        std::memset(delay_buf_r_, 0, sizeof(delay_buf_r_));
        buf_pos_ = 0;
        gain_ = 1.0f;
    }

    void set_sample_rate(float sample_rate) {
        sample_rate_ = sample_rate;
        delay_samples_ = std::min(kMaxDelay, static_cast<size_t>(sample_rate * 0.001f)); // 1ms lookahead (48 samples)
        if (delay_samples_ == 0) delay_samples_ = 1;
        
        // Attack: 1ms lookahead window, Release: 80ms
        release_alpha_ = time_to_alpha(0.080f, sample_rate_);
    }

    inline void process_frame(float& l, float& r) {
        constexpr float kCeiling = 0.89125f; // -1.0 dBFS ceiling

        // 1. Detect incoming true peak in current sample
        float in_peak = std::max(std::abs(l), std::abs(r));
        float target_gain = 1.0f;
        if (in_peak > kCeiling) {
            target_gain = kCeiling / in_peak;
        }

        // Fast instantaneous attack if peak exceeds ceiling, otherwise smooth exponential release
        if (target_gain < gain_) {
            gain_ = target_gain;
        } else {
            gain_ += (1.0f - gain_) * release_alpha_;
        }

        // 2. Read delayed audio from lookahead buffer
        float delayed_l = delay_buf_l_[buf_pos_];
        float delayed_r = delay_buf_r_[buf_pos_];

        // 3. Write current input into circular delay buffer
        delay_buf_l_[buf_pos_] = l;
        delay_buf_r_[buf_pos_] = r;
        buf_pos_ = (buf_pos_ + 1) % delay_samples_;

        // 4. Apply calculated lookahead gain reduction to delayed audio
        l = std::clamp(delayed_l * gain_, -kCeiling, kCeiling);
        r = std::clamp(delayed_r * gain_, -kCeiling, kCeiling);
    }

private:
    float sample_rate_ = 48000.0f;
    size_t delay_samples_ = 48; // 1ms
    float delay_buf_l_[kMaxDelay] = {};
    float delay_buf_r_[kMaxDelay] = {};
    size_t buf_pos_ = 0;
    float gain_ = 1.0f;
    float release_alpha_ = 0.00026f;
};

// ============================================================================
// 5. Complete Studio Vocal DSP Pipeline
// ============================================================================

class AudioDSPPipeline {
public:
    AudioDSPPipeline(float sample_rate = 48000.0f)
        : sample_rate_(sample_rate),
          hpf_(80.0f, sample_rate),
          expander_(sample_rate),
          compressor_(sample_rate),
          limiter_(sample_rate)
    {
        set_gain_percent(50); // 50% = 0 dB Unity (CORRECT DEFAULT)
        hpf_enabled_ = true;
        compressor_enabled_ = true;
        limiter_enabled_ = true;
        expander_.set_level(NoiseGateLevel::Medium);
    }

    void reset() {
        hpf_.reset();
        expander_.reset();
        compressor_.reset();
        limiter_.reset();
    }

    void set_sample_rate(float sample_rate) {
        sample_rate_ = sample_rate;
        hpf_.set_sample_rate(sample_rate_, 80.0f);
        expander_.set_sample_rate(sample_rate_);
        compressor_.set_sample_rate(sample_rate_);
        limiter_.set_sample_rate(sample_rate_);
    }

    // Gain Mapping: 0% = -12 dB, 50% = 0 dB (Unity), 100% = +12 dB
    void set_gain_percent(int percent) {
        percent_ = std::clamp(percent, 0, 100);
        float db = (static_cast<float>(percent_) / 100.0f) * 24.0f - 12.0f;
        gain_linear_ = std::pow(10.0f, db / 20.0f);
    }

    void set_gain_db(float db) {
        gain_linear_ = std::pow(10.0f, db / 20.0f);
    }

    int gain_percent() const { return percent_; }
    bool is_muted() const { return is_muted_; }
    bool is_hpf_enabled() const { return hpf_enabled_; }
    bool is_compressor_enabled() const { return compressor_enabled_; }
    bool is_limiter_enabled() const { return limiter_enabled_; }

    void set_muted(bool muted) {
        is_muted_ = muted;
    }

    void set_high_pass_filter(bool enable) {
        hpf_enabled_ = enable;
    }

    void set_agc(bool enable) {
        compressor_enabled_ = enable;
    }

    void set_limiter(bool enable) {
        limiter_enabled_ = enable;
    }

    void set_noise_gate(int level) {
        expander_.set_level(static_cast<NoiseGateLevel>(level));
    }

    void process(AudioFrame* frames, size_t count) {
        if (!frames || count == 0) return;

        if (is_muted_) {
            std::memset(frames, 0, count * sizeof(AudioFrame));
            return;
        }

        for (size_t i = 0; i < count; ++i) {
            float l = frames[i].left;
            float r = frames[i].right;

            // Step 1: 80Hz High-Pass Low-Cut Filter
            if (hpf_enabled_) {
                hpf_.process_frame(l, r);
            }

            // Step 2: Downwards Expander (Noise Suppression)
            expander_.process_frame(l, r);

            // Step 3: Hardware Digital Gain
            l *= gain_linear_;
            r *= gain_linear_;

            // Step 4: Vocal Dynamic Compressor (Leveler)
            if (compressor_enabled_) {
                compressor_.process_frame(l, r);
            }

            // Step 5: 1ms Lookahead True-Peak Limiter (-1.0 dBFS Ceiling)
            if (limiter_enabled_) {
                limiter_.process_frame(l, r);
            }

            frames[i].left = l;
            frames[i].right = r;
        }
    }

private:
    float sample_rate_ = 48000.0f;
    float gain_linear_ = 1.0f;
    int percent_ = 50;
    bool is_muted_ = false;
    bool hpf_enabled_ = true;
    bool compressor_enabled_ = true;
    bool limiter_enabled_ = true;

    HighPassFilter hpf_;
    DownwardsExpander expander_;
    VocalCompressor compressor_;
    LookaheadLimiter limiter_;
};

} // namespace iphone_mic
