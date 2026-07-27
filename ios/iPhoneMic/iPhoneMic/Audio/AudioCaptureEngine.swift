// AudioCaptureEngine.swift
// iPhoneMic
//
// Manages AVAudioEngine for real-time microphone capture.
// Captures audio from the built-in microphone, converts to 24-bit PCM,
// and delivers buffers to a callback for network transmission.

import Foundation
import AVFoundation

/// Callback type for delivering captured PCM audio data and levels
typealias AudioDataCallback = (Data, Float, Float) -> Void

/// Errors that can occur during audio capture
enum AudioCaptureError: Error, LocalizedError {
    case microphonePermissionDenied
    case audioSessionSetupFailed(Error)
    case engineStartFailed(Error)
    case noInputAvailable
    
    var errorDescription: String? {
        switch self {
        case .microphonePermissionDenied:
            return "麦克风权限被拒绝"
        case .audioSessionSetupFailed(let err):
            return "音频会话设置失败: \(err.localizedDescription)"
        case .engineStartFailed(let err):
            return "音频引擎启动失败: \(err.localizedDescription)"
        case .noInputAvailable:
            return "没有可用的音频输入"
        }
    }
}

@MainActor
final class AudioCaptureEngine: ObservableObject {
    
    // MARK: - Published State
    
    @Published var isRunning = false
    @Published var peakLevel: Float = -160.0
    @Published var rmsLevel: Float = -160.0
    @Published var errorMessage: String?
    @Published var inputDeviceName: String = "Unknown"
    
    // MARK: - Configuration
    
    private let config: AudioConfig
    
    // MARK: - Audio Engine
    
    private var audioEngine: AVAudioEngine?
    private var dataCallback: AudioDataCallback?
    
    // Level smoothing
    private var smoothedPeak: Float = -160.0
    private var smoothedRMS: Float = -160.0
    private let smoothingFactor: Float = 0.3
    
    // MARK: - Init
    
    init(config: AudioConfig) {
        self.config = config
    }
    
    deinit {
        // Stop is called from MainActor context
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
    
    /// Start audio capture
    func start() async throws {
        // Request permission
        let granted = await requestPermission()
        guard granted else {
            throw AudioCaptureError.microphonePermissionDenied
        }
        
        // Configure audio session
        try configureAudioSession()
        
        // Setup and start engine
        try setupEngine()
        
        isRunning = true
        errorMessage = nil
    }
    
    /// Stop audio capture
    func stop() {
        guard isRunning else { return }
        
        audioEngine?.inputNode.removeTap(onBus: 0)
        audioEngine?.stop()
        audioEngine = nil
        
        isRunning = false
        peakLevel = -160.0
        rmsLevel = -160.0
    }
    
    // MARK: - Private Implementation
    
    private func configureAudioSession() throws {
        let session = AVAudioSession.sharedInstance()
        
        do {
            // Set category for recording with low latency
            try session.setCategory(
                .playAndRecord,
                mode: .measurement,
                options: [.defaultToSpeaker, .allowBluetooth]
            )
            
            // Set preferred sample rate
            try session.setPreferredSampleRate(Double(config.sampleRate.rawValue))
            
            // Set preferred buffer duration based on config
            let bufferDuration = Double(config.bufferSize.rawValue) / Double(config.sampleRate.rawValue)
            try session.setPreferredIOBufferDuration(bufferDuration)
            
            // Activate session
            try session.setActive(true)
            
            // Log actual settings
            let actualSR = session.sampleRate
            let actualBuf = session.ioBufferDuration
            print("[AudioCapture] Session active: SR=\(actualSR)Hz, Buffer=\(actualBuf * 1000)ms")
            
        } catch {
            throw AudioCaptureError.audioSessionSetupFailed(error)
        }
    }
    
    private func setupEngine() throws {
        let engine = AVAudioEngine()
        let inputNode = engine.inputNode
        
        // Get the hardware input format
        let hwFormat = inputNode.inputFormat(forBus: 0)
        guard hwFormat.sampleRate > 0 else {
            throw AudioCaptureError.noInputAvailable
        }
        
        print("[AudioCapture] Hardware format: \(hwFormat)")
        
        // Update input device name on main thread
        let portName = AVAudioSession.sharedInstance().currentRoute.inputs.first?.portName ?? "iPhone Microphone"
        self.inputDeviceName = portName
        
        // Create the desired output format for the tap
        // We request Float32 at the hardware sample rate, then do our own conversion
        let tapFormat: AVAudioFormat
        if let format = AVAudioFormat(
            commonFormat: .pcmFormatFloat32,
            sampleRate: hwFormat.sampleRate,
            channels: AVAudioChannelCount(config.channelMode.rawValue),
            interleaved: false
        ) {
            tapFormat = format
        } else {
            tapFormat = hwFormat
        }
        
        // Install tap on input node
        let bufferSize = AVAudioFrameCount(config.bufferSize.rawValue)
        let channelCount = config.channelMode.rawValue
        
        // Capture weak self for the tap callback (runs on audio thread)
        let callback = self.dataCallback
        
        inputNode.installTap(onBus: 0, bufferSize: bufferSize, format: tapFormat) { 
            [weak self] (buffer, time) in
            
            // Convert Float32 → 16-bit PCM
            let pcmData: Data?
            if channelCount == 2 {
                pcmData = PCMConverter.monoToStereoInt16(buffer: buffer)
            } else {
                pcmData = PCMConverter.convertToInt16(
                    buffer: buffer, 
                    channelCount: channelCount
                )
            }
            
            // Calculate levels
            let levels = PCMConverter.calculateLevels(buffer: buffer)
            
            // Deliver PCM data via callback
            if let data = pcmData {
                callback?(data, levels.peak, levels.rms)
            }
            
            // Update UI levels (dispatch to main thread)
            DispatchQueue.main.async { [weak self] in
                guard let self = self else { return }
                // Exponential smoothing for visual display
                self.smoothedPeak = self.smoothingFactor * levels.peak + 
                                   (1.0 - self.smoothingFactor) * self.smoothedPeak
                self.smoothedRMS = self.smoothingFactor * levels.rms + 
                                  (1.0 - self.smoothingFactor) * self.smoothedRMS
                self.peakLevel = self.smoothedPeak
                self.rmsLevel = self.smoothedRMS
            }
        }
        
        // Start the engine
        do {
            try engine.start()
            self.audioEngine = engine
            print("[AudioCapture] Engine started successfully")
        } catch {
            throw AudioCaptureError.engineStartFailed(error)
        }
    }
}
