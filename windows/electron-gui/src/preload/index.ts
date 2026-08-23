import { contextBridge, ipcRenderer } from 'electron'

const electronAPI = {
  getDevices: (): Promise<string[]> => ipcRenderer.invoke('get-devices'),
  getSelectedDeviceIndex: (): Promise<number> => ipcRenderer.invoke('get-selected-device-index'),
  setOutputDevice: (index: number): Promise<void> => ipcRenderer.invoke('set-output-device', index),
  setMonitorAudio: (enable: boolean): Promise<void> => ipcRenderer.invoke('set-monitor-audio', enable),
  getMonitorAudio: (): Promise<boolean> => ipcRenderer.invoke('get-monitor-audio'),
  getConnectionStatus: (): Promise<boolean> => ipcRenderer.invoke('get-connection-status'),
  getAudioLevels: (): Promise<{ left: number; right: number }> => ipcRenderer.invoke('get-audio-levels'),
  setGainPercent: (percent: number): Promise<void> => ipcRenderer.invoke('set-gain-percent', percent),
  setMuted: (mute: boolean): Promise<void> => ipcRenderer.invoke('set-muted', mute),
  setHighPassFilter: (enable: boolean): Promise<void> => ipcRenderer.invoke('set-high-pass-filter', enable),
  setAGC: (enable: boolean): Promise<void> => ipcRenderer.invoke('set-agc', enable),
  setNoiseGate: (level: number): Promise<void> => ipcRenderer.invoke('set-noise-gate', level),
  setBufferSize: (size: number): Promise<void> => ipcRenderer.invoke('set-buffer-size', size),
  getDroppedFrames: (): Promise<number> => ipcRenderer.invoke('get-dropped-frames'),
  getSampleRate: (): Promise<number> => ipcRenderer.invoke('get-sample-rate'),
  getBufferSize: (): Promise<number> => ipcRenderer.invoke('get-buffer-size'),
}

contextBridge.exposeInMainWorld('electronAPI', electronAPI)
