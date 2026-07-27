// iPhoneMicApp.swift
// iPhoneMic
//
// Main app entry point with SwiftUI lifecycle.

import SwiftUI
import AVFoundation

@main
struct iPhoneMicApp: App {
    
    @StateObject private var audioConfig = AudioConfig()
    @StateObject private var viewModel = AppViewModel()
    
    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(audioConfig)
                .environmentObject(viewModel)
                .onAppear {
                    // Prevent screen from sleeping during use
                    UIApplication.shared.isIdleTimerDisabled = true
                }
                .onDisappear {
                    UIApplication.shared.isIdleTimerDisabled = false
                }
        }
    }
}

// MARK: - App ViewModel

/// Coordinates AudioCaptureEngine and USBServer
@MainActor
final class AppViewModel: ObservableObject {
    
    @Published var isStreaming = false
    @Published var connectionState: ConnectionState = .disconnected
    @Published var peakLevel: Float = -160.0
    @Published var rmsLevel: Float = -160.0
    @Published var bytesSent: UInt64 = 0
    @Published var packetsDropped: UInt64 = 0
    @Published var errorMessage: String?
    @Published var inputDeviceName: String = "iPhone Microphone"
    
    private var captureEngine: AudioCaptureEngine?
    private var usbServer: USBServer?
    
    func startStreaming(config: AudioConfig) {
        guard !isStreaming else { return }
        
        // Create components
        let server = USBServer(config: config)
        let engine = AudioCaptureEngine(config: config)
        
        self.usbServer = server
        self.captureEngine = engine
        
        // Wire up: audio data → network
        engine.setDataCallback { [weak server, weak self] pcmData, peak, rms in
            server?.sendAudioData(pcmData)
            DispatchQueue.main.async {
                self?.peakLevel = peak
                self?.rmsLevel = rms
            }
        }
        
        // Start server first, then audio
        server.startListening()
        
        Task {
            do {
                try await engine.start()
                self.isStreaming = true
                self.errorMessage = nil
                self.inputDeviceName = engine.inputDeviceName
                
                // Observe server state
                self.observeServerState(server)
                
            } catch {
                self.errorMessage = error.localizedDescription
                server.stopListening()
            }
        }
    }
    
    func stopStreaming() {
        captureEngine?.stop()
        usbServer?.stopListening()
        
        captureEngine = nil
        usbServer = nil
        
        isStreaming = false
        connectionState = .disconnected
        peakLevel = -160.0
        rmsLevel = -160.0
        bytesSent = 0
        packetsDropped = 0
    }
    
    private func observeServerState(_ server: USBServer) {
        // Poll server state periodically for UI updates
        // (In production, use Combine publishers instead)
        Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self, weak server] timer in
            guard let self = self, let server = server else {
                timer.invalidate()
                return
            }
            
            Task { @MainActor in
                self.connectionState = server.connectionState
                self.bytesSent = server.bytesSent
                self.packetsDropped = server.packetsDropped
            }
        }
    }
}
