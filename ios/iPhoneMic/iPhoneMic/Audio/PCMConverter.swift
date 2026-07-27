// PCMConverter.swift
// iPhoneMic
//
// Converts Float32 PCM audio (from AVAudioEngine) to 24-bit signed integer
// PCM format for wire transmission.

import Foundation
import AVFoundation
import Accelerate

final class PCMConverter {
    
    /// Convert AVAudioPCMBuffer (Float32) to 24-bit signed integer PCM Data.
    ///
    /// iOS's AVAudioEngine always provides Float32 samples. We convert to
    /// 24-bit LE integers for wire transmission to minimize bandwidth while
    /// maintaining professional audio quality.
    ///
    /// - Parameters:
    ///   - buffer: Source audio buffer (must be Float32 format)
    ///   - channelCount: Number of channels to output (1=mono, 2=stereo)
    /// - Returns: Raw 24-bit PCM data, or nil on error
    static func convertToInt24(buffer: AVAudioPCMBuffer, 
                                channelCount: Int = 1) -> Data? {
        guard let floatData = buffer.floatChannelData else { return nil }
        
        let frameLength = Int(buffer.frameLength)
        guard frameLength > 0 else { return nil }
        
        let srcChannels = Int(buffer.format.channelCount)
        let outChannels = min(channelCount, srcChannels)
        
        // Output: 3 bytes per sample, interleaved
        let outputSize = frameLength * outChannels * 3
        var output = Data(count: outputSize)
        
        output.withUnsafeMutableBytes { rawPtr in
            guard let dst = rawPtr.baseAddress?.assumingMemoryBound(to: UInt8.self) else {
                return
            }
            
            var writeOffset = 0
            
            for frame in 0..<frameLength {
                for ch in 0..<outChannels {
                    // Get float sample [-1.0, 1.0]
                    let sample = floatData[ch][frame]
                    
                    // Clamp and convert to 24-bit signed integer
                    let clamped = max(-1.0, min(1.0, sample))
                    let scaled = Int32(clamped * 8388607.0)  // 2^23 - 1
                    
                    // Write as 3 bytes, little-endian
                    dst[writeOffset]     = UInt8(truncatingIfNeeded: scaled & 0xFF)
                    dst[writeOffset + 1] = UInt8(truncatingIfNeeded: (scaled >> 8) & 0xFF)
                    dst[writeOffset + 2] = UInt8(truncatingIfNeeded: (scaled >> 16) & 0xFF)
                    
                    writeOffset += 3
                }
            }
        }
        
        return output
    }
    
    /// Calculate peak and RMS levels from Float32 buffer.
    ///
    /// Uses Accelerate framework for optimized vector math.
    ///
    /// - Parameter buffer: Source audio buffer
    /// - Returns: Tuple of (peakDB, rmsDB) in dBFS
    static func calculateLevels(buffer: AVAudioPCMBuffer) -> (peak: Float, rms: Float) {
        guard let floatData = buffer.floatChannelData else {
            return (-160.0, -160.0)
        }
        
        let frameLength = Int(buffer.frameLength)
        guard frameLength > 0 else { return (-160.0, -160.0) }
        
        // Use channel 0 for level metering
        let samples = floatData[0]
        
        // Peak using vDSP
        var peak: Float = 0.0
        let count = vDSP_Length(frameLength)
        vDSP_maxmgv(samples, 1, &peak, count)
        
        // RMS using vDSP
        var rms: Float = 0.0
        vDSP_rmsqv(samples, 1, &rms, count)
        
        // Convert to dBFS
        let peakDB: Float = peak > 0 ? 20.0 * log10(peak) : -160.0
        let rmsDB: Float = rms > 0 ? 20.0 * log10(rms) : -160.0
        
        return (peakDB, rmsDB)
    }
    
    /// Convert a mono Float32 buffer to stereo (duplicate L channel to R).
    /// Used when channelMode is stereo but hardware provides mono.
    static func monoToStereoInt24(buffer: AVAudioPCMBuffer) -> Data? {
        guard let floatData = buffer.floatChannelData else { return nil }
        
        let frameLength = Int(buffer.frameLength)
        guard frameLength > 0 else { return nil }
        
        // Output: 3 bytes × 2 channels per frame
        let outputSize = frameLength * 2 * 3
        var output = Data(count: outputSize)
        
        output.withUnsafeMutableBytes { rawPtr in
            guard let dst = rawPtr.baseAddress?.assumingMemoryBound(to: UInt8.self) else {
                return
            }
            
            let srcChannels = Int(buffer.format.channelCount)
            var writeOffset = 0
            
            for frame in 0..<frameLength {
                let sample = floatData[0][frame]
                let clamped = max(-1.0, min(1.0, sample))
                let scaled = Int32(clamped * 8388607.0)
                
                let b0 = UInt8(truncatingIfNeeded: scaled & 0xFF)
                let b1 = UInt8(truncatingIfNeeded: (scaled >> 8) & 0xFF)
                let b2 = UInt8(truncatingIfNeeded: (scaled >> 16) & 0xFF)
                
                // Left channel
                dst[writeOffset]     = b0
                dst[writeOffset + 1] = b1
                dst[writeOffset + 2] = b2
                
                // Right channel (duplicate or use channel 1 if available)
                if srcChannels > 1 {
                    let sampleR = floatData[1][frame]
                    let clampedR = max(-1.0, min(1.0, sampleR))
                    let scaledR = Int32(clampedR * 8388607.0)
                    dst[writeOffset + 3] = UInt8(truncatingIfNeeded: scaledR & 0xFF)
                    dst[writeOffset + 4] = UInt8(truncatingIfNeeded: (scaledR >> 8) & 0xFF)
                    dst[writeOffset + 5] = UInt8(truncatingIfNeeded: (scaledR >> 16) & 0xFF)
                } else {
                    // Duplicate mono to stereo
                    dst[writeOffset + 3] = b0
                    dst[writeOffset + 4] = b1
                    dst[writeOffset + 5] = b2
                }
                
                writeOffset += 6
            }
        }
        
        return output
    }
}
