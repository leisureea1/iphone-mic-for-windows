// AudioCaptureEngine.swift
// iPhoneMic
//
// Studio-Grade CoreAudio RemoteIO (AudioUnit) hardware capture engine.
// Direct access to hardware ADC with 128~256 sample real-time callbacks (~2.6ms - 5.3ms ultra low latency).

import Foundation
import AudioToolbox
import AVFoundation
import Accelerate

/// Callback type for delivering captured PCM audio data and levels (peak, rms in dBFS)
typealias AudioDataCallback = @Sendable (Data, Float, Float) -> Void

/// Errors that can occur during audio capture
enum AudioCaptureError: Error, LocalizedError {
    case microphonePermissionDenied
    case audioSessionSetupFailed(Error)
    case audioComponentNotFound
    case audioUnitCreationFailed(OSStatus)
    case audioUnitInitFailed(OSStatus)
    case audioUnitStartFailed(OSStatus)
    
    var errorDescription: String? {
        switch self {
        case .microphonePermissionDenied:
            return "麦克风权限被拒绝"
        case .audioSessionSetupFailed(let err):
            return "音频会话设置失败: \(err.localizedDescription)"
        case .audioComponentNotFound:
            return "未找到 RemoteIO 音频组件"
        case .audioUnitCreationFailed(let status):
            return "AudioUnit 创建失败: \(status)"
        case .audioUnitInitFailed(let status):
            return "AudioUnit 初始化失败: \(status)"
        case .audioUnitStartFailed(let status):
            return "AudioUnit 启动失败: \(status)"
        }
    }
}

// C-convention audio render callback for RemoteIO input bus
private func remoteIORenderCallback(
    inRefCon: UnsafeMutableRawPointer,
    ioActionFlags: UnsafeMutablePointer<AudioUnitRenderActionFlags>,
    inTimeStamp: UnsafePointer<AudioTimeStamp>,
    inBusNumber: UInt32,
    inNumberFrames: UInt32,
    ioData: UnsafeMutablePointer<AudioBufferList>?
) -> OSStatus {
    let engine = Unmanaged<AudioCaptureEngine>.fromOpaque(inRefCon).takeUnretainedValue()
    return engine.processAudioInput(
        ioActionFlags: ioActionFlags,
        inTimeStamp: inTimeStamp,
        inBusNumber: inBusNumber,
        inNumberFrames: inNumberFrames
    )
}

@MainActor
final class AudioCaptureEngine: ObservableObject {
    
    // MARK: - Published State
    
    @Published var isRunning = false
    @Published var peakLevel: Float = -160.0
    @Published var rmsLevel: Float = -160.0
    @Published var errorMessage: String?
    @Published var inputDeviceName: String = "iPhone Microphone"
    
    // MARK: - Captured Configuration Snapshot (Immutable & Thread-safe)
    
    nonisolated private let sampleRate: Double
    nonisolated private let channelCount: UInt32
    nonisolated private let bufferDuration: Double
    
    nonisolated private let maxFrames: UInt32 = 4096
    
    // CoreAudio Handles & Buffers
    nonisolated(unsafe) private var audioUnit: AudioUnit?
    nonisolated(unsafe) private var dataCallback: AudioDataCallback?
    nonisolated(unsafe) private var bufferList: UnsafeMutablePointer<AudioBufferList>?
    nonisolated(unsafe) private var rawPcmBuffer: UnsafeMutablePointer<Int16>?
    
    // Level meter smoothing
    private var smoothedPeak: Float = -160.0
    private var smoothedRMS: Float = -160.0
    private let smoothingFactor: Float = 0.3
    
    // MARK: - Init
    
    init(config: AudioConfig) {
        self.sampleRate = Double(config.sampleRate.rawValue)
        self.channelCount = UInt32(config.channelMode.rawValue)
        self.bufferDuration = Double(config.bufferSize.rawValue) / Double(config.sampleRate.rawValue)
        allocateBuffers()
    }
    
    deinit {
        if let raw = rawPcmBuffer {
            raw.deallocate()
        }
        if let abl = bufferList {
            abl.deallocate()
        }
    }
    
    nonisolated private func allocateBuffers() {
        let byteSize = maxFrames * channelCount * 2
        let rawBuffer = UnsafeMutablePointer<Int16>.allocate(capacity: Int(maxFrames * channelCount))
        rawPcmBuffer = rawBuffer
        
        let ablSize = MemoryLayout<AudioBufferList>.size + MemoryLayout<AudioBuffer>.size
        let ptr = UnsafeMutableRawPointer.allocate(byteCount: ablSize, alignment: MemoryLayout<AudioBufferList>.alignment)
        let abl = ptr.bindMemory(to: AudioBufferList.self, capacity: 1)
        
        abl.pointee.mNumberBuffers = 1
        abl.pointee.mBuffers.mNumberChannels = channelCount
        abl.pointee.mBuffers.mDataByteSize = byteSize
        abl.pointee.mBuffers.mData = UnsafeMutableRawPointer(rawBuffer)
        
        bufferList = abl
    }
    
    // MARK: - Public API
    
    /// Set the callback that receives PCM data for network transmission
    func setDataCallback(_ callback: @escaping AudioDataCallback) {
        self.dataCallback = callback
    }
    
    /// Request microphone permission
    func requestPermission() async -> Bool {
        return await withCheckedContinuation { continuation in
            if #available(iOS 17.0, *) {
                AVAudioApplication.requestRecordPermission { granted in
                    continuation.resume(returning: granted)
                }
            } else {
                AVAudioSession.sharedInstance().requestRecordPermission { granted in
                    continuation.resume(returning: granted)
                }
            }
        }
    }
    
    /// Start hardware audio capture
    func start() async throws {
        let granted = await requestPermission()
        guard granted else {
            throw AudioCaptureError.microphonePermissionDenied
        }
        
        try configureAudioSession()
        try setupRemoteIO()
        
        guard let unit = audioUnit else {
            throw AudioCaptureError.audioComponentNotFound
        }
        
        let status = AudioOutputUnitStart(unit)
        guard status == noErr else {
            throw AudioCaptureError.audioUnitStartFailed(status)
        }
        
        self.isRunning = true
        self.errorMessage = nil
        
        print("[AudioCapture] CoreAudio RemoteIO started successfully")
    }
    
    /// Stop hardware audio capture
    func stop() {
        guard isRunning || audioUnit != nil else { return }
        
        if let unit = audioUnit {
            AudioOutputUnitStop(unit)
            AudioUnitUninitialize(unit)
            AudioComponentInstanceDispose(unit)
            audioUnit = nil
        }
        
        self.isRunning = false
        self.peakLevel = -160.0
        self.rmsLevel = -160.0
        
        print("[AudioCapture] CoreAudio RemoteIO stopped")
    }
    
    // MARK: - Private Implementation
    
    private func configureAudioSession() throws {
        let session = AVAudioSession.sharedInstance()
        do {
            try session.setCategory(.playAndRecord, mode: .measurement, options: [.defaultToSpeaker, .allowBluetooth])
            try session.setPreferredSampleRate(sampleRate)
            try session.setPreferredIOBufferDuration(bufferDuration)
            try session.setActive(true)
            
            let portName = session.currentRoute.inputs.first?.portName ?? "iPhone Microphone"
            self.inputDeviceName = portName
            
            print("[AudioCapture] Session active: SR=\(session.sampleRate)Hz, HW Buffer=\(session.ioBufferDuration * 1000)ms")
        } catch {
            throw AudioCaptureError.audioSessionSetupFailed(error)
        }
    }
    
    private func setupRemoteIO() throws {
        var desc = AudioComponentDescription(
            componentType: kAudioUnitType_Output,
            componentSubType: kAudioUnitSubType_RemoteIO,
            componentManufacturer: kAudioUnitManufacturer_Apple,
            componentFlags: 0,
            componentFlagsMask: 0
        )
        
        guard let component = AudioComponentFindNext(nil, &desc) else {
            throw AudioCaptureError.audioComponentNotFound
        }
        
        var unit: AudioUnit?
        var status = AudioComponentInstanceNew(component, &unit)
        guard status == noErr, let au = unit else {
            throw AudioCaptureError.audioUnitCreationFailed(status)
        }
        
        // 1. Enable Input on Bus 1 (microphone input)
        var enableInput: UInt32 = 1
        status = AudioUnitSetProperty(
            au,
            kAudioOutputUnitProperty_EnableIO,
            kAudioUnitScope_Input,
            1,
            &enableInput,
            UInt32(MemoryLayout<UInt32>.size)
        )
        guard status == noErr else { throw AudioCaptureError.audioUnitInitFailed(status) }
        
        // 2. Disable Output on Bus 0 (speaker output)
        var disableOutput: UInt32 = 0
        status = AudioUnitSetProperty(
            au,
            kAudioOutputUnitProperty_EnableIO,
            kAudioUnitScope_Output,
            0,
            &disableOutput,
            UInt32(MemoryLayout<UInt32>.size)
        )
        guard status == noErr else { throw AudioCaptureError.audioUnitInitFailed(status) }
        
        // 3. Configure AudioStreamBasicDescription: 16-bit Linear PCM, Little-Endian, Packed
        var asbd = AudioStreamBasicDescription(
            mSampleRate: sampleRate,
            mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
            mBytesPerPacket: 2 * channelCount,
            mFramesPerPacket: 1,
            mBytesPerFrame: 2 * channelCount,
            mChannelsPerFrame: channelCount,
            mBitsPerChannel: 16,
            mReserved: 0
        )
        
        status = AudioUnitSetProperty(
            au,
            kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Output,
            1,
            &asbd,
            UInt32(MemoryLayout<AudioStreamBasicDescription>.size)
        )
        guard status == noErr else { throw AudioCaptureError.audioUnitInitFailed(status) }
        
        // 4. Set Render Callback
        var callbackStruct = AURenderCallbackStruct(
            inputProc: remoteIORenderCallback,
            inputProcRefCon: Unmanaged.passUnretained(self).toOpaque()
        )
        status = AudioUnitSetProperty(
            au,
            kAudioOutputUnitProperty_SetInputCallback,
            kAudioUnitScope_Global,
            0,
            &callbackStruct,
            UInt32(MemoryLayout<AURenderCallbackStruct>.size)
        )
        guard status == noErr else { throw AudioCaptureError.audioUnitInitFailed(status) }
        
        // 5. Initialize AudioUnit
        status = AudioUnitInitialize(au)
        guard status == noErr else { throw AudioCaptureError.audioUnitInitFailed(status) }
        
        self.audioUnit = au
    }
    
    nonisolated fileprivate func processAudioInput(
        ioActionFlags: UnsafeMutablePointer<AudioUnitRenderActionFlags>,
        inTimeStamp: UnsafePointer<AudioTimeStamp>,
        inBusNumber: UInt32,
        inNumberFrames: UInt32
    ) -> OSStatus {
        guard let au = audioUnit, let abl = bufferList else { return noErr }
        
        let byteSize = inNumberFrames * channelCount * 2
        abl.pointee.mBuffers.mDataByteSize = byteSize
        
        let status = AudioUnitRender(au, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, abl)
        guard status == noErr else { return status }
        
        guard let pcmPtr = abl.pointee.mBuffers.mData?.assumingMemoryBound(to: Int16.self) else {
            return noErr
        }
        
        // Calculate Peak & RMS levels from 16-bit PCM samples
        let sampleCount = Int(inNumberFrames * channelCount)
        var maxVal: Float = 0.0
        var sumSq: Double = 0.0
        
        for i in 0..<sampleCount {
            let s = Float(pcmPtr[i]) / 32768.0
            let abs_s = abs(s)
            if abs_s > maxVal { maxVal = abs_s }
            sumSq += Double(s * s)
        }
        
        let rms = sampleCount > 0 ? Float(sqrt(sumSq / Double(sampleCount))) : 0.0
        let peakDB: Float = maxVal > 0 ? 20.0 * log10(maxVal) : -160.0
        let rmsDB: Float = rms > 0 ? 20.0 * log10(rms) : -160.0
        
        let pcmData = Data(bytes: pcmPtr, count: Int(byteSize))
        dataCallback?(pcmData, peakDB, rmsDB)
        
        return noErr
    }
}
