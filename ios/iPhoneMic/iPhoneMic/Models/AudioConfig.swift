// AudioConfig.swift
// iPhoneMic
//
// Audio configuration model for the microphone capture system.

import Foundation
import AVFoundation

// MARK: - Supported Values

enum SupportedSampleRate: Int, CaseIterable, Identifiable {
    case rate48000 = 48000
    
    var id: Int { rawValue }
    var displayName: String { "\(rawValue) Hz" }
}

enum SupportedBufferSize: Int, CaseIterable, Identifiable {
    case size64  = 64
    case size128 = 128
    case size256 = 256
    case size512 = 512
    
    var id: Int { rawValue }
    var displayName: String { "\(rawValue) samples" }
    
    var latencyMs: Double {
        Double(rawValue) / 48000.0 * 1000.0
    }
}

enum ChannelMode: Int, CaseIterable, Identifiable {
    case mono   = 1
    case stereo = 2
    
    var id: Int { rawValue }
    var displayName: String {
        switch self {
        case .mono:   return "Mono"
        case .stereo: return "Stereo"
        }
    }
}

// MARK: - Audio Config

@MainActor
class AudioConfig: ObservableObject {
    @Published var sampleRate: SupportedSampleRate = .rate48000
    @Published var bufferSize: SupportedBufferSize = .size256
    @Published var channelMode: ChannelMode = .mono
    
    /// Fixed at 24-bit for this application
    let bitDepth: Int = 24
    
    /// Bytes per sample (24-bit = 3 bytes)
    var bytesPerSample: Int { bitDepth / 8 }
    
    /// Bytes per frame (bytesPerSample * channels)
    var bytesPerFrame: Int { bytesPerSample * channelMode.rawValue }
    
    /// Bytes per buffer (bytesPerFrame * bufferSize)
    var bytesPerBuffer: Int { bytesPerFrame * bufferSize.rawValue }
    
    /// Buffer latency in milliseconds
    var bufferLatencyMs: Double {
        Double(bufferSize.rawValue) / Double(sampleRate.rawValue) * 1000.0
    }
    
    /// Create a protocol config packet from current settings
    func toConfigPacket() -> AudioConfigPacket {
        return AudioConfigPacket(
            sampleRate: sampleRate.rawValue,
            bitDepth: bitDepth,
            channels: channelMode.rawValue,
            bufferSize: bufferSize.rawValue
        )
    }
}

// MARK: - Connection State

enum ConnectionState: Equatable {
    case disconnected
    case waitingForConnection
    case connected(peerName: String)
    case error(message: String)
    
    var displayName: String {
        switch self {
        case .disconnected:          return "未连接"
        case .waitingForConnection:  return "等待连接..."
        case .connected(let peer):   return "已连接: \(peer)"
        case .error(let msg):        return "错误: \(msg)"
        }
    }
    
    var isConnected: Bool {
        if case .connected = self { return true }
        return false
    }
}

// MARK: - Audio Level

struct AudioLevel {
    var peak: Float = -160.0     // dBFS
    var rms: Float = -160.0      // dBFS
    var clipping: Bool = false
    
    static let silence = AudioLevel()
    
    /// Convert linear amplitude (0.0 - 1.0) to dBFS
    static func linearToDBFS(_ linear: Float) -> Float {
        if linear <= 0 { return -160.0 }
        return 20.0 * log10(linear)
    }
}
