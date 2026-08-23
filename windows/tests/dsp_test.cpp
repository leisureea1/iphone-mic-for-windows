// dsp_test.cpp
// Unit tests for the Studio Grade Vocal DSP Pipeline.

#include "audio_dsp.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>

using namespace iphone_mic;

void test_high_pass_filter() {
    HighPassFilter hpf(80.0f, 48000.0f);
    hpf.reset();

    // DC signal (0 Hz): should be attenuated to near zero
    float l = 1.0f, r = 1.0f;
    for (int i = 0; i < 2000; ++i) {
        l = 1.0f; r = 1.0f;
        hpf.process_frame(l, r);
    }
    assert(std::abs(l) < 0.01f);
    assert(std::abs(r) < 0.01f);

    // High frequency signal (1000 Hz): should pass with minimal attenuation
    hpf.reset();
    float max_pass = 0.0f;
    for (int i = 0; i < 480; ++i) {
        float sample = std::sin(2.0f * 3.14159265f * 1000.0f * i / 48000.0f);
        l = sample; r = sample;
        hpf.process_frame(l, r);
        if (i > 100) {
            max_pass = std::max(max_pass, std::abs(l));
        }
    }
    assert(max_pass > 0.95f);

    std::cout << "  test_high_pass_filter: PASSED\n";
}

void test_downwards_expander() {
    DownwardsExpander expander(48000.0f);
    expander.reset();
    expander.set_level(NoiseGateLevel::Medium); // Threshold -35 dBFS (~0.0177 linear)

    // Very quiet signal below threshold (e.g. 0.001 linear ~ -60 dBFS)
    float l = 0.001f, r = 0.001f;
    for (int i = 0; i < 5000; ++i) {
        l = 0.001f; r = 0.001f;
        expander.process_frame(l, r);
    }
    // Should be expanded downwards (attenuated further)
    assert(l < 0.0005f);

    // Normal speaking voice (0.2 linear ~ -14 dBFS)
    expander.reset();
    for (int i = 0; i < 5000; ++i) {
        l = 0.2f; r = 0.2f;
        expander.process_frame(l, r);
    }
    assert(std::abs(l - 0.2f) < 0.02f);

    std::cout << "  test_downwards_expander: PASSED\n";
}

void test_vocal_compressor() {
    VocalCompressor comp(48000.0f);
    comp.reset();
    // Default: Threshold -18 dBFS, Ratio 3:1, +2dB makeup

    // Loud signal (0.8 linear ~ -1.9 dBFS)
    float l = 0.8f, r = 0.8f;
    for (int i = 0; i < 5000; ++i) {
        l = 0.8f; r = 0.8f;
        comp.process_frame(l, r);
    }
    // With 3:1 compression, output should be significantly reduced compared to linear + makeup
    assert(l < 0.7f);

    std::cout << "  test_vocal_compressor: PASSED\n";
}

void test_lookahead_limiter() {
    LookaheadLimiter limiter(48000.0f);
    limiter.reset();

    // Feed a massive peak (+6 dBFS = 2.0 linear)
    for (int i = 0; i < 200; ++i) {
        float l = 2.0f;
        float r = 2.0f;
        limiter.process_frame(l, r);
        // Lookahead limiter must NEVER overshoot ceiling (0.89125f = -1.0 dBFS)
        assert(std::abs(l) <= 0.8913f);
        assert(std::abs(r) <= 0.8913f);
    }

    std::cout << "  test_lookahead_limiter: PASSED\n";
}

void test_dsp_pipeline_toggles() {
    AudioDSPPipeline pipeline(48000.0f);
    pipeline.reset();

    // Verify default states
    assert(pipeline.is_hpf_enabled() == true);
    assert(pipeline.is_compressor_enabled() == true);
    assert(pipeline.is_limiter_enabled() == true);
    assert(pipeline.is_muted() == false);
    assert(pipeline.gain_percent() == 50);

    // Verify set_limiter isolates from compressor
    pipeline.set_limiter(false);
    assert(pipeline.is_limiter_enabled() == false);
    assert(pipeline.is_compressor_enabled() == true); // Compressor must remain true!

    pipeline.set_agc(false);
    assert(pipeline.is_compressor_enabled() == false);
    assert(pipeline.is_limiter_enabled() == false);

    pipeline.set_limiter(true);
    assert(pipeline.is_limiter_enabled() == true);
    assert(pipeline.is_compressor_enabled() == false); // Compressor must remain false!

    // Verify mute
    pipeline.set_muted(true);
    std::vector<AudioFrame> frames(64, {0.5f, 0.5f});
    pipeline.process(frames.data(), frames.size());
    for (const auto& f : frames) {
        assert(f.left == 0.0f && f.right == 0.0f);
    }

    std::cout << "  test_dsp_pipeline_toggles: PASSED\n";
}

int main() {
    std::cout << "DSP Pipeline Tests\n";
    std::cout << "==================\n";

    test_high_pass_filter();
    test_downwards_expander();
    test_vocal_compressor();
    test_lookahead_limiter();
    test_dsp_pipeline_toggles();

    std::cout << "\nAll DSP pipeline tests PASSED!\n";
    return 0;
}
