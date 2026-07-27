// audio_format_test.cpp
// Unit tests for audio format conversion.

#include "audio_format.h"

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <cstring>

using namespace iphone_mic::audio_convert;

void test_int24_to_int32() {
    std::cout << "test_int24_to_int32... ";
    
    // Test positive value: 0x7FFFFF (max 24-bit positive)
    {
        uint8_t src[3] = {0xFF, 0xFF, 0x7F};
        int32_t result = int24_to_int32(src);
        assert(result > 0);
        // Should be 0x7FFFFF << 8 = 0x7FFFFF00
        assert(result == 0x7FFFFF00);
    }
    
    // Test zero
    {
        uint8_t src[3] = {0, 0, 0};
        int32_t result = int24_to_int32(src);
        assert(result == 0);
    }
    
    // Test negative: -1 in 24-bit = 0xFFFFFF
    {
        uint8_t src[3] = {0xFF, 0xFF, 0xFF};
        int32_t result = int24_to_int32(src);
        assert(result == -256);  // -1 << 8 = -256
    }
    
    // Test min negative: 0x800000
    {
        uint8_t src[3] = {0x00, 0x00, 0x80};
        int32_t result = int24_to_int32(src);
        assert(result < 0);
    }
    
    std::cout << "PASSED" << std::endl;
}

void test_int24_to_float32() {
    std::cout << "test_int24_to_float32... ";
    
    // Silence
    {
        uint8_t src[3] = {0, 0, 0};
        float result = int24_to_float32(src);
        assert(result == 0.0f);
    }
    
    // Max positive (~1.0)
    {
        uint8_t src[3] = {0xFF, 0xFF, 0x7F};
        float result = int24_to_float32(src);
        assert(result > 0.999f && result <= 1.0f);
    }
    
    // Max negative (~-1.0)
    {
        uint8_t src[3] = {0x00, 0x00, 0x80};
        float result = int24_to_float32(src);
        assert(result >= -1.0f && result < -0.999f);
    }
    
    // Mid positive (~0.5)
    {
        uint8_t src[3] = {0x00, 0x00, 0x40};
        float result = int24_to_float32(src);
        assert(std::abs(result - 0.5f) < 0.01f);
    }
    
    std::cout << "PASSED" << std::endl;
}

void test_float32_to_int24_roundtrip() {
    std::cout << "test_float32_to_int24_roundtrip... ";
    
    // Test several values
    float test_values[] = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 0.001f, -0.001f};
    
    for (float original : test_values) {
        uint8_t int24[3];
        float32_to_int24(original, int24);
        float recovered = int24_to_float32(int24);
        
        // Should be within 24-bit precision
        float error = std::abs(original - recovered);
        assert(error < 0.0001f);
    }
    
    std::cout << "PASSED" << std::endl;
}

void test_bulk_conversion() {
    std::cout << "test_bulk_conversion... ";
    
    constexpr size_t SAMPLES = 256;
    
    // Generate 24-bit test data (ascending pattern)
    std::vector<uint8_t> src(SAMPLES * 3);
    for (size_t i = 0; i < SAMPLES; i++) {
        int32_t val = static_cast<int32_t>(i * 100) - 12800;  // Centered around 0
        src[i * 3]     = static_cast<uint8_t>(val & 0xFF);
        src[i * 3 + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
        src[i * 3 + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    }
    
    // Convert to int32
    std::vector<int32_t> int32_buf(SAMPLES);
    convert_int24_to_int32(src.data(), int32_buf.data(), SAMPLES);
    
    // Convert to float
    std::vector<float> float_buf(SAMPLES);
    convert_int24_to_float32(src.data(), float_buf.data(), SAMPLES);
    
    // Verify int32 values are scaled correctly
    assert(int32_buf[0] != 0 || src[0] == 0);
    
    // Convert int32 back to int24
    std::vector<uint8_t> roundtrip(SAMPLES * 3);
    convert_int32_to_int24(int32_buf.data(), roundtrip.data(), SAMPLES);
    
    // Verify roundtrip
    assert(std::memcmp(src.data(), roundtrip.data(), SAMPLES * 3) == 0);
    
    std::cout << "PASSED (" << SAMPLES << " samples converted)" << std::endl;
}

void test_level_metering() {
    std::cout << "test_level_metering... ";
    
    // Test with silence
    {
        std::vector<uint8_t> silence(768, 0);
        float peak, rms;
        calculate_levels_int24(silence.data(), 256, peak, rms);
        assert(peak == -160.0f);
        assert(rms == -160.0f);
    }
    
    // Test with full-scale signal
    {
        constexpr size_t SAMPLES = 100;
        std::vector<uint8_t> fullscale(SAMPLES * 3);
        for (size_t i = 0; i < SAMPLES; i++) {
            // Max positive: 0x7FFFFF
            fullscale[i * 3]     = 0xFF;
            fullscale[i * 3 + 1] = 0xFF;
            fullscale[i * 3 + 2] = 0x7F;
        }
        
        float peak, rms;
        calculate_levels_int24(fullscale.data(), SAMPLES, peak, rms);
        
        // Peak should be ~0 dBFS
        assert(peak > -0.5f);
        // RMS of DC full-scale should also be ~0 dBFS
        assert(rms > -0.5f);
    }
    
    std::cout << "PASSED" << std::endl;
}

void test_clipping() {
    std::cout << "test_clipping... ";
    
    // Test that float32_to_int24 clips values > 1.0
    {
        uint8_t out[3];
        float32_to_int24(2.0f, out);
        float result = int24_to_float32(out);
        assert(result <= 1.0f);
        assert(result > 0.99f);
    }
    
    // Test that float32_to_int24 clips values < -1.0
    {
        uint8_t out[3];
        float32_to_int24(-2.0f, out);
        float result = int24_to_float32(out);
        assert(result >= -1.0f);
        assert(result < -0.99f);
    }
    
    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "Audio Format Tests" << std::endl;
    std::cout << "==================" << std::endl;
    
    test_int24_to_int32();
    test_int24_to_float32();
    test_float32_to_int24_roundtrip();
    test_bulk_conversion();
    test_level_metering();
    test_clipping();
    
    std::cout << "\nAll audio format tests PASSED!" << std::endl;
    return 0;
}
