// Protocol.swift
// iPhoneMic
//
// Binary wire protocol for iPhone-to-Windows audio streaming over USB.
// All multi-byte values are little-endian.

import Foundation

// MARK: - Constants

/// Protocol magic bytes: "IPHM" (iPhone Microphone)
let kProtocolMagic: UInt32 = 0x4D485049  // "IPHM" in little-endian

/// Current protocol version
let kProtocolVersion: UInt16 = 1

/// Default TCP port for the audio server
let kDefaultPort: UInt16 = 8730

/// Heartbeat interval in seconds
let kHeartbeatInterval: TimeInterval = 1.0

// MARK: - Packet Types

enum PacketType: UInt16 {
    case audioData  = 0x01
    case config     = 0x02
    case heartbeat  = 0x03
    case configAck  = 0x04
}

// MARK: - Packet Header

/// 24-byte packet header
/// Layout:
///   [0..3]   magic      (UInt32 LE) - "IPHM"
///   [4..5]   version    (UInt16 LE) - protocol version
///   [6..7]   type       (UInt16 LE) - packet type
///   [8..11]  payloadSize(UInt32 LE) - payload byte count
///   [12..15] reserved   (UInt32 LE) - reserved for future use
///   [16..23] timestamp  (UInt64 LE) - microsecond timestamp
struct PacketHeader {
    static let size = 24
    
    let magic: UInt32
    let version: UInt16
    let type: PacketType
    let payloadSize: UInt32
    let reserved: UInt32
    let timestamp: UInt64
    
    init(type: PacketType, payloadSize: UInt32) {
        self.magic = kProtocolMagic
        self.version = kProtocolVersion
        self.type = type
        self.payloadSize = payloadSize
        self.reserved = 0
        self.timestamp = Self.currentTimestampMicros()
    }
    
    /// Current time in microseconds since some reference point
    static func currentTimestampMicros() -> UInt64 {
        return UInt64(mach_absolute_time() / 1000)
    }
    
    /// Serialize header to 24 bytes (little-endian)
    func serialize() -> Data {
        var data = Data(capacity: PacketHeader.size)
        
        var magic = self.magic.littleEndian
        data.append(Data(bytes: &magic, count: 4))
        
        var version = self.version.littleEndian
        data.append(Data(bytes: &version, count: 2))
        
        var type = self.type.rawValue.littleEndian
        data.append(Data(bytes: &type, count: 2))
        
        var payloadSize = self.payloadSize.littleEndian
        data.append(Data(bytes: &payloadSize, count: 4))
        
        var reserved = self.reserved.littleEndian
        data.append(Data(bytes: &reserved, count: 4))
        
        var timestamp = self.timestamp.littleEndian
        data.append(Data(bytes: &timestamp, count: 8))
        
        return data
    }
    
    /// Deserialize header from raw bytes
    static func deserialize(from data: Data) -> PacketHeader? {
        guard data.count >= PacketHeader.size else { return nil }
        
        let magic = data.withUnsafeBytes { ptr -> UInt32 in
            ptr.load(fromByteOffset: 0, as: UInt32.self).littleEndian
        }
        guard magic == kProtocolMagic else { return nil }
        
        let version = data.withUnsafeBytes { ptr -> UInt16 in
            ptr.load(fromByteOffset: 4, as: UInt16.self).littleEndian
        }
        
        let typeRaw = data.withUnsafeBytes { ptr -> UInt16 in
            ptr.load(fromByteOffset: 6, as: UInt16.self).littleEndian
        }
        guard let type = PacketType(rawValue: typeRaw) else { return nil }
        
        let payloadSize = data.withUnsafeBytes { ptr -> UInt32 in
            ptr.load(fromByteOffset: 8, as: UInt32.self).littleEndian
        }
        
        let reserved = data.withUnsafeBytes { ptr -> UInt32 in
            ptr.load(fromByteOffset: 12, as: UInt32.self).littleEndian
        }
        
        let timestamp = data.withUnsafeBytes { ptr -> UInt64 in
            ptr.load(fromByteOffset: 16, as: UInt64.self).littleEndian
        }
        
        return PacketHeader(
            magic: magic,
            version: version,
            type: type,
            payloadSize: payloadSize,
            reserved: reserved,
            timestamp: timestamp
        )
    }
    
    // Private full initializer
    private init(magic: UInt32, version: UInt16, type: PacketType,
                 payloadSize: UInt32, reserved: UInt32, timestamp: UInt64) {
        self.magic = magic
        self.version = version
        self.type = type
        self.payloadSize = payloadSize
        self.reserved = reserved
        self.timestamp = timestamp
    }
}

// MARK: - Config Packet Payload

/// Configuration data sent as JSON payload
struct AudioConfigPacket: Codable {
    let sampleRate: Int
    let bitDepth: Int
    let channels: Int
    let bufferSize: Int
    
    func serialize() -> Data? {
        return try? JSONEncoder().encode(self)
    }
    
    static func deserialize(from data: Data) -> AudioConfigPacket? {
        return try? JSONDecoder().decode(AudioConfigPacket.self, from: data)
    }
}

// MARK: - Packet Builder

enum PacketBuilder {
    
    /// Build a complete audio data packet (header + PCM payload)
    static func buildAudioPacket(pcmData: Data) -> Data {
        let header = PacketHeader(type: .audioData, payloadSize: UInt32(pcmData.count))
        var packet = header.serialize()
        packet.append(pcmData)
        return packet
    }
    
    /// Build a config packet
    static func buildConfigPacket(config: AudioConfigPacket) -> Data? {
        guard let payload = config.serialize() else { return nil }
        let header = PacketHeader(type: .config, payloadSize: UInt32(payload.count))
        var packet = header.serialize()
        packet.append(payload)
        return packet
    }
    
    /// Build a heartbeat packet (no payload)
    static func buildHeartbeatPacket() -> Data {
        let header = PacketHeader(type: .heartbeat, payloadSize: 0)
        return header.serialize()
    }
    
    /// Build a config ACK packet
    static func buildConfigAckPacket() -> Data {
        let header = PacketHeader(type: .configAck, payloadSize: 0)
        return header.serialize()
    }
}
