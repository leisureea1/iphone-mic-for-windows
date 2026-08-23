import { app, BrowserWindow, ipcMain } from 'electron'
import { join } from 'path'
import { BackendBridge } from './backend-bridge'

import { exec } from 'child_process'

let mainWindow: BrowserWindow | null = null
let backend: BackendBridge | null = null

const gotTheLock = app.requestSingleInstanceLock()
if (!gotTheLock) {
  app.quit()
} else {
  app.on('second-instance', () => {
    if (mainWindow) {
      if (mainWindow.isMinimized()) mainWindow.restore()
      mainWindow.focus()
    }
  })
}

function registerAppPath(): void {
  const exePath = process.execPath
  exec(`reg add "HKCU\\Software\\iPhoneMic" /v InstallPath /t REG_SZ /d "${exePath}" /f`, () => {})
}

function createWindow(): void {
  registerAppPath()
  mainWindow = new BrowserWindow({
    width: 1200,
    height: 800,
    minWidth: 900,
    minHeight: 600,
    backgroundColor: '#07090e',
    resizable: true,
    maximizable: true,
    fullscreenable: true,
    title: 'iPhoneMic Studio',
    webPreferences: {
      preload: join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
    },
  })

  // Hide the default menu bar
  mainWindow.setMenuBarVisibility(false)

  if (process.env['ELECTRON_RENDERER_URL']) {
    mainWindow.loadURL(process.env['ELECTRON_RENDERER_URL'])
  } else {
    mainWindow.loadFile(join(__dirname, '../renderer/index.html'))
  }
}

app.whenReady().then(() => {
  // Initialize the C++ backend
  backend = new BackendBridge()
  backend.init()

  // Register IPC handlers
  ipcMain.handle('get-devices', () => backend!.getDevices())
  ipcMain.handle('get-selected-device-index', () => backend!.getSelectedDeviceIndex())
  ipcMain.handle('set-output-device', (_event, index: number) => backend!.setOutputDevice(index))
  ipcMain.handle('set-monitor-audio', (_event, enable: boolean) => backend!.setMonitorAudio(enable))
  ipcMain.handle('get-monitor-audio', () => backend!.getMonitorAudio())
  ipcMain.handle('get-connection-status', () => backend!.getConnectionStatus())
  ipcMain.handle('get-audio-levels', () => backend!.getAudioLevels())
  ipcMain.handle('set-gain-percent', (_event, percent: number) => backend!.setGainPercent(percent))
  ipcMain.handle('set-muted', (_event, mute: boolean) => backend!.setMuted(mute))
  ipcMain.handle('set-high-pass-filter', (_event, enable: boolean) => backend!.setHighPassFilter(enable))
  ipcMain.handle('set-agc', (_event, enable: boolean) => backend!.setAGC(enable))
  ipcMain.handle('set-noise-gate', (_event, level: number) => backend!.setNoiseGate(level))
  ipcMain.handle('set-buffer-size', (_event, size: number) => backend!.setBufferSize(size))
  ipcMain.handle('get-dropped-frames', () => backend!.getDroppedFrames())
  ipcMain.handle('get-sample-rate', () => backend!.getSampleRate())
  ipcMain.handle('get-buffer-size', () => backend!.getBufferSize())

  createWindow()
})

app.on('window-all-closed', () => {
  if (backend) {
    backend.shutdown()
    backend = null
  }
  app.quit()
})
