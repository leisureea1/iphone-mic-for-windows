/// <reference types="vite/client" />

interface Window {
  electronAPI?: {
    getDevices: () => Promise<string[]>
    getSelectedDeviceIndex: () => Promise<number>
    setOutputDevice: (index: number) => Promise<void>
    setMonitorAudio: (enable: boolean) => Promise<void>
    getMonitorAudio: () => Promise<boolean>
    getConnectionStatus: () => Promise<boolean>
    getAudioLevels: () => Promise<{ left: number; right: number }>
    setGainPercent: (percent: number) => Promise<void>
    setMuted: (mute: boolean) => Promise<void>
    setHighPassFilter: (enable: boolean) => Promise<void>
    setAGC: (enable: boolean) => Promise<void>
    setNoiseGate: (level: number) => Promise<void>
    setBufferSize: (size: number) => Promise<void>
    getDroppedFrames: () => Promise<number>
    getSampleRate: () => Promise<number>
    getBufferSize: () => Promise<number>
  }
}
