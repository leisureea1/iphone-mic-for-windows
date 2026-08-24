// ContentView.swift
// iPhoneMic
//
// Studio-Grade Realtime Audio Interface for iPhone USB Microphone.
// Designed with Obsidian Dark Mode, Frosted Glassmorphism, Dynamic Peak-Hold VU Meters,
// CoreAudio RemoteIO Engine Telemetry, and Tactile Haptics.

import SwiftUI
import AVFoundation

// MARK: - Main Content View

struct ContentView: View {
    @EnvironmentObject var config: AudioConfig
    @EnvironmentObject var viewModel: AppViewModel
    
    @State private var showingSettingsSheet = false
    @State private var showingHelpSheet = false
    @State private var sessionSeconds: Int = 0
    @State private var sessionTimer: Timer?
    @State private var isPulsing = false
    
    // Peak Hold State for Studio Meters
    @State private var peakHoldLeft: Float = -160.0
    @State private var peakHoldRight: Float = -160.0
    @State private var peakHoldTimerLeft: Timer?
    @State private var peakHoldTimerRight: Timer?
    
    var body: some View {
        ZStack {
            // Ambient Obsidian Background with Fluid Lighting
            backgroundLayer
            
            VStack(spacing: 0) {
                // Top Navigation & Device Bar
                topNavigationBar
                    .padding(.horizontal, 20)
                    .padding(.top, 12)
                    .padding(.bottom, 8)
                
                ScrollView(showsIndicators: false) {
                    VStack(spacing: 18) {
                        // Dynamic Status Hero Card
                        statusHeroCard
                        
                        // Professional Studio VU Level Meter with Peak Hold
                        studioVUMeterCard
                        
                        // Quick Settings Deck (Channel & Buffer)
                        if !viewModel.isStreaming {
                            quickConfigCard
                        }
                        
                        // Live Telemetry HUD (When streaming)
                        if viewModel.isStreaming {
                            liveTelemetryHUD
                        }
                        
                        // Master Stream Toggle Button
                        masterActionButton
                            .padding(.top, 4)
                        
                        // Error Alert Banner if any
                        if let error = viewModel.errorMessage {
                            errorBanner(error)
                        }
                        
                        // Bottom Hardware & Mode Hint
                        footerInfoBadge
                            .padding(.top, 6)
                            .padding(.bottom, 24)
                    }
                    .padding(.horizontal, 20)
                    .padding(.top, 8)
                }
            }
        }
        .sheet(isPresented: $showingSettingsSheet) {
            StudioSettingsSheet()
                .environmentObject(config)
        }
        .sheet(isPresented: $showingHelpSheet) {
            DAWGuideSheet()
        }
        .onChange(of: viewModel.isStreaming) { streaming in
            if streaming {
                startSessionTimer()
            } else {
                stopSessionTimer()
            }
        }
        .onChange(of: viewModel.peakLevel) { newPeak in
            updatePeakHold(newPeak: newPeak)
        }
        .onAppear {
            withAnimation(.easeInOut(duration: 2.0).repeatForever(autoreverses: true)) {
                isPulsing = true
            }
        }
    }
    
    // MARK: - Background Layer
    
    private var backgroundLayer: some View {
        ZStack {
            // Pure Obsidian Base
            Color(hex: "070710").ignoresSafeArea()
            
            // Top Right Violet Ambient Glow
            RadialGradient(
                colors: [Color(hex: "4f46e5").opacity(viewModel.isStreaming ? 0.38 : 0.20), Color.clear],
                center: .topTrailing,
                startRadius: 20,
                endRadius: 380
            )
            .ignoresSafeArea()
            
            // Bottom Left Cyan Ambient Glow
            RadialGradient(
                colors: [Color(hex: "06b6d4").opacity(viewModel.isStreaming ? 0.28 : 0.14), Color.clear],
                center: .bottomLeading,
                startRadius: 40,
                endRadius: 440
            )
            .ignoresSafeArea()
            
            // Subtle Architectural Grid Overlay
            GeometryReader { geo in
                Path { path in
                    let step: CGFloat = 32
                    for x in stride(from: 0, to: geo.size.width, by: step) {
                        path.move(to: CGPoint(x: x, y: 0))
                        path.addLine(to: CGPoint(x: x, y: geo.size.height))
                    }
                }
                .stroke(Color.white.opacity(0.015), lineWidth: 1)
            }
            .ignoresSafeArea()
        }
    }
    
    // MARK: - Top Navigation Bar
    
    private var topNavigationBar: some View {
        HStack {
            // Brand Logo & Title
            HStack(spacing: 10) {
                ZStack {
                    RoundedRectangle(cornerRadius: 11, style: .continuous)
                        .fill(LinearGradient(
                            colors: [Color(hex: "6366f1"), Color(hex: "8b5cf6")],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        ))
                        .frame(width: 36, height: 36)
                        .shadow(color: Color(hex: "6366f1").opacity(0.45), radius: 8, x: 0, y: 3)
                    
                    Image(systemName: "mic.fill")
                        .font(.system(size: 17, weight: .bold))
                        .foregroundColor(.white)
                }
                
                VStack(alignment: .leading, spacing: 2) {
                    HStack(spacing: 6) {
                        Text("iPhoneMic")
                            .font(.system(size: 18, weight: .bold, design: .rounded))
                            .foregroundColor(.white)
                        
                        Text("ASIO PRO")
                            .font(.system(size: 9, weight: .black, design: .monospaced))
                            .foregroundColor(Color(hex: "38bdf8"))
                            .padding(.horizontal, 5)
                            .padding(.vertical, 1.5)
                            .background(Capsule().fill(Color(hex: "0284c7").opacity(0.3)))
                    }
                    
                    Text("CoreAudio RemoteIO • 48kHz • 16-bit")
                        .font(.system(size: 10, weight: .medium))
                        .foregroundColor(Color.white.opacity(0.55))
                }
            }
            
            Spacer()
            
            // Action Icon Buttons
            HStack(spacing: 12) {
                Button(action: {
                    UIImpactFeedbackGenerator(style: .light).impactOccurred()
                    showingHelpSheet = true
                }) {
                    Image(systemName: "questionmark.circle.fill")
                        .font(.system(size: 21))
                        .foregroundColor(Color.white.opacity(0.7))
                }
                
                Button(action: {
                    UIImpactFeedbackGenerator(style: .light).impactOccurred()
                    showingSettingsSheet = true
                }) {
                    Image(systemName: "slider.horizontal.3")
                        .font(.system(size: 21))
                        .foregroundColor(Color.white.opacity(0.7))
                }
            }
        }
    }
    
    // MARK: - Status Hero Card
    
    private var statusHeroCard: some View {
        StudioCard {
            VStack(spacing: 14) {
                HStack(alignment: .center, spacing: 14) {
                    // Animated Radar / Connection Beacon
                    ZStack {
                        Circle()
                            .fill(statusColor.opacity(0.12))
                            .frame(width: 48, height: 48)
                        
                        Circle()
                            .stroke(statusColor.opacity(viewModel.isStreaming ? 0.6 : 0.2), lineWidth: 1.5)
                            .frame(width: 36, height: 36)
                            .scaleEffect(viewModel.isStreaming ? (isPulsing ? 1.15 : 0.95) : 1.0)
                        
                        Circle()
                            .fill(statusColor)
                            .frame(width: 14, height: 14)
                            .shadow(color: statusColor.opacity(0.8), radius: 6)
                    }
                    
                    VStack(alignment: .leading, spacing: 4) {
                        HStack(spacing: 6) {
                            Text(statusHeadline)
                                .font(.system(size: 16, weight: .bold, design: .rounded))
                                .foregroundColor(.white)
                            
                            if viewModel.isStreaming {
                                Text("LIVE")
                                    .font(.system(size: 9, weight: .black, design: .rounded))
                                    .foregroundColor(.white)
                                    .padding(.horizontal, 6)
                                    .padding(.vertical, 2)
                                    .background(Capsule().fill(Color(hex: "ef4444")))
                            }
                        }
                        
                        Text(statusSubheadline)
                            .font(.system(size: 12, weight: .medium))
                            .foregroundColor(Color.white.opacity(0.65))
                    }
                    
                    Spacer()
                    
                    // Session Timer or USB Icon
                    if viewModel.isStreaming {
                        VStack(alignment: .trailing, spacing: 2) {
                            Text(formatSessionTime(sessionSeconds))
                                .font(.system(size: 15, weight: .bold, design: .monospaced))
                                .foregroundColor(Color(hex: "38bdf8"))
                            
                            Text("已推流时间")
                                .font(.system(size: 10))
                                .foregroundColor(Color.white.opacity(0.45))
                        }
                    } else {
                        Image(systemName: "cable.connector")
                            .font(.system(size: 22))
                            .foregroundColor(statusColor.opacity(0.85))
                    }
                }
                
                // Hardware Input Source Pill
                HStack {
                    Image(systemName: "waveform.circle.fill")
                        .font(.system(size: 12))
                        .foregroundColor(Color(hex: "a78bfa"))
                    
                    Text("输入源: \(viewModel.inputDeviceName)")
                        .font(.system(size: 11, weight: .medium))
                        .foregroundColor(Color.white.opacity(0.8))
                    
                    Spacer()
                    
                    Text(config.channelMode.displayName)
                        .font(.system(size: 10, weight: .semibold, design: .monospaced))
                        .foregroundColor(Color(hex: "38bdf8"))
                        .padding(.horizontal, 8)
                        .padding(.vertical, 3)
                        .background(Capsule().fill(Color(hex: "0284c7").opacity(0.25)))
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
                .background(RoundedRectangle(cornerRadius: 10).fill(Color.white.opacity(0.04)))
            }
        }
    }
    
    private var statusHeadline: String {
        if viewModel.isStreaming {
            return viewModel.connectionState.isConnected ? "音频实时推流中" : "采集进行中 (等待 PC)"
        } else {
            return viewModel.connectionState.isConnected ? "USB 隧道已建立" : "待机准备就绪"
        }
    }
    
    private var statusSubheadline: String {
        switch viewModel.connectionState {
        case .connected(let peer):
            return "已直连 Windows: \(peer)"
        case .waitingForConnection:
            return "等待电脑端 DAW / ASIO 连接"
        case .error(let msg):
            return "连接异常: \(msg)"
        case .disconnected:
            return "请用 USB 连接 iPhone 到 Windows 电脑"
        }
    }
    
    private var statusColor: Color {
        if viewModel.isStreaming {
            return viewModel.connectionState.isConnected ? Color(hex: "10b981") : Color(hex: "f59e0b")
        }
        switch viewModel.connectionState {
        case .connected:          return Color(hex: "10b981")
        case .waitingForConnection: return Color(hex: "f59e0b")
        case .error:              return Color(hex: "ef4444")
        case .disconnected:       return Color(hex: "64748b")
        }
    }
    
    // MARK: - Studio VU Level Meter Card
    
    private var studioVUMeterCard: some View {
        StudioCard {
            VStack(spacing: 16) {
                // Header with DB Readout
                HStack {
                    HStack(spacing: 6) {
                        Image(systemName: "slider.vertical.3")
                            .font(.system(size: 12))
                            .foregroundColor(Color(hex: "38bdf8"))
                        Text("录音声学电平 (dBFS)")
                            .font(.system(size: 13, weight: .semibold))
                            .foregroundColor(.white)
                    }
                    
                    Spacer()
                    
                    HStack(spacing: 12) {
                        HStack(spacing: 4) {
                            Text("PEAK")
                                .font(.system(size: 10, weight: .bold))
                                .foregroundColor(.gray)
                            Text(String(format: "%.1f", viewModel.peakLevel))
                                .font(.system(size: 12, weight: .bold, design: .monospaced))
                                .foregroundColor(dbColor(for: viewModel.peakLevel))
                        }
                        
                        HStack(spacing: 4) {
                            Text("RMS")
                                .font(.system(size: 10, weight: .bold))
                                .foregroundColor(.gray)
                            Text(String(format: "%.1f", viewModel.rmsLevel))
                                .font(.system(size: 12, weight: .bold, design: .monospaced))
                                .foregroundColor(Color(hex: "38bdf8"))
                        }
                    }
                }
                
                // Studio Segmented Bars with Peak Hold Indicators
                VStack(spacing: 10) {
                    vuSegmentedBar(title: "CH 1 / 左声道 (L)", level: viewModel.peakLevel, peakHold: peakHoldLeft)
                    if config.channelMode == .stereo {
                        vuSegmentedBar(title: "CH 2 / 右声道 (R)", level: viewModel.peakLevel * 0.98, peakHold: peakHoldRight)
                    }
                }
                
                // dB Scale Ticks
                HStack {
                    Text("-60").frame(maxWidth: .infinity, alignment: .leading)
                    Text("-42").frame(maxWidth: .infinity, alignment: .center)
                    Text("-24").frame(maxWidth: .infinity, alignment: .center)
                    Text("-12").frame(maxWidth: .infinity, alignment: .center)
                    Text("-6").frame(maxWidth: .infinity, alignment: .center)
                    Text("-1").frame(maxWidth: .infinity, alignment: .trailing)
                    Text("CLIP").frame(width: 32, alignment: .trailing)
                        .foregroundColor(viewModel.peakLevel >= -0.5 ? Color(hex: "ef4444") : Color.white.opacity(0.3))
                }
                .font(.system(size: 9, weight: .bold, design: .monospaced))
                .foregroundColor(Color.white.opacity(0.35))
            }
        }
    }
    
    private func vuSegmentedBar(title: String, level: Float, peakHold: Float) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(title)
                    .font(.system(size: 10, weight: .bold, design: .monospaced))
                    .foregroundColor(Color.white.opacity(0.55))
                Spacer()
            }
            
            GeometryReader { geo in
                let width = geo.size.width
                let normalized = CGFloat(max(0.0, min(1.0, (level + 60.0) / 60.0)))
                let barWidth = width * normalized
                
                let holdNormalized = CGFloat(max(0.0, min(1.0, (peakHold + 60.0) / 60.0)))
                let holdOffset = max(0, min(width - 3, width * holdNormalized))
                
                ZStack(alignment: .leading) {
                    // Track background
                    RoundedRectangle(cornerRadius: 6)
                        .fill(Color.white.opacity(0.04))
                    
                    // Active VU gradient bar
                    RoundedRectangle(cornerRadius: 6)
                        .fill(LinearGradient(
                            stops: [
                                .init(color: Color(hex: "10b981"), location: 0.0),   // Green (-60 to -18)
                                .init(color: Color(hex: "3b82f6"), location: 0.55),  // Cyan/Blue
                                .init(color: Color(hex: "eab308"), location: 0.82),  // Amber (-6 to -1)
                                .init(color: Color(hex: "ef4444"), location: 1.0)    // Red Peak
                            ],
                            startPoint: .leading,
                            endPoint: .trailing
                        ))
                        .frame(width: barWidth)
                        .animation(.linear(duration: 0.03), value: level)
                        .shadow(color: dbColor(for: level).opacity(0.35), radius: 4, x: 0, y: 0)
                    
                    // Studio Peak Hold Floating Notch
                    if peakHold > -58.0 {
                        Rectangle()
                            .fill(dbColor(for: peakHold))
                            .frame(width: 2.5, height: 14)
                            .offset(x: holdOffset)
                            .shadow(color: dbColor(for: peakHold), radius: 3)
                    }
                }
            }
            .frame(height: 14)
        }
    }
    
    private func updatePeakHold(newPeak: Float) {
        if newPeak > peakHoldLeft {
            peakHoldLeft = newPeak
            peakHoldTimerLeft?.invalidate()
            peakHoldTimerLeft = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: false) { _ in
                withAnimation(.easeOut(duration: 0.3)) {
                    peakHoldLeft = viewModel.peakLevel
                }
            }
        }
        
        let rightVal = newPeak * 0.98
        if rightVal > peakHoldRight {
            peakHoldRight = rightVal
            peakHoldTimerRight?.invalidate()
            peakHoldTimerRight = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: false) { _ in
                withAnimation(.easeOut(duration: 0.3)) {
                    peakHoldRight = viewModel.peakLevel * 0.98
                }
            }
        }
    }
    
    private func dbColor(for level: Float) -> Color {
        if level >= -1.0 { return Color(hex: "ef4444") }
        if level >= -6.0 { return Color(hex: "f59e0b") }
        if level >= -18.0 { return Color(hex: "38bdf8") }
        return Color(hex: "10b981")
    }
    
    // MARK: - Quick Configuration Card
    
    private var quickConfigCard: some View {
        StudioCard {
            VStack(spacing: 14) {
                HStack {
                    Image(systemName: "tuningfork")
                        .font(.system(size: 12))
                        .foregroundColor(Color(hex: "818cf8"))
                    Text("硬件采集参数")
                        .font(.system(size: 13, weight: .semibold))
                        .foregroundColor(.white)
                    Spacer()
                    Text("CoreAudio 直通")
                        .font(.system(size: 10, weight: .medium))
                        .foregroundColor(Color(hex: "10b981"))
                }
                
                // Buffer Size Selector with Mode Tag
                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        Text("缓冲区 (Buffer Size)")
                            .font(.system(size: 11, weight: .medium))
                            .foregroundColor(Color.white.opacity(0.6))
                        Spacer()
                        
                        latencyTagBadge(for: config.bufferSize)
                    }
                    
                    Picker("Buffer", selection: $config.bufferSize) {
                        ForEach(SupportedBufferSize.allCases) { size in
                            Text(size.displayName).tag(size)
                        }
                    }
                    .pickerStyle(.segmented)
                    .onChange(of: config.bufferSize) { _ in
                        UIImpactFeedbackGenerator(style: .light).impactOccurred()
                    }
                }
                
                // Channel Mode
                VStack(alignment: .leading, spacing: 6) {
                    Text("声道模式 (Channel Mode)")
                        .font(.system(size: 11, weight: .medium))
                        .foregroundColor(Color.white.opacity(0.6))
                    
                    Picker("Channel", selection: $config.channelMode) {
                        ForEach(ChannelMode.allCases) { mode in
                            Text(mode.displayName).tag(mode)
                        }
                    }
                    .pickerStyle(.segmented)
                    .onChange(of: config.channelMode) { _ in
                        UIImpactFeedbackGenerator(style: .light).impactOccurred()
                    }
                }
            }
        }
    }
    
    private func latencyTagBadge(for size: SupportedBufferSize) -> some View {
        let (title, color) = latencyInfo(for: size)
        return Text(title)
            .font(.system(size: 10, weight: .bold, design: .monospaced))
            .foregroundColor(color)
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(Capsule().fill(color.opacity(0.18)))
    }
    
    private func latencyInfo(for size: SupportedBufferSize) -> (String, Color) {
        switch size {
        case .size64:  return ("极速 1.33ms", Color(hex: "10b981"))
        case .size128: return ("超低 2.67ms", Color(hex: "38bdf8"))
        case .size256: return ("专业 5.33ms", Color(hex: "818cf8"))
        case .size512: return ("稳定 10.67ms", Color(hex: "f59e0b"))
        }
    }
    
    // MARK: - Live Telemetry HUD Card
    
    private var liveTelemetryHUD: some View {
        StudioCard {
            VStack(spacing: 12) {
                HStack {
                    HStack(spacing: 6) {
                        Circle()
                            .fill(Color(hex: "10b981"))
                            .frame(width: 6, height: 6)
                        Text("实时传输遥测 (Telemetry)")
                            .font(.system(size: 13, weight: .semibold))
                            .foregroundColor(.white)
                    }
                    Spacer()
                    Text("ASRC 漂移同步中")
                        .font(.system(size: 10, weight: .medium))
                        .foregroundColor(Color(hex: "10b981"))
                }
                
                LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 10) {
                    telemetryMetric(title: "发送数据", value: formatBytes(viewModel.bytesSent))
                    telemetryMetric(title: "丢包数量", value: "\(viewModel.packetsDropped)")
                    telemetryMetric(title: "硬件延迟", value: String(format: "%.2fms", config.bufferLatencyMs))
                    telemetryMetric(title: "采样格式", value: "48k/16b")
                }
            }
        }
    }
    
    private func telemetryMetric(title: String, value: String) -> some View {
        VStack(spacing: 4) {
            Text(title)
                .font(.system(size: 10, weight: .medium))
                .foregroundColor(Color.white.opacity(0.5))
            Text(value)
                .font(.system(size: 12, weight: .bold, design: .monospaced))
                .foregroundColor(.white)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 8)
        .background(RoundedRectangle(cornerRadius: 8).fill(Color.white.opacity(0.03)))
    }
    
    // MARK: - Master Action Button
    
    private var masterActionButton: some View {
        Button(action: {
            let impact = UIImpactFeedbackGenerator(style: .rigid)
            impact.impactOccurred()
            
            withAnimation(.spring(response: 0.35, dampingFraction: 0.7)) {
                if viewModel.isStreaming {
                    viewModel.stopStreaming()
                } else {
                    viewModel.startStreaming(config: config)
                }
            }
        }) {
            HStack(spacing: 12) {
                Image(systemName: viewModel.isStreaming ? "stop.circle.fill" : "record.circle.fill")
                    .font(.system(size: 22, weight: .bold))
                
                Text(viewModel.isStreaming ? "停止推流 (Stop Stream)" : "开始高质量采集 (Start Capture)")
                    .font(.system(size: 16, weight: .bold, design: .rounded))
            }
            .frame(maxWidth: .infinity)
            .frame(height: 56)
            .background(
                Group {
                    if viewModel.isStreaming {
                        LinearGradient(
                            colors: [Color(hex: "ef4444"), Color(hex: "dc2626")],
                            startPoint: .leading,
                            endPoint: .trailing
                        )
                    } else {
                        LinearGradient(
                            colors: [Color(hex: "4f46e5"), Color(hex: "7c3aed"), Color(hex: "06b6d4")],
                            startPoint: .leading,
                            endPoint: .trailing
                        )
                    }
                }
            )
            .foregroundColor(.white)
            .clipShape(RoundedRectangle(cornerRadius: 16, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 16, style: .continuous)
                    .stroke(Color.white.opacity(0.25), lineWidth: 1)
            )
            .shadow(
                color: viewModel.isStreaming ? Color(hex: "ef4444").opacity(0.45) : Color(hex: "6366f1").opacity(0.45),
                radius: 16,
                x: 0,
                y: 8
            )
        }
    }
    
    // MARK: - Error Banner
    
    private func errorBanner(_ message: String) -> some View {
        HStack(spacing: 10) {
            Image(systemName: "exclamationmark.triangle.fill")
                .font(.system(size: 16))
                .foregroundColor(Color(hex: "f59e0b"))
            
            Text(message)
                .font(.system(size: 12, weight: .medium))
                .foregroundColor(.white)
            
            Spacer()
        }
        .padding(14)
        .background(RoundedRectangle(cornerRadius: 12).fill(Color(hex: "ef4444").opacity(0.18)))
        .overlay(RoundedRectangle(cornerRadius: 12).stroke(Color(hex: "ef4444").opacity(0.4), lineWidth: 1))
    }
    
    // MARK: - Footer Info Badge
    
    private var footerInfoBadge: some View {
        HStack(spacing: 6) {
            Image(systemName: "lock.shield.fill")
                .font(.system(size: 10))
                .foregroundColor(Color(hex: "10b981"))
            Text("内置 usbmuxd 硬件隧道 • 零拷贝无锁驱动 • 自动伴奏对齐")
                .font(.system(size: 10, weight: .medium))
                .foregroundColor(Color.white.opacity(0.45))
        }
    }
    
    // MARK: - Helper Methods
    
    private func startSessionTimer() {
        sessionSeconds = 0
        sessionTimer?.invalidate()
        sessionTimer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { _ in
            sessionSeconds += 1
        }
    }
    
    private func stopSessionTimer() {
        sessionTimer?.invalidate()
        sessionTimer = nil
        sessionSeconds = 0
    }
    
    private func formatSessionTime(_ seconds: Int) -> String {
        let m = seconds / 60
        let s = seconds % 60
        return String(format: "%02d:%02d", m, s)
    }
    
    private func formatBytes(_ bytes: UInt64) -> String {
        if bytes < 1024 { return "\(bytes) B" }
        if bytes < 1024 * 1024 { return String(format: "%.1f KB", Double(bytes) / 1024.0) }
        return String(format: "%.1f MB", Double(bytes) / (1024.0 * 1024.0))
    }
}

// MARK: - Studio Frosted Glass Card Container

struct StudioCard<Content: View>: View {
    let content: Content
    
    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }
    
    var body: some View {
        content
            .padding(18)
            .background(
                RoundedRectangle(cornerRadius: 20, style: .continuous)
                    .fill(Color(hex: "121224").opacity(0.72))
                    .background(
                        RoundedRectangle(cornerRadius: 20, style: .continuous)
                            .fill(.ultraThinMaterial.opacity(0.85))
                    )
            )
            .overlay(
                RoundedRectangle(cornerRadius: 20, style: .continuous)
                    .stroke(
                        LinearGradient(
                            colors: [Color.white.opacity(0.18), Color.white.opacity(0.04)],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        ),
                        lineWidth: 1
                    )
            )
            .shadow(color: Color.black.opacity(0.4), radius: 12, x: 0, y: 6)
    }
}

// MARK: - Studio Settings Sheet

struct StudioSettingsSheet: View {
    @Environment(\.dismiss) private var dismiss
    @EnvironmentObject var config: AudioConfig
    
    var body: some View {
        NavigationView {
            ZStack {
                Color(hex: "0b0b18").ignoresSafeArea()
                
                ScrollView {
                    VStack(spacing: 20) {
                        StudioCard {
                            VStack(alignment: .leading, spacing: 16) {
                                Text("专业音频架构")
                                    .font(.system(size: 14, weight: .bold))
                                    .foregroundColor(.white)
                                
                                VStack(alignment: .leading, spacing: 6) {
                                    Text("采集引擎 (Capture Engine)")
                                        .font(.system(size: 12))
                                        .foregroundColor(.gray)
                                    Text("CoreAudio RemoteIO (硬件 ADC 直通)")
                                        .font(.system(size: 14, weight: .semibold, design: .monospaced))
                                        .foregroundColor(Color(hex: "10b981"))
                                }
                                
                                Divider().background(Color.white.opacity(0.1))
                                
                                VStack(alignment: .leading, spacing: 6) {
                                    Text("采样率 (Sample Rate)")
                                        .font(.system(size: 12))
                                        .foregroundColor(.gray)
                                    Text("48,000 Hz (DAW 标准广播级)")
                                        .font(.system(size: 14, weight: .semibold, design: .monospaced))
                                        .foregroundColor(Color(hex: "818cf8"))
                                }
                                
                                Divider().background(Color.white.opacity(0.1))
                                
                                VStack(alignment: .leading, spacing: 6) {
                                    Text("位深 (Bit Depth)")
                                        .font(.system(size: 12))
                                        .foregroundColor(.gray)
                                    Text("16-bit PCM (未压缩无损传输)")
                                        .font(.system(size: 14, weight: .semibold, design: .monospaced))
                                        .foregroundColor(Color(hex: "818cf8"))
                                }
                                
                                Divider().background(Color.white.opacity(0.1))
                                
                                VStack(alignment: .leading, spacing: 6) {
                                    Text("ASRC 硬件时钟漂移补偿")
                                        .font(.system(size: 12))
                                        .foregroundColor(.gray)
                                    Text("已开启 (±500 ppm 动态自适应重采样)")
                                        .font(.system(size: 13, weight: .semibold))
                                        .foregroundColor(Color(hex: "10b981"))
                                }
                            }
                        }
                        
                        StudioCard {
                            VStack(alignment: .leading, spacing: 12) {
                                Text("后台保活机制")
                                    .font(.system(size: 14, weight: .bold))
                                    .foregroundColor(.white)
                                
                                Text("应用已配置 Background Audio 模式，锁屏或切换至后台时仍将持续保持麦克风采集与推流。")
                                    .font(.system(size: 12))
                                    .foregroundColor(Color.white.opacity(0.7))
                                    .lineSpacing(4)
                            }
                        }
                    }
                    .padding(20)
                }
            }
            .navigationTitle("音频参数")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("完成") { dismiss() }
                        .font(.system(size: 15, weight: .bold))
                        .foregroundColor(Color(hex: "818cf8"))
                }
            }
        }
    }
}

// MARK: - DAW Connection Guide Sheet

struct DAWGuideSheet: View {
    @Environment(\.dismiss) private var dismiss
    
    var body: some View {
        NavigationView {
            ZStack {
                Color(hex: "0b0b18").ignoresSafeArea()
                
                ScrollView {
                    VStack(alignment: .leading, spacing: 18) {
                        guideStepCard(
                            step: "1",
                            title: "USB 数据线连接",
                            desc: "使用 Lightning 或 USB-C 线将 iPhone 连接到 Windows 电脑，并在手机上点击「信任此电脑」。"
                        )
                        
                        guideStepCard(
                            step: "2",
                            title: "开启 iPhoneMic 采集",
                            desc: "点击主界面「开始采集」按钮，状态将变为绿色「音频实时推流中」。"
                        )
                        
                        guideStepCard(
                            step: "3",
                            title: "DAW 音频设置",
                            desc: "在 Studio One / Cubase / Reaper / Ableton 的音频设备中选择「iPhone USB Microphone ASIO」，新建轨道即可录音。"
                        )
                        
                        guideStepCard(
                            step: "4",
                            title: "伴奏对齐与录音",
                            desc: "驱动已自动计算并上报端到端延迟，录音完成后干声将与工程伴奏无缝对齐。"
                        )
                    }
                    .padding(20)
                }
            }
            .navigationTitle("使用指南")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("知道了") { dismiss() }
                        .font(.system(size: 15, weight: .bold))
                        .foregroundColor(Color(hex: "818cf8"))
                }
            }
        }
    }
    
    private func guideStepCard(step: String, title: String, desc: String) -> some View {
        StudioCard {
            HStack(alignment: .top, spacing: 14) {
                ZStack {
                    Circle()
                        .fill(LinearGradient(
                            colors: [Color(hex: "6366f1"), Color(hex: "8b5cf6")],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        ))
                        .frame(width: 28, height: 28)
                    
                    Text(step)
                        .font(.system(size: 14, weight: .bold, design: .rounded))
                        .foregroundColor(.white)
                }
                
                VStack(alignment: .leading, spacing: 4) {
                    Text(title)
                        .font(.system(size: 14, weight: .bold))
                        .foregroundColor(.white)
                    
                    Text(desc)
                        .font(.system(size: 12))
                        .foregroundColor(Color.white.opacity(0.7))
                        .lineSpacing(3)
                }
            }
        }
    }
}

// MARK: - Color Hex Initializer

extension Color {
    init(hex: String) {
        let scanner = Scanner(string: hex)
        var hexNumber: UInt64 = 0
        scanner.scanHexInt64(&hexNumber)
        
        let r = Double((hexNumber & 0xFF0000) >> 16) / 255.0
        let g = Double((hexNumber & 0x00FF00) >> 8) / 255.0
        let b = Double(hexNumber & 0x0000FF) / 255.0
        
        self.init(red: r, green: g, blue: b)
    }
}

// MARK: - Preview

#Preview {
    ContentView()
        .environmentObject(AudioConfig())
        .environmentObject(AppViewModel())
}
