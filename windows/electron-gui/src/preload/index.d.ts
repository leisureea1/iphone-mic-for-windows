export interface ElectronAPI {
  getDevices(): Promise<string[]>
  getSelectedDeviceIndex(): Promise<number>
  setOutputDevice(index: number): Promise<void>
  setMonitorAudio(enable: boolean): Promise<void>
  getMonitorAudio(): Promise<boolean>
  getConnectionStatus(): Promise<boolean>
  getAudioLevels(): Promise<{ left: number; right: number }>
}

declare global {
  interface Window {
    electronAPI: ElectronAPI
  }
}
