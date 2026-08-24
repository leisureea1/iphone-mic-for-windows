// USBServer.swift
// iPhoneMic
//
// TCP server using Network.framework (NWListener) for USB communication.
// Listens on a configurable port. When iPhone is connected via USB,
// Windows uses usbmuxd/iproxy to tunnel TCP to this port.

import Foundation
import Network

final class USBServer: ObservableObject {
    
    // MARK: - Published State
    
    @Published var connectionState: ConnectionState = .disconnected
    @Published var bytesSent: UInt64 = 0
    @Published var packetsDropped: UInt64 = 0
    
    // MARK: - Configuration
    
    private let port: UInt16
    private let config: AudioConfig
    
    // MARK: - Network Objects & Concurrency
    
    private var listener: NWListener?
    private var activeConnections: [NWConnection] = []
    private let connectionLock = NSLock()
    private var heartbeatTimer: DispatchSourceTimer?
    
    // Queue for network listener operations
    private let networkQueue = DispatchQueue(
        label: "com.iphonemic.network",
        qos: .userInteractive
    )
    
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
            let parameters = NWParameters.tcp
            parameters.serviceClass = .interactiveVideo  // Low latency priority
            
            let nwPort = NWEndpoint.Port(rawValue: port)!
            listener = try NWListener(using: parameters, on: nwPort)
            
            listener?.stateUpdateHandler = { [weak self] state in
                DispatchQueue.main.async {
                    self?.handleListenerState(state)
                }
            }
            
            listener?.newConnectionHandler = { [weak self] connection in
                self?.handleNewConnection(connection)
            }
            
            listener?.start(queue: networkQueue)
            
            DispatchQueue.main.async {
                self.connectionState = .waitingForConnection
            }
            
            print("[USBServer] Listener started on port \(port)")
            
        } catch {
            DispatchQueue.main.async {
                self.connectionState = .error(message: "监听失败: \(error.localizedDescription)")
            }
            print("[USBServer] Failed to start listener: \(error)")
        }
    }
    
    /// Stop the TCP listener and disconnect
    func stopListening() {
        stopHeartbeat()
        
        connectionLock.lock()
        let conns = activeConnections
        activeConnections.removeAll()
        connectionLock.unlock()
        
        conns.forEach { $0.cancel() }
        
        listener?.cancel()
        listener = nil
        
        DispatchQueue.main.async {
            self.connectionState = .disconnected
            self.bytesSent = 0
            self.packetsDropped = 0
        }
        
        print("[USBServer] Server stopped")
    }
    
    /// Send audio PCM data to connected clients.
    /// Called directly from the high-priority audio capture callback.
    func sendAudioData(_ pcmData: Data) {
        connectionLock.lock()
        let conns = activeConnections
        connectionLock.unlock()
        
        guard !conns.isEmpty else { return }
        
        let packet = PacketBuilder.buildAudioPacket(pcmData: pcmData)
        let count = UInt64(packet.count)
        
        for connection in conns {
            connection.send(content: packet, completion: .contentProcessed { error in
                if let error = error {
                    print("[USBServer] Send error: \(error)")
                }
            })
        }
        
        DispatchQueue.main.async { [weak self] in
            self?.bytesSent += count
        }
    }
    
    /// Send current audio config to the client
    func sendConfig() {
        connectionLock.lock()
        let conns = activeConnections
        connectionLock.unlock()
        
        guard !conns.isEmpty else { return }
        
        Task { @MainActor in
            let configPacket = self.config.toConfigPacket()
            if let packet = PacketBuilder.buildConfigPacket(config: configPacket) {
                for connection in conns {
                    connection.send(content: packet, completion: .contentProcessed { _ in })
                }
            }
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
        connectionLock.lock()
        activeConnections.append(connection)
        connectionLock.unlock()
        
        let endpoint = connection.endpoint
        let peerName: String
        switch endpoint {
        case .hostPort(let host, let port):
            peerName = "\(host):\(port)"
        default:
            peerName = "Unknown"
        }
        
        connection.stateUpdateHandler = { [weak self] state in
            self?.handleConnectionState(state, connection: connection, peerName: peerName)
        }
        
        connection.start(queue: networkQueue)
        
        print("[USBServer] New connection from \(peerName)")
    }
    
    private func handleConnectionState(_ state: NWConnection.State, connection: NWConnection, peerName: String) {
        switch state {
        case .ready:
            print("[USBServer] Connection ready: \(peerName)")
            DispatchQueue.main.async {
                self.connectionState = .connected(peerName: peerName)
            }
            
            // Send initial config
            sendConfig()
            
            // Start heartbeat
            startHeartbeat()
            
            // Start receiving
            startReceiving(connection)
            
        case .failed(let error):
            print("[USBServer] Connection failed: \(error)")
            removeConnection(connection)
            
        case .cancelled:
            print("[USBServer] Connection cancelled: \(peerName)")
            removeConnection(connection)
            
        default:
            break
        }
    }
    
    private func removeConnection(_ connection: NWConnection) {
        connectionLock.lock()
        if let idx = activeConnections.firstIndex(where: { $0 === connection }) {
            activeConnections.remove(at: idx)
        }
        let isEmpty = activeConnections.isEmpty
        connectionLock.unlock()
        
        if isEmpty {
            DispatchQueue.main.async {
                self.connectionState = .waitingForConnection
            }
            stopHeartbeat()
        }
    }
    
    // MARK: - Private: Receiving
    
    private func startReceiving(_ connection: NWConnection) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self, weak connection] (data, context, isComplete, error) in
            guard let self = self, let connection = connection else { return }
            
            if let data = data, !data.isEmpty {
                self.handleReceivedData(data)
            }
            
            if isComplete {
                return
            }
            
            if error == nil {
                self.startReceiving(connection)
            }
        }
    }
    
    private func handleReceivedData(_ data: Data) {
        guard let header = PacketHeader.deserialize(from: data) else { return }
        
        switch header.type {
        case .configAck:
            print("[USBServer] Config acknowledged by client")
        case .config:
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
            guard let self = self else { return }
            self.connectionLock.lock()
            let conns = self.activeConnections
            self.connectionLock.unlock()
            
            guard !conns.isEmpty else { return }
            let packet = PacketBuilder.buildHeartbeatPacket()
            for conn in conns {
                conn.send(content: packet, completion: .contentProcessed { _ in })
            }
        }
        timer.resume()
        heartbeatTimer = timer
    }
    
    private func stopHeartbeat() {
        heartbeatTimer?.cancel()
        heartbeatTimer = nil
    }
}
