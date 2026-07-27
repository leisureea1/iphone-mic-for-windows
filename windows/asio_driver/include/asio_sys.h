// asio_sys.h
// iPhone USB Microphone - Windows ASIO Driver
//
// Minimal ASIO interface definitions compatible with Steinberg ASIO SDK.
// These definitions are based on the publicly documented ASIO specification.
// 
// NOTE: For production use, replace this with the official Steinberg ASIO SDK
// headers downloaded from https://www.steinberg.net/developers/
// The ASIO technology is available under GPLv3 or Steinberg proprietary license.

#pragma once

#include <cstdint>
#include <windows.h>
#include <Unknwn.h>

// ============================================================================
// ASIO Type Definitions
// ============================================================================

typedef long ASIOError;
typedef long ASIOBool;
typedef double ASIOSampleRate;
typedef long long int ASIOSamples;
typedef long long int ASIOTimeStamp;

// ASIO Error Codes
enum {
    ASE_OK               = 0,
    ASE_SUCCESS          = 0x3f4847a0,
    ASE_NotPresent       = -1000,
    ASE_HWMalfunction    = ASE_NotPresent + 1,
    ASE_InvalidParameter = ASE_NotPresent + 2,
    ASE_InvalidMode      = ASE_NotPresent + 3,
    ASE_SPNotAdvancing   = ASE_NotPresent + 4,
    ASE_NoClock          = ASE_NotPresent + 5,
    ASE_NoMemory         = ASE_NotPresent + 6,
};

// ASIO Boolean
enum {
    ASIOFalse = 0,
    ASIOTrue  = 1,
};

// ASIO Sample Types
enum ASIOSampleType {
    ASIOSTInt16MSB   = 0,
    ASIOSTInt24MSB   = 1,
    ASIOSTInt32MSB   = 2,
    ASIOSTFloat32MSB = 3,
    ASIOSTFloat64MSB = 4,
    
    ASIOSTInt16LSB   = 16,
    ASIOSTInt24LSB   = 17,
    ASIOSTInt32LSB   = 18,
    ASIOSTFloat32LSB = 19,
    ASIOSTFloat64LSB = 20,
};

// ============================================================================
// ASIO Structures
// ============================================================================

struct ASIOClockSource {
    long index;
    long associatedChannel;
    long associatedGroup;
    ASIOBool isCurrentSource;
    char name[32];
};

struct ASIOChannelInfo {
    long channel;
    ASIOBool isInput;
    ASIOBool isActive;
    long channelGroup;
    ASIOSampleType type;
    char name[32];
};

struct ASIOBufferInfo {
    ASIOBool isInput;
    long channelNum;
    void* buffers[2];  // double buffer
};

// ASIO Time Info
struct AsioTimeInfo {
    double speed;
    ASIOTimeStamp systemTime;
    ASIOSamples samplePosition;
    ASIOSampleRate sampleRate;
    unsigned long flags;
    char reserved[12];
};

// ASIO Time Code
struct ASIOTimeCode {
    double speed;
    ASIOSamples timeCodeSamples;
    unsigned long flags;
    char future[64];
};

// ASIOTime
struct ASIOTime {
    long reserved[4];
    AsioTimeInfo timeInfo;
    ASIOTimeCode timeCode;
};

// ASIO Callbacks from driver to host
struct ASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, ASIOBool directProcess);
    void (*sampleRateDidChange)(ASIOSampleRate sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* params, long doubleBufferIndex, 
                                      ASIOBool directProcess);
};

// ASIO Message Selectors
enum {
    kAsioSelectorSupported    = 1,
    kAsioEngineVersion        = 2,
    kAsioResetRequest         = 3,
    kAsioBufferSizeChange     = 4,
    kAsioResyncRequest        = 5,
    kAsioLatenciesChanged     = 6,
    kAsioSupportsTimeInfo     = 7,
    kAsioSupportsTimeCode     = 8,
    kAsioMMCCommand           = 9,
    kAsioSupportsInputMonitor = 10,
    kAsioSupportsInputGain    = 11,
    kAsioSupportsInputMeter   = 12,
    kAsioSupportsOutputGain   = 13,
    kAsioSupportsOutputMeter  = 14,
    kAsioOverload             = 15,
};

// ASIO Future Selectors
enum {
    kAsioEnableTimeCodeRead = 1,
    kAsioDisableTimeCodeRead,
    kAsioSetInputMonitor,
    kAsioTransport,
    kAsioSetInputGain,
    kAsioGetInputMeter,
    kAsioSetOutputGain,
    kAsioGetOutputMeter,
    kAsioCanInputMonitor,
    kAsioCanTimeInfo,
    kAsioCanTimeCode,
    kAsioCanTransport,
    kAsioCanInputGain,
    kAsioCanInputMeter,
    kAsioCanOutputGain,
    kAsioCanOutputMeter,
};

// ============================================================================
// IASIO Interface - Pure virtual class that drivers must implement
// ============================================================================

class IASIO : public IUnknown {
public:
    virtual ASIOBool init(void* sysHandle) = 0;
    virtual void getDriverName(char* name) = 0;
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char* string) = 0;
    
    virtual ASIOError start() = 0;
    virtual ASIOError stop() = 0;
    
    virtual ASIOError getChannels(long* numInputChannels, 
                                   long* numOutputChannels) = 0;
    virtual ASIOError getLatencies(long* inputLatency, 
                                    long* outputLatency) = 0;
    virtual ASIOError getBufferSize(long* minSize, long* maxSize,
                                     long* preferredSize, long* granularity) = 0;
    
    virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getSampleRate(ASIOSampleRate* sampleRate) = 0;
    virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) = 0;
    
    virtual ASIOError getClockSources(ASIOClockSource* clocks, 
                                       long* numSources) = 0;
    virtual ASIOError setClockSource(long reference) = 0;
    
    virtual ASIOError getSamplePosition(ASIOSamples* sPos, 
                                         ASIOTimeStamp* tStamp) = 0;
    
    virtual ASIOError getChannelInfo(ASIOChannelInfo* info) = 0;
    
    virtual ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                                     long bufferSize, ASIOCallbacks* callbacks) = 0;
    virtual ASIOError disposeBuffers() = 0;
    
    virtual ASIOError controlPanel() = 0;
    virtual ASIOError future(long selector, void* opt) = 0;
    virtual ASIOError outputReady() = 0;
};
