// ContentView.swift
// iPhoneMic
//
// Main UI for the iPhone USB Microphone application.
// Shows connection status, audio levels, and configuration controls.

import SwiftUI

struct ContentView: View {
    @EnvironmentObject var config: AudioConfig
    @EnvironmentObject var viewModel: AppViewModel
    
    var body: some View {
        NavigationView {
            ZStack {
                // Background gradient
                LinearGradient(
                    colors: [Color(hex: "0a0a1a"), Color(hex: "1a1a3a")],
                    startPoint: .top,
                    endPoint: .bottom
                )
                .ignoresSafeArea()
                
                ScrollView {
                    VStack(spacing: 24) {
                        // Header
                        headerSection
                        
                        // Connection status
                        connectionStatusCard
                        
                        // Level meter
                        levelMeterCard
                        
                        // Stats
                        if viewModel.isStreaming {
                            statsCard
                        }
                        
                        // Configuration
                        if !viewModel.isStreaming {
                            configCard
                        }
                        
                        // Start/Stop button
                        controlButton
                        
                        // Error display
                        if let error = viewModel.errorMessage {
                            errorBanner(error)
                        }
                        
                        Spacer(minLength: 40)
                    }
                    .padding(.horizontal)
                }
            }
            .navigationBarHidden(true)
        }
    }
    
    // MARK: - Header
    
    private var headerSection: some View {
        VStack(spacing: 8) {
            Image(systemName: "mic.fill")
                .font(.system(size: 40))
                .foregroundStyle(
                    LinearGradient(
                        colors: [Color(hex: "6366f1"), Color(hex: "8b5cf6")],
                        startPoint: .topLeading,
                        endPoint: .bottomTrailing
                    )
                )
                .padding(.top, 20)
            
            Text("iPhone USB Mic")
                .font(.system(size: 28, weight: .bold, design: .rounded))
                .foregroundColor(.white)
            
            Text("专业 USB 麦克风输入设备")
                .font(.system(size: 14, weight: .medium))
                .foregroundColor(.gray)
        }
    }
    
    // MARK: - Connection Status
    
    private var connectionStatusCard: some View {
        GlassCard {
            HStack(spacing: 12) {
                // Status indicator dot
                Circle()
                    .fill(statusColor)
                    .frame(width: 12, height: 12)
                    .overlay(
                        Circle()
                            .fill(statusColor.opacity(0.5))
                            .frame(width: 20, height: 20)
                            .opacity(viewModel.connectionState.isConnected ? 1 : 0)
                            .animation(.easeInOut(duration: 1).repeatForever(), 
                                     value: viewModel.connectionState.isConnected)
                    )
                
                VStack(alignment: .leading, spacing: 4) {
                    Text("连接状态")
                        .font(.system(size: 12, weight: .medium))
                        .foregroundColor(.gray)
                    
                    Text(viewModel.connectionState.displayName)
                        .font(.system(size: 16, weight: .semibold))
                        .foregroundColor(.white)
                }
                
                Spacer()
                
                // USB icon
                Image(systemName: viewModel.connectionState.isConnected ? 
                      "cable.connector" : "cable.connector.slash")
                    .font(.system(size: 24))
                    .foregroundColor(statusColor)
            }
        }
    }
    
    private var statusColor: Color {
        switch viewModel.connectionState {
        case .connected:          return Color(hex: "22c55e")
        case .waitingForConnection: return Color(hex: "f59e0b")
        case .error:              return Color(hex: "ef4444")
        case .disconnected:       return Color(hex: "6b7280")
        }
    }
    
    // MARK: - Level Meter
    
    private var levelMeterCard: some View {
        GlassCard {
            VStack(spacing: 16) {
                Text("音频电平")
                    .font(.system(size: 14, weight: .medium))
                    .foregroundColor(.gray)
                    .frame(maxWidth: .infinity, alignment: .leading)
                
                // Peak meter bar
                VStack(spacing: 8) {
                    meterBar(label: "Peak", level: viewModel.peakLevel)
                    meterBar(label: "RMS ", level: viewModel.rmsLevel)
                }
                
                // dB readout
                HStack {
                    Text("Peak: \(String(format: "%.1f", viewModel.peakLevel)) dBFS")
                        .font(.system(size: 12, design: .monospaced))
                        .foregroundColor(.gray)
                    
                    Spacer()
                    
                    Text("RMS: \(String(format: "%.1f", viewModel.rmsLevel)) dBFS")
                        .font(.system(size: 12, design: .monospaced))
                        .foregroundColor(.gray)
                }
            }
        }
    }
    
    private func meterBar(label: String, level: Float) -> some View {
        HStack(spacing: 8) {
            Text(label)
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(.gray)
                .frame(width: 35, alignment: .leading)
            
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    // Background
                    RoundedRectangle(cornerRadius: 4)
                        .fill(Color.white.opacity(0.05))
                    
                    // Level bar
                    let normalizedLevel = max(0, min(1, (level + 60) / 60))
                    RoundedRectangle(cornerRadius: 4)
                        .fill(meterGradient(level: normalizedLevel))
                        .frame(width: geo.size.width * CGFloat(normalizedLevel))
                        .animation(.linear(duration: 0.05), value: level)
                }
            }
            .frame(height: 16)
        }
    }
    
    private func meterGradient(level: Float) -> LinearGradient {
        LinearGradient(
            colors: [
                Color(hex: "22c55e"),    // Green
                Color(hex: "eab308"),    // Yellow
                Color(hex: "ef4444")     // Red
            ],
            startPoint: .leading,
            endPoint: .trailing
        )
    }
    
    // MARK: - Stats
    
    private var statsCard: some View {
        GlassCard {
            VStack(spacing: 12) {
                Text("传输统计")
                    .font(.system(size: 14, weight: .medium))
                    .foregroundColor(.gray)
                    .frame(maxWidth: .infinity, alignment: .leading)
                
                HStack(spacing: 20) {
                    statItem(title: "已发送", 
                            value: formatBytes(viewModel.bytesSent))
                    
                    statItem(title: "丢包数", 
                            value: "\(viewModel.packetsDropped)")
                    
                    statItem(title: "延迟", 
                            value: String(format: "%.1fms", config.bufferLatencyMs))
                    
                    statItem(title: "输入", 
                            value: viewModel.inputDeviceName)
                }
            }
        }
    }
    
    private func statItem(title: String, value: String) -> some View {
        VStack(spacing: 4) {
            Text(title)
                .font(.system(size: 11, weight: .medium))
                .foregroundColor(.gray)
            Text(value)
                .font(.system(size: 13, weight: .semibold, design: .monospaced))
                .foregroundColor(.white)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .frame(maxWidth: .infinity)
    }
    
    // MARK: - Config
    
    private var configCard: some View {
        GlassCard {
            VStack(spacing: 16) {
                Text("音频配置")
                    .font(.system(size: 14, weight: .medium))
                    .foregroundColor(.gray)
                    .frame(maxWidth: .infinity, alignment: .leading)
                
                // Sample Rate
                configRow(title: "采样率") {
                    Text("48,000 Hz")
                        .font(.system(size: 14, weight: .semibold, design: .monospaced))
                        .foregroundColor(Color(hex: "8b5cf6"))
                }
                
                // Bit Depth
                configRow(title: "位深") {
                    Text("16 bit")
                        .font(.system(size: 14, weight: .semibold, design: .monospaced))
                        .foregroundColor(Color(hex: "8b5cf6"))
                }
                
                // Buffer Size
                configRow(title: "缓冲区") {
                    Picker("Buffer", selection: $config.bufferSize) {
                        ForEach(SupportedBufferSize.allCases) { size in
                            Text(size.displayName).tag(size)
                        }
                    }
                    .pickerStyle(.segmented)
                }
                
                // Channel Mode
                configRow(title: "声道") {
                    Picker("Channel", selection: $config.channelMode) {
                        ForEach(ChannelMode.allCases) { mode in
                            Text(mode.displayName).tag(mode)
                        }
                    }
                    .pickerStyle(.segmented)
                }
                
                // Latency display
                HStack {
                    Text("缓冲延迟")
                        .font(.system(size: 13))
                        .foregroundColor(.gray)
                    Spacer()
                    Text(String(format: "%.2f ms", config.bufferLatencyMs))
                        .font(.system(size: 13, weight: .semibold, design: .monospaced))
                        .foregroundColor(Color(hex: "6366f1"))
                }
            }
        }
    }
    
    private func configRow<Content: View>(title: String, 
                                           @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title)
                .font(.system(size: 13))
                .foregroundColor(.gray)
            content()
        }
    }
    
    // MARK: - Control Button
    
    private var controlButton: some View {
        Button(action: {
            if viewModel.isStreaming {
                viewModel.stopStreaming()
            } else {
                viewModel.startStreaming(config: config)
            }
        }) {
            HStack(spacing: 12) {
                Image(systemName: viewModel.isStreaming ? "stop.fill" : "play.fill")
                    .font(.system(size: 18))
                
                Text(viewModel.isStreaming ? "停止" : "开始采集")
                    .font(.system(size: 18, weight: .bold))
            }
            .frame(maxWidth: .infinity)
            .frame(height: 56)
            .background(
                viewModel.isStreaming ?
                LinearGradient(
                    colors: [Color(hex: "ef4444"), Color(hex: "dc2626")],
                    startPoint: .leading, endPoint: .trailing
                ) :
                LinearGradient(
                    colors: [Color(hex: "6366f1"), Color(hex: "8b5cf6")],
                    startPoint: .leading, endPoint: .trailing
                )
            )
            .foregroundColor(.white)
            .cornerRadius(16)
            .shadow(color: viewModel.isStreaming ? 
                   Color(hex: "ef4444").opacity(0.4) :
                   Color(hex: "6366f1").opacity(0.4), 
                   radius: 12, x: 0, y: 6)
        }
        .padding(.top, 8)
    }
    
    // MARK: - Error Banner
    
    private func errorBanner(_ message: String) -> some View {
        HStack(spacing: 8) {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundColor(Color(hex: "f59e0b"))
            
            Text(message)
                .font(.system(size: 13))
                .foregroundColor(.white)
            
            Spacer()
        }
        .padding()
        .background(Color(hex: "ef4444").opacity(0.2))
        .cornerRadius(12)
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(Color(hex: "ef4444").opacity(0.5), lineWidth: 1)
        )
    }
    
    // MARK: - Helpers
    
    private func formatBytes(_ bytes: UInt64) -> String {
        if bytes < 1024 { return "\(bytes) B" }
        if bytes < 1024 * 1024 { return String(format: "%.1f KB", Double(bytes) / 1024) }
        return String(format: "%.1f MB", Double(bytes) / (1024 * 1024))
    }
}

// MARK: - Glass Card Component

struct GlassCard<Content: View>: View {
    let content: Content
    
    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }
    
    var body: some View {
        content
            .padding(16)
            .background(
                RoundedRectangle(cornerRadius: 16)
                    .fill(Color.white.opacity(0.05))
                    .background(
                        RoundedRectangle(cornerRadius: 16)
                            .fill(.ultraThinMaterial)
                    )
            )
            .overlay(
                RoundedRectangle(cornerRadius: 16)
                    .stroke(Color.white.opacity(0.1), lineWidth: 1)
            )
    }
}

// MARK: - Color Extension

extension Color {
    init(hex: String) {
        let scanner = Scanner(string: hex)
        var hexNumber: UInt64 = 0
        scanner.scanHexInt64(&hexNumber)
        
        let r = Double((hexNumber & 0xFF0000) >> 16) / 255
        let g = Double((hexNumber & 0x00FF00) >> 8) / 255
        let b = Double(hexNumber & 0x0000FF) / 255
        
        self.init(red: r, green: g, blue: b)
    }
}

// MARK: - Preview

#Preview {
    ContentView()
        .environmentObject(AudioConfig())
        .environmentObject(AppViewModel())
}
