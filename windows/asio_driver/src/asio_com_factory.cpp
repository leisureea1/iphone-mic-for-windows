// asio_com_factory.cpp
// iPhone USB Microphone - ASIO Driver
//
// COM Class Factory for creating iPhoneAsioDriver instances.
// DAWs call DllGetClassObject → IClassFactory::CreateInstance → iPhoneAsioDriver

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <objbase.h>

#include "iphone_asio_driver.h"

using namespace iphone_mic;

// Module handle
extern HMODULE g_hModule;

// ============================================================================
// COM Class Factory
// ============================================================================

class iPhoneAsioClassFactory : public IClassFactory {
public:
    iPhoneAsioClassFactory() : ref_count_(1) {}
    
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&ref_count_);
    }
    
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&ref_count_);
        if (count == 0) delete this;
        return count;
    }
    
    // IClassFactory
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, 
                                             void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        
        // No aggregation support
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        
        // Create the ASIO driver
        auto* driver = new (std::nothrow) iPhoneAsioDriver();
        if (!driver) return E_OUTOFMEMORY;
        
        // Query the requested interface
        HRESULT hr = driver->QueryInterface(riid, ppv);
        driver->Release();  // QueryInterface did AddRef if successful
        
        return hr;
    }
    
    HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) override {
        // Not implemented (single-use driver)
        return S_OK;
    }
    
private:
    long ref_count_;
};

// ============================================================================
// DLL Exports
// ============================================================================

// Global lock count for DllCanUnloadNow
static long g_server_locks = 0;

extern "C" {

/// Called by COM to get the class factory
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    
    // Check if the requested CLSID matches our driver
    if (!IsEqualCLSID(rclsid, CLSID_iPhoneAsioDriver)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    
    // Create and return the class factory
    auto* factory = new (std::nothrow) iPhoneAsioClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    
    return hr;
}

/// Called by COM to check if the DLL can be unloaded
HRESULT WINAPI DllCanUnloadNow() {
    return (g_server_locks == 0) ? S_OK : S_FALSE;
}

} // extern "C"
