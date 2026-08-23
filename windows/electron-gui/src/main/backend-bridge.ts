import koffi from 'koffi'
import { join } from 'path'
import { existsSync } from 'fs'
import { app } from 'electron'

export class BackendBridge {
  private loaded = false
  private backendInit?: () => void
  private backendShutdown?: () => void
  private backendGetOutputDevicesCSV?: () => string
  private backendGetSelectedDeviceIndex?: () => number
  private backendSetOutputDevice?: (index: number) => void
  private backendSetMonitorAudio?: (enable: boolean) => void
  private backendGetMonitorAudio?: () => boolean
  private backendGetConnectionStatus?: () => boolean
  private backendGetAudioLevels?: (left: Float32Array, right: Float32Array) => void
  private backendSetGainPercent?: (percent: number) => void
  private backendSetMuted?: (mute: boolean) => void
  private backendSetHighPassFilter?: (enable: boolean) => void
  private backendSetAGC?: (enable: boolean) => void
  private backendSetNoiseGate?: (level: number) => void
  private backendSetBufferSize?: (size: number) => void
  private backendGetDroppedFrames?: () => number
  private backendGetSampleRate?: () => number
  private backendGetBufferSize?: () => number

  constructor() {
    // Resolve DLL path: packaged vs development
    const dllPath = app.isPackaged
      ? join(process.resourcesPath, 'iphone_mic_backend.dll')
      : join(__dirname, '..', '..', '..', 'build', 'bin', 'Release', 'iphone_mic_backend.dll')

    if (!existsSync(dllPath)) {
      console.warn(`[BackendBridge] DLL not found at: ${dllPath}`)
      console.warn('[BackendBridge] Running in UI-only mode. Build the C++ backend first.')
      return
    }

    try {
      const lib = koffi.load(dllPath)

      // Bind all exported C functions from the backend DLL
      this.backendInit = lib.func('void Backend_Init()')
      this.backendShutdown = lib.func('void Backend_Shutdown()')
      this.backendGetOutputDevicesCSV = lib.func('const char* Backend_GetOutputDevicesCSV()')
      this.backendGetSelectedDeviceIndex = lib.func('int Backend_GetSelectedDeviceIndex()')
      this.backendSetOutputDevice = lib.func('void Backend_SetOutputDevice(int index)')
      this.backendSetMonitorAudio = lib.func('void Backend_SetMonitorAudio(bool enable)')
      this.backendGetMonitorAudio = lib.func('bool Backend_GetMonitorAudio()')
      this.backendGetConnectionStatus = lib.func('bool Backend_GetConnectionStatus()')
      this.backendGetAudioLevels = lib.func('void Backend_GetAudioLevels(float *left, float *right)')
      this.backendSetGainPercent = lib.func('void Backend_SetGainPercent(int percent)')
      this.backendSetMuted = lib.func('void Backend_SetMuted(bool mute)')
      this.backendSetHighPassFilter = lib.func('void Backend_SetHighPassFilter(bool enable)')
      this.backendSetAGC = lib.func('void Backend_SetAGC(bool enable)')
      this.backendSetNoiseGate = lib.func('void Backend_SetNoiseGate(int level)')
      this.backendSetBufferSize = lib.func('void Backend_SetBufferSize(int size)')
      this.backendGetDroppedFrames = lib.func('uint64_t Backend_GetDroppedFrames()')
      this.backendGetSampleRate = lib.func('int Backend_GetSampleRate()')
      this.backendGetBufferSize = lib.func('int Backend_GetBufferSize()')

      this.loaded = true
    } catch (err) {
      console.error('[BackendBridge] Failed to load DLL:', err)
    }
  }

  get isLoaded(): boolean {
    return this.loaded
  }

  init(): void {
    if (this.loaded) this.backendInit!()
  }

  shutdown(): void {
    if (this.loaded) this.backendShutdown!()
  }

  getDevices(): string[] {
    if (!this.loaded) return ['(Backend not loaded)']
    const csv = this.backendGetOutputDevicesCSV!()
    if (csv) {
      return csv.split('|').filter(Boolean)
    }
    return []
  }

  getSelectedDeviceIndex(): number {
    if (!this.loaded) return 0
    return this.backendGetSelectedDeviceIndex!()
  }

  setOutputDevice(index: number): void {
    if (this.loaded) this.backendSetOutputDevice!(index)
  }

  setMonitorAudio(enable: boolean): void {
    if (this.loaded) this.backendSetMonitorAudio!(enable)
  }

  getMonitorAudio(): boolean {
    if (!this.loaded) return false
    return this.backendGetMonitorAudio!()
  }

  getConnectionStatus(): boolean {
    if (!this.loaded) return false
    return this.backendGetConnectionStatus!()
  }

  getAudioLevels(): { left: number; right: number } {
    if (!this.loaded) return { left: 0, right: 0 }
    const leftBuf = new Float32Array(1)
    const rightBuf = new Float32Array(1)
    this.backendGetAudioLevels!(leftBuf, rightBuf)
    return { left: leftBuf[0], right: rightBuf[0] }
  }

  setGainPercent(percent: number): void {
    if (this.loaded) this.backendSetGainPercent!(percent)
  }

  setMuted(mute: boolean): void {
    if (this.loaded) this.backendSetMuted!(mute)
  }

  setHighPassFilter(enable: boolean): void {
    if (this.loaded) this.backendSetHighPassFilter!(enable)
  }

  setAGC(enable: boolean): void {
    if (this.loaded) this.backendSetAGC!(enable)
  }

  setNoiseGate(level: number): void {
    if (this.loaded) this.backendSetNoiseGate!(level)
  }

  setBufferSize(size: number): void {
    if (this.loaded) this.backendSetBufferSize!(size)
  }

  getDroppedFrames(): number {
    if (!this.loaded) return 0
    return this.backendGetDroppedFrames!()
  }

  getSampleRate(): number {
    if (!this.loaded) return 0
    return this.backendGetSampleRate!()
  }

  getBufferSize(): number {
    if (!this.loaded) return 0
    return this.backendGetBufferSize!()
  }
}
