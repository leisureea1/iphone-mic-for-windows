// dllmain.cpp
// iPhone USB Microphone - ASIO Driver DLL Entry Point

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

// Global module handle used by COM registration
HMODULE g_hModule = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            g_hModule = hModule;
            DisableThreadLibraryCalls(hModule);
            break;
            
        case DLL_PROCESS_DETACH:
            break;
            
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}
