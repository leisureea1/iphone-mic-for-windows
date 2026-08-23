// registry_utils.h
// iPhone USB Microphone - Windows
//
// Shared Windows Registry configuration helper functions.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include "audio_dsp.h"

namespace iphone_mic {
namespace registry {

constexpr const char* REG_SUBKEY = "Software\\iPhoneMic";

inline bool save_dword(const char* name, DWORD val) {
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_SUBKEY, 0, NULL, 
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, name, 0, REG_DWORD, 
            reinterpret_cast<const BYTE*>(&val), sizeof(val));
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

inline bool load_dword(const char* name, DWORD& val) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_SUBKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwSize = sizeof(val);
        LSTATUS status = RegQueryValueExA(hKey, name, NULL, NULL, reinterpret_cast<LPBYTE>(&val), &dwSize);
        RegCloseKey(hKey);
        return status == ERROR_SUCCESS;
    }
    return false;
}

inline DWORD get_dword(const char* name, DWORD default_val) {
    DWORD val = default_val;
    if (load_dword(name, val)) return val;
    return default_val;
}

inline bool save_string(const char* name, const std::string& str) {
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_SUBKEY, 0, NULL, 
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, name, 0, REG_SZ, 
            reinterpret_cast<const BYTE*>(str.c_str()), 
            static_cast<DWORD>(str.length() + 1));
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

inline std::string get_string(const char* name, const std::string& default_val = "") {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_SUBKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[512] = {0};
        DWORD bufferSize = sizeof(buffer);
        if (RegQueryValueExA(hKey, name, NULL, NULL, 
            reinterpret_cast<LPBYTE>(buffer), &bufferSize) == ERROR_SUCCESS) {
            buffer[sizeof(buffer) - 1] = '\0';
            RegCloseKey(hKey);
            return std::string(buffer);
        }
        RegCloseKey(hKey);
    }
    return default_val;
}

inline void apply_dsp_settings(AudioDSPPipeline& dsp) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_SUBKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwVal = 0;
        DWORD dwSize = sizeof(dwVal);

        if (RegQueryValueExA(hKey, "GainPercent", NULL, NULL, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS) {
            dsp.set_gain_percent(static_cast<int>(dwVal));
        }

        dwSize = sizeof(dwVal);
        if (RegQueryValueExA(hKey, "IsMuted", NULL, NULL, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS) {
            dsp.set_muted(dwVal != 0);
        }

        dwSize = sizeof(dwVal);
        if (RegQueryValueExA(hKey, "HighPassFilter", NULL, NULL, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS) {
            dsp.set_high_pass_filter(dwVal != 0);
        }

        dwSize = sizeof(dwVal);
        if (RegQueryValueExA(hKey, "AGC", NULL, NULL, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS) {
            dsp.set_agc(dwVal != 0);
        }

        dwSize = sizeof(dwVal);
        if (RegQueryValueExA(hKey, "Limiter", NULL, NULL, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS) {
            dsp.set_limiter(dwVal != 0);
        }

        dwSize = sizeof(dwVal);
        if (RegQueryValueExA(hKey, "NoiseGate", NULL, NULL, reinterpret_cast<LPBYTE>(&dwVal), &dwSize) == ERROR_SUCCESS) {
            dsp.set_noise_gate(static_cast<int>(dwVal));
        }

        RegCloseKey(hKey);
    }
}

} // namespace registry
} // namespace iphone_mic
