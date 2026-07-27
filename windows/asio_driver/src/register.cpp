// register.cpp
// iPhone USB Microphone - ASIO Driver
//
// COM server self-registration (DllRegisterServer / DllUnregisterServer).
// Creates registry entries so DAWs can discover the ASIO driver.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <objbase.h>
#include <string>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#include "iphone_asio_driver.h"

using namespace iphone_mic;

extern HMODULE g_hModule;

// CLSID as string
static const char* CLSID_STR = "{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}";

// Registry paths
static const char* ASIO_REG_PATH = "SOFTWARE\\ASIO\\iPhone USB Microphone ASIO";
static const char* CLSID_REG_PATH = "CLSID\\{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}";
static const char* INPROC_REG_PATH = "CLSID\\{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}\\InprocServer32";

/// Helper: create a registry key with a string value
static bool set_reg_value(HKEY root, const char* path, const char* name, 
                           const char* value) {
    HKEY hKey;
    LONG result = RegCreateKeyExA(root, path, 0, NULL, REG_OPTION_NON_VOLATILE,
                                   KEY_WRITE, NULL, &hKey, NULL);
    if (result != ERROR_SUCCESS) return false;
    
    result = RegSetValueExA(hKey, name, 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(value),
                             static_cast<DWORD>(strlen(value) + 1));
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

/// Helper: delete a registry key tree
static void delete_reg_key(HKEY root, const char* path) {
    SHDeleteKeyA(root, path);
}

extern "C" {

/// Register the ASIO driver in the Windows registry
/// Called by: regsvr32 iphone_asio_driver.dll
HRESULT WINAPI DllRegisterServer() {
    // Get DLL path
    char dll_path[MAX_PATH];
    GetModuleFileNameA(g_hModule, dll_path, MAX_PATH);
    
    // ================================================================
    // 1. Register under HKLM\SOFTWARE\ASIO
    //    This is how DAWs discover ASIO drivers
    // ================================================================
    if (!set_reg_value(HKEY_LOCAL_MACHINE, ASIO_REG_PATH, "CLSID", CLSID_STR)) {
        // Try HKCU if HKLM fails (no admin rights)
        set_reg_value(HKEY_CURRENT_USER, ASIO_REG_PATH, "CLSID", CLSID_STR);
    }
    
    // Also set Description
    set_reg_value(HKEY_LOCAL_MACHINE, ASIO_REG_PATH, "Description",
                  "iPhone USB Microphone ASIO Driver");
    
    // ================================================================
    // 2. Register COM class under HKCR\CLSID
    //    This is standard COM registration
    // ================================================================
    
    // CLSID key with default value = driver name
    set_reg_value(HKEY_CLASSES_ROOT, CLSID_REG_PATH, NULL, DRIVER_NAME);
    
    // InprocServer32 = DLL path
    set_reg_value(HKEY_CLASSES_ROOT, INPROC_REG_PATH, NULL, dll_path);
    
    // Threading model
    set_reg_value(HKEY_CLASSES_ROOT, INPROC_REG_PATH, "ThreadingModel", "Apartment");
    
    return S_OK;
}

/// Unregister the ASIO driver
/// Called by: regsvr32 /u iphone_asio_driver.dll
HRESULT WINAPI DllUnregisterServer() {
    // Remove ASIO registration
    delete_reg_key(HKEY_LOCAL_MACHINE, ASIO_REG_PATH);
    delete_reg_key(HKEY_CURRENT_USER, ASIO_REG_PATH);
    
    // Remove COM registration
    delete_reg_key(HKEY_CLASSES_ROOT, CLSID_REG_PATH);
    
    return S_OK;
}

} // extern "C"
