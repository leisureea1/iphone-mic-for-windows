// audio_format_test.cpp
// Unit tests for audio format conversion and resampler.

#include "audio_format.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>

using namespace iphone_mic;

void test_int16_to_int32_roundtrip() {
    int16_t samples[] = {-32768, -16384, 0, 16383, 32767};
    for (int16_t s : samples) {
        uint8_t bytes[2];
        bytes[0] = static_cast<uint8_t>(s & 0xFF);
        bytes[1] = static_cast<uint8_t>((s >> 8) & 0xFF);
        
        int32_t i32 = audio_convert::int16_to_int32(bytes);
        assert((i32 >> 16) == s);
        
        uint8_t out_bytes[2];
        audio_convert::int32_to_int16(i32, out_bytes);
        assert(out_bytes[0] == bytes[0]);
        assert(out_bytes[1] == bytes[1]);
    }
    std::cout << "  test_int16_to_int32_roundtrip: PASSED\n";
}

void test_int16_to_float32() {
    int16_t max_val = 32767;
    uint8_t bytes_max[2] = {static_cast<uint8_t>(max_val & 0xFF), static_cast<uint8_t>((max_val >> 8) & 0xFF)};
    float f_max = audio_convert::int16_to_float32(bytes_max);
    assert(f_max > 0.999f && f_max <= 1.0f);

    int16_t min_val = -32768;
    uint8_t bytes_min[2] = {static_cast<uint8_t>(min_val & 0xFF), static_cast<uint8_t>((min_val >> 8) & 0xFF)};
    float f_min = audio_convert::int16_to_float32(bytes_min);
    assert(f_min == -1.0f);

    uint8_t bytes_zero[2] = {0, 0};
    float f_zero = audio_convert::int16_to_float32(bytes_zero);
    assert(f_zero == 0.0f);
    
    std::cout << "  test_int16_to_float32: PASSED\n";
}

void test_pcm16_to_audio_frames() {
    // Mono conversion
    int16_t mono_data[4] = {0, 16384, -16384, 32767};
    std::vector<AudioFrame> frames;
    audio_convert::pcm16_to_audio_frames(reinterpret_cast<const uint8_t*>(mono_data), sizeof(mono_data), 1, frames);
    assert(frames.size() == 4);
    assert(std::abs(frames[1].left - 0.5f) < 0.01f);
    assert(frames[1].left == frames[1].right); // Mono duplicates to stereo

    // Stereo conversion
    int16_t stereo_data[4] = {16384, -16384, 0, 32767};
    audio_convert::pcm16_to_audio_frames(reinterpret_cast<const uint8_t*>(stereo_data), sizeof(stereo_data), 2, frames);
    assert(frames.size() == 2);
    assert(std::abs(frames[0].left - 0.5f) < 0.01f);
    assert(std::abs(frames[0].right - (-0.5f)) < 0.01f);

    std::cout << "  test_pcm16_to_audio_frames: PASSED\n";
}

void test_calculate_levels() {
    float peak = 0.0f, rms = 0.0f;
    
    // Silence
    std::vector<uint8_t> silence(100, 0);
    audio_convert::calculate_levels_int16(silence.data(), 50, peak, rms);
    assert(peak <= -100.0f);
    assert(rms <= -100.0f);

    std::cout << "  test_calculate_levels: PASSED\n";
}

void test_adaptive_resampler() {
    AdaptiveResampler resampler;
    resampler.reset();

    std::vector<AudioFrame> input(256, {0.5f, -0.5f});
    std::vector<AudioFrame> output(256, {0.0f, 0.0f});

    size_t consumed = resampler.process(input.data(), input.size(), output.data(), output.size());
    assert(consumed > 0);
    assert(output[0].left == 0.5f);
    assert(output[0].right == -0.5f);

    // Test ratio update
    resampler.update_ratio(0.6, 0.5);
    assert(resampler.current_ratio() > 1.0);

    std::cout << "  test_adaptive_resampler: PASSED\n";
}

int main() {
    std::cout << "Audio Format Tests\n";
    std::cout << "==================\n";
    
    test_int16_to_int32_roundtrip();
    test_int16_to_float32();
    test_pcm16_to_audio_frames();
    test_calculate_levels();
    test_adaptive_resampler();

    std::cout << "\nAll audio format tests PASSED!\n";
    return 0;
}
