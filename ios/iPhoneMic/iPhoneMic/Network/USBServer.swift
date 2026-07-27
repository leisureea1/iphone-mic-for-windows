// USBServer.swift
// iPhoneMic
//
// TCP server using Network.framework (NWListener) for USB communication.
// Listens on a configurable port. When iPhone is connected via USB,
// Windows uses usbmuxd/iproxy to tunnel TCP to this port.

import Foundation
import Network

@MainActor
final class USBServer: ObservableObject {
    
    // MARK: - Published State
    
    @Published var connectionState: ConnectionState = .disconnected
    @Published var bytesSent: UInt64 = 0
    @Published var packetsDropped: UInt64 = 0
    
    // MARK: - Configuration
    
    private let port: UInt16
    private let config: AudioConfig
    
    // MARK: - Network Objects
    
    private var listener: NWListener?
    private var activeConnections: [NWConnection] = []
    private var heartbeatTimer: DispatchSourceTimer?
    
    // Queue for network operations
    private let networkQueue = DispatchQueue(
        label: "com.iphonemic.network",
        qos: .userInteractive
    )
    
    // Send control
    private var isSending = false
    private var pendingData: [Data] = []
    private let maxPendingPackets = 8  // Drop packets if too many pending
    
    // MARK: - Init
    
    init(port: UInt16 = kDefaultPort, config: AudioConfig) {
        self.port = port
        self.config = config
    }
    
    // MARK: - Public API
    
    /// Start the TCP listener
    func startListening() {
        guard listener == nil else { return }
        
        do {
            // Configure TCP parameters for low latency
            let parameters = NWParameters.tcp
            parameters.serviceClass = .interactiveVideo  // Low latency priority
            
            // Set TCP no-delay (disable Nagle's algorithm)
            // TCP_NODELAY is enabled by default in NWParameters.tcp
            
            let nwPort = NWEndpoint.Port(rawValue: port)!
            listener = try NWListener(using: parameters, on: nwPort)
            
            listener?.stateUpdateHandler = { [weak self] state in
                DispatchQueue.main.async {
                    self?.handleListenerState(state)
                }
            }
            
            listener?.newConnectionHandler = { [weak self] connection in
                DispatchQueue.main.async {
                    self?.handleNewConnection(connection)
                }
            }
            
            listener?.start(queue: networkQueue)
            connectionState = .waitingForConnection
            
            print("[USBServer] Listener started on port \(port)")
            
        } catch {
            connectionState = .error(message: "监听失败: \(error.localizedDescription)")
            print("[USBServer] Failed to start listener: \(error)")
        }
    }
    
    /// Stop the TCP listener and disconnect
    func stopListening() {
        stopHeartbeat()
        
        activeConnections.forEach { $0.cancel() }
        activeConnections.removeAll()
        
        listener?.cancel()
        listener = nil
        
        connectionState = .disconnected
        bytesSent = 0
        packetsDropped = 0
        isSending = false
        pendingData.removeAll()
        
        print("[USBServer] Server stopped")
    }
    
    /// Send audio PCM data to the connected client.
    /// Called from the audio capture callback.
    func sendAudioData(_ pcmData: Data) {
        guard !activeConnections.isEmpty else { return }
        
        let packet = PacketBuilder.buildAudioPacket(pcmData: pcmData)
        enqueueAndSend(packet)
    }
    
    /// Send current audio config to the client
    func sendConfig() {
        guard !activeConnections.isEmpty else { return }
        
        let configPacket = config.toConfigPacket()
        if let packet = PacketBuilder.buildConfigPacket(config: configPacket) {
            enqueueAndSend(packet)
        }
    }
    
    // MARK: - Private: Connection Handling
    
    private func handleListenerState(_ state: NWListener.State) {
        switch state {
        case .ready:
            print("[USBServer] Listener ready on port \(port)")
            connectionState = .waitingForConnection
            
        case .failed(let error):
            print("[USBServer] Listener failed: \(error)")
            connectionState = .error(message: error.localizedDescription)
            
            // Try to restart after a delay
            DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { [weak self] in
                self?.listener?.cancel()
                self?.listener = nil
                self?.startListening()
            }
            
        case .cancelled:
            print("[USBServer] Listener cancelled")
            
        default:
            break
        }
    }
    
    private func handleNewConnection(_ connection: NWConnection) {
        activeConnections.append(connection)
        
        let endpoint = connection.endpoint
        let peerName: String
        switch endpoint {
        case .hostPort(let host, let port):
            peerName = "\(host):\(port)"
        default:
            peerName = "Unknown"
        }
        
        connection.stateUpdateHandler = { [weak self] state in
            DispatchQueue.main.async {
                self?.handleConnectionState(state, peerName: peerName)
            }
        }
        
        connection.start(queue: networkQueue)
        
        print("[USBServer] New connection from \(peerName)")
    }
    
    private func handleConnectionState(_ state: NWConnection.State, peerName: String) {
        switch state {
        case .ready:
            print("[USBServer] Connection ready: \(peerName)")
            connectionState = .connected(peerName: peerName)
            isSending = false
            pendingData.removeAll()
            
            // Send initial config
            sendConfig()
            
            // Start heartbeat
            startHeartbeat()
            
            // Start receiving (for config ACK, etc.)
            if let conn = activeConnections.first(where: { "\($0.endpoint)" == peerName }) {
                startReceiving(conn)
            }
            
        case .failed(let error):
            print("[USBServer] Connection failed: \(error)")
            if let idx = activeConnections.firstIndex(where: { "\($0.endpoint)" == peerName }) {
                activeConnections.remove(at: idx)
            }
            if activeConnections.isEmpty {
                connectionState = .waitingForConnection
                stopHeartbeat()
            }
            
        case .cancelled:
            print("[USBServer] Connection cancelled")
            if let idx = activeConnections.firstIndex(where: { "\($0.endpoint)" == peerName }) {
                activeConnections.remove(at: idx)
            }
            if activeConnections.isEmpty {
                connectionState = .waitingForConnection
                stopHeartbeat()
            }
            
        default:
            break
        }
    }
    
    // MARK: - Private: Data Sending
    
    private func enqueueAndSend(_ data: Data) {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            
            guard !self.activeConnections.isEmpty else { return }
            
            // Send directly to all connections. Network.framework handles internal buffering.
            // For real-time audio, if a client is completely stalled, the OS will eventually
            // drop the connection. This avoids stalling all other fast clients.
            for connection in self.activeConnections {
                connection.send(content: data, completion: .contentProcessed { error in
                    if let error = error {
                        print("[USBServer] Send error: \(error)")
                    }
                })
            }
            
            self.bytesSent += UInt64(data.count)
        }
    }
    
    // MARK: - Private: Receiving
    
    private func startReceiving(_ connection: NWConnection) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] (data, context, isComplete, error) in
            
            if let data = data, !data.isEmpty {
                DispatchQueue.main.async { [weak self] in
                    // Parse incoming packets (config changes, etc.)
                    self?.handleReceivedData(data)
                }
            }
            
            if isComplete {
                return
            }
            
            if error == nil {
                // Continue receiving
                DispatchQueue.main.async { [weak self] in
                    self?.startReceiving(connection)
                }
            }
        }
    }
    
    private func handleReceivedData(_ data: Data) {
        // Parse packet header
        guard let header = PacketHeader.deserialize(from: data) else { return }
        
        switch header.type {
        case .configAck:
            print("[USBServer] Config acknowledged by client")
        case .config:
            // Client requesting config change - handle if needed
            print("[USBServer] Received config request from client")
        default:
            break
        }
    }
    
    // MARK: - Private: Heartbeat
    
    private func startHeartbeat() {
        stopHeartbeat()
        
        let timer = DispatchSource.makeTimerSource(queue: networkQueue)
        timer.schedule(deadline: .now() + kHeartbeatInterval,
                      repeating: kHeartbeatInterval)
        timer.setEventHandler { [weak self] in
            guard let self = self, !self.activeConnections.isEmpty else { return }
            let packet = PacketBuilder.buildHeartbeatPacket()
            self.enqueueAndSend(packet)
        }
        timer.resume()
        heartbeatTimer = timer
    }
    
    private func stopHeartbeat() {
        heartbeatTimer?.cancel()
        heartbeatTimer = nil
    }
}
