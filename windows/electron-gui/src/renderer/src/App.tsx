import { useState, useEffect, useRef } from "react";
import {
  Mic,
  Smartphone,
  Usb,
  Volume2,
  VolumeX,
  Speaker,
  Headphones,
  Radio,
  RefreshCw,
  Zap,
  ShieldCheck,
  CheckCircle2,
  AlertTriangle,
  HelpCircle,
  BarChart3,
  Layers,
  SlidersHorizontal,
  Sun,
  Moon,
  Palette,
  Play,
  Square,
  Gauge,
  Cpu,
} from "lucide-react";

type ConnectionState = "disconnected" | "connecting" | "connected" | "error";
type ThemeName = "obsidian" | "studio" | "synth" | "titanium";
type ColorMode = "dark" | "light";

const THEMES: { id: ThemeName; name: string }[] = [
  { id: "obsidian", name: "Cyber Obsidian (翡翠)" },
  { id: "studio", name: "Studio Gold (琥珀)" },
  { id: "synth", name: "Neon Synth (霓虹)" },
  { id: "titanium", name: "Minimal Titanium (钛金)" },
];

function getDeviceIcon(name: string) {
  const lower = name.toLowerCase();
  if (lower.includes("usb") || lower.includes("dac") || lower.includes("fiio")) return Usb;
  if (lower.includes("headphone") || lower.includes("ear") || lower.includes("耳机") || lower.includes("airpods")) return Headphones;
  if (lower.includes("speaker") || lower.includes("扬声器") || lower.includes("realtek")) return Speaker;
  return Radio;
}

function OscilloscopeCanvas({ active, level, isLight }: { active: boolean; level: number; isLight: boolean }) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    let animId = 0;
    let phase = 0;

    const render = () => {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.lineWidth = 2;
      ctx.strokeStyle = active ? (isLight ? "#059669" : "#10b981") : isLight ? "#cbd5e1" : "#334155";
      ctx.shadowBlur = active ? 8 : 0;
      ctx.shadowColor = isLight ? "#059669" : "#10b981";

      ctx.beginPath();
      const width = canvas.width;
      const height = canvas.height;
      const centerY = height / 2;
      const amp = active ? Math.max(0.1, level * 2.5) : 0;

      for (let x = 0; x < width; x++) {
        if (!active || amp < 0.01) {
          ctx.lineTo(x, centerY + (Math.random() - 0.5) * 1.5);
        } else {
          const y =
            centerY +
            Math.sin(x * 0.04 + phase) * 22 * amp * Math.sin(x * 0.01) +
            Math.cos(x * 0.08 - phase * 1.5) * 10 * amp +
            (Math.random() - 0.5) * 2;
          if (x === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
      }
      ctx.stroke();

      if (active) {
        phase += 0.15;
      }
      animId = requestAnimationFrame(render);
    };

    render();
    return () => cancelAnimationFrame(animId);
  }, [active, level, isLight]);

  return (
    <div className={`relative w-full h-24 rounded-lg overflow-hidden border p-2 ${isLight ? "bg-slate-100/80 border-slate-300" : "bg-black/40 border-white/5"}`}>
      <canvas ref={canvasRef} width={500} height={80} className="w-full h-full block" />
      <div className={`absolute top-2 left-3 font-mono text-[10px] flex items-center gap-2 ${isLight ? "text-slate-600" : "text-slate-400"}`}>
        <span className="w-1.5 h-1.5 rounded-full bg-emerald-500 animate-pulse" />
        实时波形示波器 (Live Oscilloscope)
      </div>
      <div className={`absolute bottom-2 right-3 font-mono text-[9px] ${isLight ? "text-slate-400" : "text-slate-500"}`}>
        SCALE: 5ms/div · 1.0V/div
      </div>
    </div>
  );
}

function DualStereoMeter({
  active,
  levelL,
  levelR,
  visMode,
  isLight
}: {
  active: boolean;
  levelL: number;
  levelR: number;
  visMode: "bars" | "waveform";
  isLight: boolean;
}) {
  const maxLevel = Math.max(levelL, levelR);
  const dbL = levelL > 0.001 ? Math.round(20 * Math.log10(levelL)) : -60;
  const dbR = levelR > 0.001 ? Math.round(20 * Math.log10(levelR)) : -60;
  const isClipping = maxLevel > 0.95;

  if (visMode === "waveform") {
    return <OscilloscopeCanvas active={active} level={maxLevel} isLight={isLight} />;
  }

  const numBands = 24;

  return (
    <div className={`flex flex-col gap-2.5 p-3.5 rounded-xl border ${isLight ? "bg-slate-100/70 border-slate-200" : "bg-black/30 border-white/5"}`}>
      {/* Channel L */}
      <div className="flex items-center gap-2">
        <span className={`font-mono text-[10px] font-bold w-4 shrink-0 ${isLight ? "text-slate-600" : "text-slate-400"}`}>L</span>
        <div className={`flex-1 flex items-end gap-[2px] h-8 p-1 rounded-md border ${isLight ? "bg-slate-200/80 border-slate-300" : "bg-black/40 border-white/5"}`}>
          {Array.from({ length: numBands }).map((_, i) => {
            const threshold = i / numBands;
            const isLit = active && levelL > threshold;
            const isRed = i >= 20;
            const isAmber = i >= 15 && i < 20;
            const barHeight = isLit ? Math.min(100, Math.max(15, (levelL - threshold) * 200 + 30)) : 8;

            return (
              <div
                key={`l-${i}`}
                className={`flex-1 rounded-xs transition-all duration-75 ${
                  isLit
                    ? isRed
                      ? "bg-rose-500 shadow-[0_0_8px_rgba(244,63,94,0.6)]"
                      : isAmber
                      ? "bg-amber-400 shadow-[0_0_6px_rgba(251,191,36,0.5)]"
                      : "bg-emerald-500 shadow-[0_0_5px_rgba(16,185,129,0.4)]"
                    : isLight
                    ? "bg-slate-300"
                    : "bg-slate-800/50"
                }`}
                style={{ height: `${barHeight}%` }}
              />
            );
          })}
        </div>
        <span className={`font-mono text-[10px] w-12 text-right shrink-0 ${active ? "text-emerald-600 dark:text-emerald-400 font-semibold" : isLight ? "text-slate-400" : "text-slate-600"}`}>
          {active ? `${dbL} dB` : "-60 dB"}
        </span>
      </div>

      {/* Channel R */}
      <div className="flex items-center gap-2">
        <span className={`font-mono text-[10px] font-bold w-4 shrink-0 ${isLight ? "text-slate-600" : "text-slate-400"}`}>R</span>
        <div className={`flex-1 flex items-end gap-[2px] h-8 p-1 rounded-md border ${isLight ? "bg-slate-200/80 border-slate-300" : "bg-black/40 border-white/5"}`}>
          {Array.from({ length: numBands }).map((_, i) => {
            const threshold = i / numBands;
            const isLit = active && levelR > threshold;
            const isRed = i >= 20;
            const isAmber = i >= 15 && i < 20;
            const barHeight = isLit ? Math.min(100, Math.max(15, (levelR - threshold) * 200 + 30)) : 8;

            return (
              <div
                key={`r-${i}`}
                className={`flex-1 rounded-xs transition-all duration-75 ${
                  isLit
                    ? isRed
                      ? "bg-rose-500 shadow-[0_0_8px_rgba(244,63,94,0.6)]"
                      : isAmber
                      ? "bg-amber-400 shadow-[0_0_6px_rgba(251,191,36,0.5)]"
                      : "bg-emerald-500 shadow-[0_0_5px_rgba(16,185,129,0.4)]"
                    : isLight
                    ? "bg-slate-300"
                    : "bg-slate-800/50"
                }`}
                style={{ height: `${barHeight}%` }}
              />
            );
          })}
        </div>
        <span className={`font-mono text-[10px] w-12 text-right shrink-0 ${active ? "text-emerald-600 dark:text-emerald-400 font-semibold" : isLight ? "text-slate-400" : "text-slate-600"}`}>
          {active ? `${dbR} dB` : "-60 dB"}
        </span>
      </div>

      {/* dB Scale indicators */}
      <div className={`flex justify-between px-6 font-mono text-[9px] pt-0.5 ${isLight ? "text-slate-500" : "text-slate-500"}`}>
        <span>-60</span>
        <span>-48</span>
        <span>-36</span>
        <span>-24</span>
        <span>-12</span>
        <span>-6</span>
        <span className={isClipping ? "text-rose-500 font-bold animate-ping" : isLight ? "text-slate-600" : "text-slate-400"}>0 dB CLIP</span>
      </div>
    </div>
  );
}

function PhoneDeviceGraphic({ state, isLight }: { state: ConnectionState; isLight: boolean }) {
  return (
    <div className="relative flex flex-col items-center justify-center py-6 px-4">
      {/* Background glow circle */}
      <div
        className={`absolute w-44 h-44 rounded-full blur-3xl transition-all duration-700 pointer-events-none ${
          state === "connected"
            ? isLight
              ? "bg-emerald-300/40 scale-110"
              : "bg-emerald-500/20 scale-110"
            : state === "connecting"
            ? "bg-amber-500/20 animate-pulse"
            : state === "error"
            ? "bg-rose-500/20"
            : "bg-slate-500/10"
        }`}
      />

      {/* iPhone hardware frame */}
      <div
        className={`relative w-28 h-52 rounded-[28px] border-[3px] p-2 transition-all duration-500 flex flex-col justify-between items-center shadow-2xl ${
          state === "connected"
            ? "border-emerald-500 bg-slate-950 shadow-[0_0_35px_rgba(16,185,129,0.3)]"
            : state === "connecting"
            ? "border-amber-400 bg-slate-950 shadow-[0_0_25px_rgba(245,158,11,0.25)]"
            : state === "error"
            ? "border-rose-500 bg-slate-950 shadow-[0_0_25px_rgba(244,63,94,0.2)]"
            : "border-slate-700 bg-slate-900"
        }`}
      >
        {/* Dynamic Island */}
        <div className="w-12 h-3.5 bg-black rounded-full flex items-center justify-between px-1.5 border border-white/10 z-10">
          <div className="w-1.5 h-1.5 rounded-full bg-slate-700" />
          <div className="w-1.5 h-1.5 rounded-full bg-emerald-500/80" />
        </div>

        {/* Screen Content */}
        <div className="flex-1 w-full flex flex-col items-center justify-center gap-2 py-2">
          {state === "connected" && (
            <div className="flex flex-col items-center gap-1.5">
              <div className="relative">
                <div className="w-10 h-10 rounded-full bg-emerald-500/15 border border-emerald-500/40 flex items-center justify-center">
                  <Mic className="w-5 h-5 text-emerald-400 animate-pulse" />
                </div>
                <span className="absolute -top-1 -right-1 flex h-3 w-3">
                  <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-emerald-400 opacity-75" />
                  <span className="relative inline-flex rounded-full h-3 w-3 bg-emerald-500" />
                </span>
              </div>
              <span className="font-mono text-[9px] font-bold text-emerald-400 tracking-wider">
                AUDIO LIVE
              </span>
              <span className="font-mono text-[8px] text-slate-300">48kHz · 16-bit</span>
            </div>
          )}

          {state === "connecting" && (
            <div className="flex flex-col items-center gap-2">
              <RefreshCw className="w-6 h-6 text-amber-400 animate-spin" />
              <span className="font-mono text-[8px] text-amber-300 font-medium text-center">
                握手建立中…
              </span>
            </div>
          )}

          {state === "disconnected" && (
            <div className="flex flex-col items-center gap-1.5 opacity-60">
              <Smartphone className="w-7 h-7 text-slate-400" />
              <span className="font-mono text-[8px] text-slate-400">等待 USB 连接</span>
            </div>
          )}

          {state === "error" && (
            <div className="flex flex-col items-center gap-1.5 text-rose-400">
              <AlertTriangle className="w-6 h-6" />
              <span className="font-mono text-[8px] text-center font-medium">权限/线缆异常</span>
            </div>
          )}
        </div>

        {/* Bottom USB-C Port */}
        <div className="w-7 h-1.5 rounded-full bg-slate-800 border border-white/10 flex items-center justify-center">
          <div className="w-4 h-[2px] bg-slate-600 rounded-full" />
        </div>
      </div>

      {/* USB Cable & Connector */}
      <div className="relative flex flex-col items-center">
        {/* Connector head */}
        <div
          className={`w-5 h-3 rounded-t-sm border-t border-x transition-colors duration-500 ${
            state === "connected"
              ? "bg-slate-800 border-emerald-500/50"
              : state === "connecting"
              ? "bg-slate-800 border-amber-400/50"
              : "bg-slate-800 border-slate-600"
          }`}
        />
        {/* Braided Cable Line */}
        <div className="relative w-[3px] h-10 bg-slate-400/50 dark:bg-slate-800 overflow-hidden rounded-full">
          {state === "connected" && (
            <div className="absolute inset-0 bg-gradient-to-b from-emerald-400 via-teal-300 to-emerald-500 animate-pulse" />
          )}
          {state === "connecting" && (
            <div className="absolute inset-0 bg-amber-400/80 animate-ping" />
          )}
        </div>
        <div className={`w-8 h-2 rounded-b-md border-b border-x flex items-center justify-center ${isLight ? "bg-slate-200 border-slate-300" : "bg-slate-800 border-slate-700"}`}>
          <Usb className={`w-3 h-3 ${isLight ? "text-slate-600" : "text-slate-400"}`} />
        </div>
      </div>
    </div>
  );
}

export default function App() {
  const [colorMode, setColorMode] = useState<ColorMode>("dark");
  const [theme, setTheme] = useState<ThemeName>("obsidian");
  const [connState, setConnState] = useState<ConnectionState>("disconnected");
  const [devices, setDevices] = useState<string[]>([]);
  const [selectedDeviceIndex, setSelectedDeviceIndex] = useState(0);
  const [sidetoneEnabled, setSidetoneEnabled] = useState(false);
  const [audioLevels, setAudioLevels] = useState({ left: 0, right: 0 });
  const [gain, setGain] = useState(50);
  const [isMuted, setIsMuted] = useState(false);
  const [noiseSuppression, setNoiseSuppression] = useState<"off" | "low" | "medium" | "high">("medium");
  const [highPassFilter, setHighPassFilter] = useState(true);
  const [autoGain, setAutoGain] = useState(true);
  const [sampleRate, setSampleRate] = useState(0);
  const [bufferSize, setBufferSize] = useState(0);
  const [visMode, setVisMode] = useState<"bars" | "waveform">("bars");
  const [latency, setLatency] = useState(2.8);
  const [droppedFrames, setDroppedFrames] = useState(0);
  const [testTonePlaying, setTestTonePlaying] = useState(false);
  const [activeTab, setActiveTab] = useState<"dashboard" | "pipeline" | "setup">("dashboard");

  const connected = connState === "connected";
  const isLight = colorMode === "light";

  // Initial load from real backend
  const refreshDevices = async () => {
    try {
      if (window.electronAPI) {
        const [devs, selIdx, monitor] = await Promise.all([
          window.electronAPI.getDevices(),
          window.electronAPI.getSelectedDeviceIndex(),
          window.electronAPI.getMonitorAudio(),
        ]);
        if (devs && devs.length > 0) setDevices(devs);
        setSelectedDeviceIndex(selIdx);
        setSidetoneEnabled(monitor);
      }
    } catch (e) {
      console.warn("Backend not available or failed to load:", e);
    }
  };

  useEffect(() => {
    refreshDevices();
  }, []);

  // 30fps polling for real connection status, audio levels, dropped frames, and engine telemetry
  useEffect(() => {
    const interval = window.setInterval(async () => {
      try {
        if (window.electronAPI) {
          const [isConnected, levels, dropped, sr, bs] = await Promise.all([
            window.electronAPI.getConnectionStatus(),
            window.electronAPI.getAudioLevels(),
            window.electronAPI.getDroppedFrames(),
            window.electronAPI.getSampleRate(),
            window.electronAPI.getBufferSize(),
          ]);
          setConnState(isConnected ? "connected" : "disconnected");
          setAudioLevels({
            left: levels.left,
            right: levels.right,
          });
          if (typeof dropped === "number") {
            setDroppedFrames(dropped);
          }
          if (typeof sr === "number" && sr > 0) {
            setSampleRate(sr);
          } else if (!isConnected) {
            setSampleRate(0);
          }
          if (typeof bs === "number" && bs > 0) {
            setBufferSize(bs);
          }
        }
      } catch {
        // Fallback / ignore
      }
    }, 33);

    return () => clearInterval(interval);
  }, []);

  // Telemetry: compute real latency from actual sample rate and buffer size
  useEffect(() => {
    if (sampleRate > 0 && bufferSize > 0) {
      // End-to-end latency = (bufferSize / sampleRate) * 1000 ms
      const bufferLatencyMs = (bufferSize / sampleRate) * 1000.0;
      setLatency(Math.round(bufferLatencyMs * 10) / 10);
    } else {
      setLatency(0);
    }
  }, [bufferSize, sampleRate]);

  const handleDeviceSelect = (index: number) => {
    setSelectedDeviceIndex(index);
    if (window.electronAPI) {
      window.electronAPI.setOutputDevice(index);
    }
  };

  const handleSidetoneToggle = (enable: boolean) => {
    setSidetoneEnabled(enable);
    if (window.electronAPI) {
      window.electronAPI.setMonitorAudio(enable);
    }
  };

  const handleGainChange = (newGain: number) => {
    setGain(newGain);
    if (window.electronAPI) {
      window.electronAPI.setGainPercent(newGain);
    }
  };

  const handleMuteToggle = () => {
    const newMuted = !isMuted;
    setIsMuted(newMuted);
    if (window.electronAPI) {
      window.electronAPI.setMuted(newMuted);
    }
  };

  const handleHPFToggle = () => {
    const newHPF = !highPassFilter;
    setHighPassFilter(newHPF);
    if (window.electronAPI) {
      window.electronAPI.setHighPassFilter(newHPF);
    }
  };

  const handleAGCToggle = () => {
    const newAGC = !autoGain;
    setAutoGain(newAGC);
    if (window.electronAPI) {
      window.electronAPI.setAGC(newAGC);
    }
  };

  const handleNoiseGateChange = (level: "off" | "low" | "medium" | "high") => {
    setNoiseSuppression(level);
    const map = { off: 0, low: 1, medium: 2, high: 3 };
    if (window.electronAPI) {
      window.electronAPI.setNoiseGate(map[level]);
    }
  };

  const handleBufferSizeChange = (size: number) => {
    setBufferSize(size);
    if (window.electronAPI) {
      window.electronAPI.setBufferSize(size);
    }
  };

  const currentDeviceName = devices[selectedDeviceIndex] || "系统默认输出 (System Default)";

  return (
    <div className={`min-h-screen mode-${colorMode} theme-${theme} transition-colors duration-300 flex flex-col font-sans selection:bg-emerald-500/30 ${isLight ? "text-slate-900 bg-slate-50" : "text-slate-100 bg-slate-950"}`}>
      {/* Top Header Bar */}
      <header className={`px-6 py-3.5 border-b sticky top-0 z-50 flex items-center justify-between backdrop-blur-md ${isLight ? "bg-white/80 border-slate-200" : "bg-black/40 border-white/10"}`}>
        <div className="flex items-center gap-4">
          <div className="flex items-center gap-3">
            <div className={`w-9 h-9 rounded-xl flex items-center justify-center border ${isLight ? "bg-emerald-50 border-emerald-300 text-emerald-600" : "bg-emerald-500/10 border-emerald-500/30 text-emerald-400 shadow-[0_0_15px_rgba(16,185,129,0.2)]"}`}>
              <Mic className="w-5 h-5" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h1 className={`font-bold text-sm tracking-tight font-heading ${isLight ? "text-slate-900" : "text-white"}`}>iPhoneMic Studio</h1>
                <span className={`px-1.5 py-0.5 rounded text-[10px] font-mono font-semibold border ${isLight ? "bg-emerald-100 text-emerald-800 border-emerald-300" : "bg-emerald-500/20 text-emerald-300 border-emerald-500/30"}`}>
                  ASIO PRO
                </span>
              </div>
              <p className={`text-[11px] font-mono ${isLight ? "text-slate-500" : "text-slate-400"}`}>USB Low-Latency ASIO Driver Control Panel</p>
            </div>
          </div>

          <div className={`hidden md:flex items-center gap-1 p-1 rounded-lg border ml-4 ${isLight ? "bg-slate-200/60 border-slate-300" : "bg-black/40 border-white/5"}`}>
            <button
              onClick={() => setActiveTab("dashboard")}
              className={`px-3 py-1.5 rounded-md text-xs font-medium transition-all ${
                activeTab === "dashboard"
                  ? isLight
                    ? "bg-white text-slate-900 shadow-sm font-semibold border border-slate-200"
                    : "bg-slate-800 text-white shadow-sm font-semibold border border-white/10"
                  : isLight
                  ? "text-slate-600 hover:text-slate-900"
                  : "text-slate-400 hover:text-slate-200"
              }`}
            >
              控制台
            </button>
            <button
              onClick={() => setActiveTab("pipeline")}
              className={`px-3 py-1.5 rounded-md text-xs font-medium transition-all flex items-center gap-1.5 ${
                activeTab === "pipeline"
                  ? isLight
                    ? "bg-white text-slate-900 shadow-sm font-semibold border border-slate-200"
                    : "bg-slate-800 text-white shadow-sm font-semibold border border-white/10"
                  : isLight
                  ? "text-slate-600 hover:text-slate-900"
                  : "text-slate-400 hover:text-slate-200"
              }`}
            >
              <Layers className="w-3.5 h-3.5" />
              信号流拓扑
            </button>
            <button
              onClick={() => setActiveTab("setup")}
              className={`px-3 py-1.5 rounded-md text-xs font-medium transition-all flex items-center gap-1.5 ${
                activeTab === "setup"
                  ? isLight
                    ? "bg-white text-slate-900 shadow-sm font-semibold border border-slate-200"
                    : "bg-slate-800 text-white shadow-sm font-semibold border border-white/10"
                  : isLight
                  ? "text-slate-600 hover:text-slate-900"
                  : "text-slate-400 hover:text-slate-200"
              }`}
            >
              <HelpCircle className="w-3.5 h-3.5" />
              DAW 配置指南
            </button>
          </div>
        </div>

        {/* Right Controls */}
        <div className="flex items-center gap-3">
          {/* Day/Night Mode Switcher */}
          <button
            onClick={() => setColorMode(isLight ? "dark" : "light")}
            className={`p-2 rounded-xl border flex items-center gap-1.5 text-xs font-medium transition-all cursor-pointer ${
              isLight
                ? "bg-amber-500/10 border-amber-400/40 text-amber-700 hover:bg-amber-500/20"
                : "bg-slate-900 border-white/10 text-slate-300 hover:text-white hover:bg-slate-800"
            }`}
            title={isLight ? "切换至夜间暗色模式" : "切换至日间亮色模式"}
          >
            {isLight ? (
              <>
                <Sun className="w-4 h-4 text-amber-500 fill-amber-400" />
                <span className="font-mono text-[11px] font-bold">日间</span>
              </>
            ) : (
              <>
                <Moon className="w-4 h-4 text-sky-400 fill-sky-400/30" />
                <span className="font-mono text-[11px] font-bold">夜间</span>
              </>
            )}
          </button>

          {/* Theme Accent Dropdown */}
          <div className={`flex items-center gap-2 border px-2.5 py-1.5 rounded-xl text-xs ${isLight ? "bg-slate-100 border-slate-300 text-slate-800" : "bg-slate-900 border-white/10 text-slate-200"}`}>
            <Palette className="w-3.5 h-3.5 text-slate-400" />
            <select
              value={theme}
              onChange={(e) => setTheme(e.target.value as ThemeName)}
              className="bg-transparent font-medium cursor-pointer focus:outline-none text-xs"
            >
              {THEMES.map((t) => (
                <option key={t.id} value={t.id} className={isLight ? "bg-white text-slate-900" : "bg-slate-900 text-slate-200"}>
                  {t.name}
                </option>
              ))}
            </select>
          </div>

          {/* Quick Mute Toggle */}
          <button
            onClick={handleMuteToggle}
            className={`p-2 rounded-xl border transition-all ${
              isMuted
                ? "bg-rose-500/20 border-rose-500/50 text-rose-500 shadow-[0_0_12px_rgba(244,63,94,0.3)]"
                : isLight
                ? "bg-slate-100 border-slate-300 text-slate-700 hover:bg-slate-200"
                : "bg-slate-900 border-white/10 text-slate-300 hover:text-white hover:bg-slate-800"
            }`}
            title={isMuted ? "取消静音" : "静音麦克风"}
          >
            {isMuted ? <VolumeX className="w-4 h-4" /> : <Volume2 className="w-4 h-4 text-emerald-500" />}
          </button>

          {/* Connection Status Pill */}
          <div className={`flex items-center gap-2 px-3 py-1.5 rounded-xl border font-mono text-xs ${isLight ? "bg-slate-100 border-slate-300 text-slate-800" : "bg-black/40 border-white/10 text-slate-300"}`}>
            <span
              className={`w-2 h-2 rounded-full ${
                connected
                  ? "bg-emerald-500 shadow-[0_0_8px_rgba(16,185,129,0.8)] animate-pulse"
                  : "bg-slate-400"
              }`}
            />
            <span className="font-medium">
              {connected ? "iPhone 已在线" : "等待 iPhone USB 连接"}
            </span>
          </div>
        </div>
      </header>

      {/* Main Container */}
      <main className="flex-1 max-w-7xl w-full mx-auto p-6 flex flex-col gap-6">
        {/* Tab 1: Dashboard Main View */}
        {activeTab === "dashboard" && (
          <div className="grid grid-cols-1 lg:grid-cols-12 gap-6 items-start">
            {/* LEFT COLUMN: Hardware Status, Gain & DSP (5 Cols) */}
            <div className="lg:col-span-5 flex flex-col gap-6">
              {/* Device Connection Card */}
              <div className="glass-panel rounded-2xl p-5 flex flex-col gap-4 relative overflow-hidden">
                <div className={`flex items-center justify-between border-b pb-3 ${isLight ? "border-slate-200" : "border-white/5"}`}>
                  <div className={`flex items-center gap-2 font-mono text-xs font-semibold uppercase tracking-wider ${isLight ? "text-slate-600" : "text-slate-400"}`}>
                    <Smartphone className="w-4 h-4 text-emerald-500" />
                    USB 麦克风硬件状态
                  </div>
                  <span
                    className={`font-mono text-[11px] px-2 py-0.5 rounded-full border font-semibold ${
                      connected
                        ? "bg-emerald-500/15 border-emerald-500/30 text-emerald-600 dark:text-emerald-300"
                        : isLight
                        ? "bg-slate-200 border-slate-300 text-slate-600"
                        : "bg-slate-800 border-white/10 text-slate-400"
                    }`}
                  >
                    {connected ? "● 在线路由中" : "○ 等待连接"}
                  </span>
                </div>

                {/* iPhone Graphic */}
                <PhoneDeviceGraphic state={connState} isLight={isLight} />

                {/* Hardware details table */}
                <div className={`rounded-xl p-3.5 border grid grid-cols-2 gap-3 font-mono text-xs ${isLight ? "bg-slate-100/80 border-slate-200" : "bg-black/40 border-white/5"}`}>
                  <div>
                    <span className={`block text-[10px] ${isLight ? "text-slate-500" : "text-slate-500"}`}>连接通道</span>
                    <span className={`font-medium ${isLight ? "text-slate-900" : "text-slate-200"}`}>
                      {connected ? "Apple usbmuxd (USB)" : "等待线缆插入"}
                    </span>
                  </div>
                  <div>
                    <span className={`block text-[10px] ${isLight ? "text-slate-500" : "text-slate-500"}`}>音频规格</span>
                    <span className={`font-medium ${isLight ? "text-slate-900" : "text-slate-200"}`}>48.0 kHz · 16-bit</span>
                  </div>
                  <div>
                    <span className={`block text-[10px] ${isLight ? "text-slate-500" : "text-slate-500"}`}>ASIO 驱动</span>
                    <span className="text-emerald-600 dark:text-emerald-400 font-medium">32-bit Float PCM</span>
                  </div>
                  <div>
                    <span className={`block text-[10px] ${isLight ? "text-slate-500" : "text-slate-500"}`}>信任状态</span>
                    <span className="text-emerald-600 dark:text-emerald-400 font-medium flex items-center gap-1">
                      <ShieldCheck className="w-3 h-3" /> 已授权
                    </span>
                  </div>
                </div>

                {/* Status Help */}
                <div className={`text-[11px] p-3 rounded-xl border flex items-center gap-2 ${isLight ? "bg-slate-100 text-slate-600 border-slate-200" : "bg-black/30 text-slate-400 border-white/5"}`}>
                  <Zap className="w-4 h-4 text-emerald-500 shrink-0" />
                  <span>无需手动连接，USB 插入并信任电脑后，iOS App 开启即可自动流转。</span>
                </div>
              </div>

              {/* Mic Gain & DSP Processing Controls */}
              <div className="glass-panel rounded-2xl p-5 flex flex-col gap-4">
                <div className={`flex items-center justify-between border-b pb-3 ${isLight ? "border-slate-200" : "border-white/5"}`}>
                  <div className={`flex items-center gap-2 font-mono text-xs font-semibold uppercase tracking-wider ${isLight ? "text-slate-600" : "text-slate-400"}`}>
                    <SlidersHorizontal className="w-4 h-4 text-emerald-500" />
                    硬件增益与 DSP 处理 (Gain & Processing)
                  </div>
                  <span className={`font-mono text-xs font-bold px-2 py-0.5 rounded border ${isLight ? "bg-emerald-100 text-emerald-800 border-emerald-300" : "bg-emerald-500/10 text-emerald-400 border-emerald-500/20"}`}>
                    {isMuted ? "MUTED" : `${gain}% (${Math.round((gain / 100) * 24 - 12)} dB)`}
                  </span>
                </div>

                {/* Gain Slider */}
                <div className="flex flex-col gap-2">
                  <div className={`flex justify-between font-mono text-[11px] ${isLight ? "text-slate-500" : "text-slate-400"}`}>
                    <span>-12 dB (PAD)</span>
                    <span className={`font-semibold ${isLight ? "text-slate-900" : "text-white"}`}>0 dB (Unity)</span>
                    <span>+12 dB (BOOST)</span>
                  </div>
                  <input
                    type="range"
                    min={0}
                    max={100}
                    value={gain}
                    disabled={isMuted}
                    onChange={(e) => handleGainChange(Number(e.target.value))}
                    className="w-full h-2 bg-slate-300 dark:bg-slate-800 rounded-lg appearance-none cursor-pointer accent-emerald-500 disabled:opacity-40"
                  />
                </div>

                {/* DSP Toggles Grid */}
                <div className="grid grid-cols-2 gap-3 pt-2">
                  {/* High Pass Filter */}
                  <button
                    onClick={handleHPFToggle}
                    className={`p-3 rounded-xl border text-left transition-all flex flex-col gap-1.5 cursor-pointer ${
                      highPassFilter
                        ? isLight
                          ? "bg-emerald-50 border-emerald-300 text-emerald-800"
                          : "bg-emerald-500/10 border-emerald-500/40 text-emerald-300"
                        : isLight
                        ? "bg-slate-100/80 border-slate-200 text-slate-600 hover:border-slate-300"
                        : "bg-black/30 border-white/5 text-slate-400 hover:border-white/10"
                    }`}
                  >
                    <div className="flex items-center justify-between">
                      <span className="font-mono text-[11px] font-bold">80Hz 高通低切</span>
                      <span className={`w-2 h-2 rounded-full ${highPassFilter ? "bg-emerald-500" : "bg-slate-400 dark:bg-slate-700"}`} />
                    </div>
                    <span className={`text-[10px] leading-tight ${isLight ? "text-slate-500" : "text-slate-400"}`}>过滤桌面震动与低频杂音</span>
                  </button>

                  {/* Auto Gain Control */}
                  <button
                    onClick={handleAGCToggle}
                    className={`p-3 rounded-xl border text-left transition-all flex flex-col gap-1.5 cursor-pointer ${
                      autoGain
                        ? isLight
                          ? "bg-emerald-50 border-emerald-300 text-emerald-800"
                          : "bg-emerald-500/10 border-emerald-500/40 text-emerald-300"
                        : isLight
                        ? "bg-slate-100/80 border-slate-200 text-slate-600 hover:border-slate-300"
                        : "bg-black/30 border-white/5 text-slate-400 hover:border-white/10"
                    }`}
                  >
                    <div className="flex items-center justify-between">
                      <span className="font-mono text-[11px] font-bold">自动增益 (AGC)</span>
                      <span className={`w-2 h-2 rounded-full ${autoGain ? "bg-emerald-500" : "bg-slate-400 dark:bg-slate-700"}`} />
                    </div>
                    <span className={`text-[10px] leading-tight ${isLight ? "text-slate-500" : "text-slate-400"}`}>防爆音 Peak Limiter 保护</span>
                  </button>
                </div>

                {/* AI Noise Suppression */}
                <div className={`flex flex-col gap-2 pt-1 border-t ${isLight ? "border-slate-200" : "border-white/5"}`}>
                  <div className="flex items-center justify-between">
                    <span className={`font-mono text-xs font-semibold ${isLight ? "text-slate-700" : "text-slate-300"}`}>智能降噪 (Noise Gate)</span>
                    <span className="font-mono text-[10px] text-emerald-600 dark:text-emerald-400 capitalize">{noiseSuppression}</span>
                  </div>
                  <div className="grid grid-cols-4 gap-1.5">
                    {(["off", "low", "medium", "high"] as const).map((level) => (
                      <button
                        key={level}
                        onClick={() => handleNoiseGateChange(level)}
                        className={`py-1.5 rounded-lg font-mono text-xs uppercase transition-all border cursor-pointer ${
                          noiseSuppression === level
                            ? isLight
                              ? "bg-emerald-100 border-emerald-300 text-emerald-800 font-bold"
                              : "bg-emerald-500/20 border-emerald-500/50 text-emerald-300 font-bold"
                            : isLight
                            ? "bg-slate-100 border-slate-200 text-slate-600 hover:bg-slate-200"
                            : "bg-black/20 border-white/5 text-slate-400 hover:bg-slate-800"
                        }`}
                      >
                        {level === "off" ? "关闭" : level === "low" ? "低" : level === "medium" ? "中" : "强"}
                      </button>
                    ))}
                  </div>
                </div>
              </div>
            </div>

            {/* RIGHT COLUMN: Audio Level Meter, Output Switcher & Setup (7 Cols) */}
            <div className="lg:col-span-7 flex flex-col gap-6">
              {/* Stereo Level Meter & Oscilloscope Card */}
              <div className="glass-panel rounded-2xl p-5 flex flex-col gap-4">
                <div className={`flex items-center justify-between border-b pb-3 ${isLight ? "border-slate-200" : "border-white/5"}`}>
                  <div className="flex items-center gap-2">
                    <div className={`font-mono text-xs font-semibold uppercase tracking-wider flex items-center gap-2 ${isLight ? "text-slate-600" : "text-slate-400"}`}>
                      <BarChart3 className="w-4 h-4 text-emerald-500" />
                      实时双通道音频电平监视器
                    </div>
                    {connected && (
                      <span className={`flex items-center gap-1 font-mono text-[10px] px-2 py-0.5 rounded-full border animate-pulse ${isLight ? "bg-emerald-100 text-emerald-800 border-emerald-300" : "bg-emerald-500/10 text-emerald-400 border-emerald-500/20"}`}>
                        <Radio className="w-3 h-3" /> LIVE STREAM
                      </span>
                    )}
                  </div>

                  {/* Mode switch */}
                  <div className={`flex items-center gap-1 p-1 rounded-lg border font-mono text-[11px] ${isLight ? "bg-slate-200/60 border-slate-300" : "bg-black/40 border-white/5"}`}>
                    <button
                      onClick={() => setVisMode("bars")}
                      className={`px-2.5 py-1 rounded transition-all cursor-pointer ${
                        visMode === "bars"
                          ? isLight
                            ? "bg-white text-slate-900 font-bold shadow-sm"
                            : "bg-slate-800 text-white font-bold"
                          : isLight
                          ? "text-slate-600 hover:text-slate-900"
                          : "text-slate-400 hover:text-slate-200"
                      }`}
                    >
                      柱状电平 (24-Band)
                    </button>
                    <button
                      onClick={() => setVisMode("waveform")}
                      className={`px-2.5 py-1 rounded transition-all cursor-pointer ${
                        visMode === "waveform"
                          ? isLight
                            ? "bg-white text-slate-900 font-bold shadow-sm"
                            : "bg-slate-800 text-white font-bold"
                          : isLight
                          ? "text-slate-600 hover:text-slate-900"
                          : "text-slate-400 hover:text-slate-200"
                      }`}
                    >
                      波形图 (Scope)
                    </button>
                  </div>
                </div>

                {/* Level Meter Component */}
                <DualStereoMeter
                  active={connected && !isMuted}
                  levelL={audioLevels.left}
                  levelR={audioLevels.right}
                  visMode={visMode}
                  isLight={isLight}
                />

                {/* Sidetone / Headphone Monitor Bar */}
                <div className={`p-3 rounded-xl border flex items-center justify-between ${isLight ? "bg-slate-100/70 border-slate-200" : "bg-black/30 border-white/5"}`}>
                  <div className="flex items-center gap-2">
                    <Headphones className="w-4 h-4 text-emerald-500" />
                    <div>
                      <span className={`text-xs font-semibold block ${isLight ? "text-slate-800" : "text-slate-200"}`}>耳返耳旁实时监听 (Sidetone)</span>
                      <span className={`text-[10px] ${isLight ? "text-slate-500" : "text-slate-400"}`}>直通监听 iPhone 说话声（DAW 录音时请关闭）</span>
                    </div>
                  </div>
                  <button
                    onClick={() => handleSidetoneToggle(!sidetoneEnabled)}
                    className={`px-3 py-1.5 rounded-lg border font-mono text-xs font-semibold transition-all cursor-pointer ${
                      sidetoneEnabled
                        ? isLight
                          ? "bg-emerald-100 border-emerald-300 text-emerald-800"
                          : "bg-emerald-500/20 border-emerald-500/40 text-emerald-300"
                        : isLight
                        ? "bg-slate-200 border-slate-300 text-slate-600 hover:text-slate-900"
                        : "bg-slate-800 border-white/10 text-slate-400 hover:text-white"
                    }`}
                  >
                    {sidetoneEnabled ? "监听开启 (ON)" : "监听关闭 (OFF)"}
                  </button>
                </div>
              </div>

              {/* Output Audio Device Selector Card */}
              <div className="glass-panel rounded-2xl p-5 flex flex-col gap-4">
                <div className={`flex items-center justify-between border-b pb-3 ${isLight ? "border-slate-200" : "border-white/5"}`}>
                  <div className={`flex items-center gap-2 font-mono text-xs font-semibold uppercase tracking-wider ${isLight ? "text-slate-600" : "text-slate-400"}`}>
                    <Speaker className="w-4 h-4 text-emerald-500" />
                    ASIO 输出设备选择（支持 USB 耳机直通）
                  </div>
                  <button
                    onClick={refreshDevices}
                    className="p-1 rounded hover:bg-white/10 text-slate-400 hover:text-white cursor-pointer transition-all"
                    title="刷新设备列表"
                  >
                    <RefreshCw className="w-3.5 h-3.5" />
                  </button>
                </div>

                <div className="grid grid-cols-1 gap-2 max-h-56 overflow-y-auto pr-1">
                  {devices.length > 0 ? (
                    devices.map((deviceName, index) => {
                      const IconComp = getDeviceIcon(deviceName);
                      const isSelected = selectedDeviceIndex === index;

                      return (
                        <button
                          key={index}
                          onClick={() => handleDeviceSelect(index)}
                          className={`p-3.5 rounded-xl border transition-all flex items-center justify-between text-left cursor-pointer ${
                            isSelected
                              ? isLight
                                ? "bg-emerald-50 border-emerald-300 text-emerald-900 shadow-sm"
                                : "bg-emerald-500/10 border-emerald-500/40 text-emerald-200 shadow-[0_0_15px_rgba(16,185,129,0.1)]"
                              : isLight
                              ? "bg-slate-100/60 border-slate-200 text-slate-700 hover:bg-slate-200/60"
                              : "bg-black/30 border-white/5 text-slate-300 hover:bg-slate-800/60 hover:border-white/10"
                          }`}
                        >
                          <div className="flex items-center gap-3">
                            <div
                              className={`w-9 h-9 rounded-lg flex items-center justify-center border ${
                                isSelected
                                  ? isLight
                                    ? "bg-emerald-200/60 border-emerald-300 text-emerald-800"
                                    : "bg-emerald-500/20 border-emerald-500/40 text-emerald-400"
                                  : isLight
                                  ? "bg-slate-200 border-slate-300 text-slate-600"
                                  : "bg-slate-800 border-white/5 text-slate-400"
                              }`}
                            >
                              <IconComp className="w-4 h-4" />
                            </div>
                            <div>
                              <span className={`font-medium text-sm block ${isLight ? "text-slate-900" : "text-white"}`}>{deviceName}</span>
                              <span className={`text-[11px] ${isLight ? "text-slate-500" : "text-slate-400"}`}>
                                {index === 0 ? "Windows 默认输出" : "WASAPI 低延迟原生直通"}
                              </span>
                            </div>
                          </div>

                          <div className="flex items-center gap-3">
                            <span className={`font-mono text-[10px] px-2 py-1 rounded ${isLight ? "bg-slate-200 text-slate-600" : "bg-slate-900 text-slate-500"}`}>
                              48.0 kHz
                            </span>
                            {isSelected ? (
                              <span className="flex items-center gap-1 font-mono text-xs font-bold text-emerald-600 dark:text-emerald-400">
                                <CheckCircle2 className="w-4 h-4" /> 活跃中
                              </span>
                            ) : (
                              <span className={`text-xs font-mono ${isLight ? "text-slate-400" : "text-slate-500"}`}>点击切换</span>
                            )}
                          </div>
                        </button>
                      );
                    })
                  ) : (
                    <div className="text-center py-4 text-xs text-slate-500 font-mono">
                      (正在加载设备列表...)
                    </div>
                  )}
                </div>

                {/* Sound Test Tool */}
                <div className={`pt-2 border-t flex items-center justify-between ${isLight ? "border-slate-200" : "border-white/5"}`}>
                  <span className={`text-xs ${isLight ? "text-slate-500" : "text-slate-400"}`}>
                    当前输出: <span className="font-semibold text-emerald-500">{currentDeviceName}</span>
                  </span>
                  <button
                    onClick={() => {
                      setTestTonePlaying(true);
                      setTimeout(() => setTestTonePlaying(false), 2000);
                    }}
                    className={`px-3 py-1.5 rounded-lg border font-mono text-xs transition-all flex items-center gap-1.5 cursor-pointer ${
                      testTonePlaying
                        ? "bg-emerald-500 text-slate-950 font-bold border-emerald-400"
                        : isLight
                        ? "bg-slate-200 border-slate-300 text-slate-700 hover:bg-slate-300"
                        : "bg-slate-800 border-white/10 text-slate-300 hover:text-white"
                    }`}
                  >
                    {testTonePlaying ? (
                      <>
                        <Square className="w-3.5 h-3.5 fill-current" /> 测试音播放中…
                      </>
                    ) : (
                      <>
                        <Play className="w-3.5 h-3.5" /> 测试输出通道音效
                      </>
                    )}
                  </button>
                </div>
              </div>

              {/* Audio Statistics & Engine Telemetry Card */}
              <div className="glass-panel rounded-2xl p-5 flex flex-col gap-4">
                <div className={`flex items-center justify-between border-b pb-3 ${isLight ? "border-slate-200" : "border-white/5"}`}>
                  <div className={`flex items-center gap-2 font-mono text-xs font-semibold uppercase tracking-wider ${isLight ? "text-slate-600" : "text-slate-400"}`}>
                    <Gauge className="w-4 h-4 text-emerald-500" />
                    ASIO 引擎实时性能与统计 (Engine Telemetry)
                  </div>
                  <span className={`font-mono text-[10px] ${isLight ? "text-slate-400" : "text-slate-500"}`}>WASAPI Pro Audio</span>
                </div>

                <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
                  <div className={`p-3 rounded-xl border flex flex-col gap-1 ${isLight ? "bg-slate-100/80 border-slate-200" : "bg-black/40 border-white/5"}`}>
                    <span className={`font-mono text-[10px] uppercase ${isLight ? "text-slate-500" : "text-slate-500"}`}>端到端延迟</span>
                    <span className={`font-mono text-base font-bold ${connected && latency > 0 ? "text-emerald-600 dark:text-emerald-400" : isLight ? "text-slate-300" : "text-slate-600"}`}>
                      {connected && latency > 0 ? `${latency.toFixed(1)} ms` : "-- ms"}
                    </span>
                    <span className={`text-[9px] ${isLight ? "text-slate-400" : "text-slate-500"}`}>{connected && latency > 0 ? "超低无感延迟" : "等待引擎启动"}</span>
                  </div>

                  <div className={`p-3 rounded-xl border flex flex-col gap-1 ${isLight ? "bg-slate-100/80 border-slate-200" : "bg-black/40 border-white/5"}`}>
                    <span className={`font-mono text-[10px] uppercase ${isLight ? "text-slate-500" : "text-slate-500"}`}>采样率</span>
                    <span className={`font-mono text-sm font-bold ${connected && sampleRate > 0 ? (isLight ? "text-slate-900" : "text-white") : (isLight ? "text-slate-300" : "text-slate-600")}`}>
                      {connected && sampleRate > 0 ? `${(sampleRate / 1000).toFixed(1)} kHz` : "-- kHz"}
                    </span>
                    <span className={`text-[9px] ${isLight ? "text-slate-400" : "text-slate-500"}`}>{connected && sampleRate > 0 ? "PCM Stream" : "未活动"}</span>
                  </div>

                  <div className={`p-3 rounded-xl border flex flex-col gap-1 ${isLight ? "bg-slate-100/80 border-slate-200" : "bg-black/40 border-white/5"}`}>
                    <span className={`font-mono text-[10px] uppercase ${isLight ? "text-slate-500" : "text-slate-500"}`}>缓冲区大小</span>
                    <select
                      value={bufferSize || 128}
                      onChange={(e) => handleBufferSizeChange(Number(e.target.value))}
                      className={`bg-transparent font-mono text-sm font-bold focus:outline-none cursor-pointer ${connected ? (isLight ? "text-slate-900" : "text-white") : (isLight ? "text-slate-300" : "text-slate-600")}`}
                    >
                      <option value={64} className={isLight ? "bg-white text-slate-900" : "bg-slate-900 text-slate-200"}>64 Samples</option>
                      <option value={128} className={isLight ? "bg-white text-slate-900" : "bg-slate-900 text-slate-200"}>128 Samples</option>
                      <option value={256} className={isLight ? "bg-white text-slate-900" : "bg-slate-900 text-slate-200"}>256 Samples</option>
                      <option value={512} className={isLight ? "bg-white text-slate-900" : "bg-slate-900 text-slate-200"}>512 Samples</option>
                    </select>
                    <span className={`text-[9px] ${isLight ? "text-slate-400" : "text-slate-500"}`}>ASIO Buffer</span>
                  </div>

                  <div className={`p-3 rounded-xl border flex flex-col gap-1 ${isLight ? "bg-slate-100/80 border-slate-200" : "bg-black/40 border-white/5"}`}>
                    <span className={`font-mono text-[10px] uppercase ${isLight ? "text-slate-500" : "text-slate-500"}`}>丢帧记录</span>
                    <span
                      className={`font-mono text-base font-bold ${
                        connected
                          ? droppedFrames > 0 ? "text-amber-500" : (isLight ? "text-slate-700" : "text-slate-300")
                          : (isLight ? "text-slate-300" : "text-slate-600")
                      }`}
                    >
                      {connected ? droppedFrames : "--"} <span className={`text-[10px] font-normal ${isLight ? "text-slate-400" : "text-slate-500"}`}>pkts</span>
                    </span>
                    <span className={`text-[9px] ${isLight ? "text-slate-400" : "text-slate-500"}`}>USB Frame Drop</span>
                  </div>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* Tab 2: Audio Signal Pipeline View */}
        {activeTab === "pipeline" && (
          <div className="glass-panel rounded-2xl p-6 flex flex-col gap-6">
            <div>
              <h2 className={`text-lg font-bold font-heading ${isLight ? "text-slate-900" : "text-white"}`}>USB 音频传输拓扑 (Audio Signal Flow Topology)</h2>
              <p className={`text-xs ${isLight ? "text-slate-500" : "text-slate-400"}`}>从 iPhone 麦克风硬件捕获到 Windows ASIO 驱动与 USB 耳机的信号路径</p>
            </div>

            <div className={`grid grid-cols-1 md:grid-cols-5 gap-4 items-center relative py-8 px-4 rounded-xl border ${isLight ? "bg-slate-100/70 border-slate-200" : "bg-black/40 border-white/5"}`}>
              {/* Node 1 */}
              <div className={`p-4 rounded-xl border flex flex-col items-center text-center gap-2 ${isLight ? "bg-white border-emerald-300 shadow-sm" : "bg-slate-900 border-emerald-500/40"}`}>
                <div className="w-10 h-10 rounded-full bg-emerald-500/20 flex items-center justify-center text-emerald-500">
                  <Smartphone className="w-5 h-5" />
                </div>
                <span className={`font-bold text-xs ${isLight ? "text-slate-900" : "text-white"}`}>iPhone Studio Mic</span>
                <span className={`font-mono text-[10px] ${isLight ? "text-slate-500" : "text-slate-400"}`}>48kHz PCM 采集</span>
              </div>

              {/* Arrow 1 */}
              <div className="flex flex-col items-center justify-center text-emerald-500 font-mono text-[10px]">
                <div className="w-full h-0.5 bg-gradient-to-r from-emerald-500 to-teal-400 relative">
                  <div className="absolute -top-1 right-0 w-2 h-2 border-t-2 border-emerald-500 transform rotate-45" />
                </div>
                <span className="mt-1">USB Lightning / Type-C (usbmuxd)</span>
              </div>

              {/* Node 2 */}
              <div className={`p-4 rounded-xl border flex flex-col items-center text-center gap-2 ${isLight ? "bg-white border-teal-300 shadow-sm" : "bg-slate-900 border-teal-500/40"}`}>
                <div className="w-10 h-10 rounded-full bg-teal-500/20 flex items-center justify-center text-teal-500">
                  <Cpu className="w-5 h-5" />
                </div>
                <span className={`font-bold text-xs ${isLight ? "text-slate-900" : "text-white"}`}>ASIO 驱动 (COM)</span>
                <span className={`font-mono text-[10px] ${isLight ? "text-slate-500" : "text-slate-400"}`}>Ring Buffer + ASRC 同步</span>
              </div>

              {/* Arrow 2 */}
              <div className="flex flex-col items-center justify-center text-teal-500 font-mono text-[10px]">
                <div className="w-full h-0.5 bg-gradient-to-r from-teal-400 to-sky-400 relative">
                  <div className="absolute -top-1 right-0 w-2 h-2 border-t-2 border-sky-500 transform rotate-45" />
                </div>
                <span className="mt-1">WASAPI 低延迟直通</span>
              </div>

              {/* Node 3 */}
              <div className={`p-4 rounded-xl border flex flex-col items-center text-center gap-2 ${isLight ? "bg-white border-sky-300 shadow-sm" : "bg-slate-900 border-sky-500/40"}`}>
                <div className="w-10 h-10 rounded-full bg-sky-500/20 flex items-center justify-center text-sky-500">
                  <Speaker className="w-5 h-5" />
                </div>
                <span className={`font-bold text-xs ${isLight ? "text-slate-900" : "text-white"}`}>{currentDeviceName}</span>
                <span className={`font-mono text-[10px] ${isLight ? "text-slate-500" : "text-slate-400"}`}>USB 耳机 / 声卡物理回放</span>
              </div>
            </div>
          </div>
        )}

        {/* Tab 3: Setup & Troubleshooting Guide */}
        {activeTab === "setup" && (
          <div className="glass-panel rounded-2xl p-6 flex flex-col gap-6">
            <div>
              <h2 className={`text-lg font-bold font-heading ${isLight ? "text-slate-900" : "text-white"}`}>快速使用与配置指南 (DAW Setup Guide)</h2>
              <p className={`text-xs ${isLight ? "text-slate-500" : "text-slate-400"}`}>在 Studio One、Cubase、FL Studio 等专业宿主中配置</p>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              {[
                {
                  step: "01",
                  title: "使用 USB 数据线连接 iPhone 到电脑",
                  desc: "使用原装 Type-C 或 Lightning 数据线连接电脑 USB 接口，并在 iPhone 屏幕上点击「信任此电脑」。",
                },
                {
                  step: "02",
                  title: "在 iPhone 上启动 iPhoneMic App",
                  desc: "打开 iPhone 上的配套 App 并允许麦克风权限，开启音频采集，电脑端驱动将自动建立高速 USB 隧道。",
                },
                {
                  step: "03",
                  title: "在 DAW 音频设备中选中 iPhone ASIO",
                  desc: "打开 Studio One / Cubase / FL Studio，在音频设置中将 ASIO 驱动选择为「iPhone USB Microphone ASIO」。",
                },
                {
                  step: "04",
                  title: "在控制台中选定你的 USB 耳机",
                  desc: "在控制台中点击选中你的 USB 耳机或外置声卡，DAW 的播放声音即可通过低延迟 WASAPI 直接输出至耳机。",
                },
                {
                  step: "05",
                  title: "创建音轨开始录音",
                  desc: "在 DAW 中新建音频轨道，输入源选择「iPhone Mic L/R」，即可直接录制 iPhone 麦克风的人声音频。",
                },
              ].map((item, idx) => (
                <div key={idx} className={`p-4 rounded-xl border flex items-start gap-4 ${isLight ? "bg-slate-100/70 border-slate-200" : "bg-black/40 border-white/5"}`}>
                  <span className={`font-mono text-sm font-bold border px-2.5 py-1 rounded-lg ${isLight ? "bg-emerald-100 text-emerald-800 border-emerald-300" : "bg-emerald-500/10 text-emerald-400 border-emerald-500/20"}`}>
                    {item.step}
                  </span>
                  <div>
                    <h3 className={`font-bold text-sm mb-1 ${isLight ? "text-slate-900" : "text-white"}`}>{item.title}</h3>
                    <p className={`text-xs leading-relaxed ${isLight ? "text-slate-600" : "text-slate-400"}`}>{item.desc}</p>
                  </div>
                </div>
              ))}
            </div>
          </div>
        )}
      </main>

      {/* Footer Bar */}
      <footer className={`border-t px-6 py-3 font-mono text-xs flex flex-col sm:flex-row items-center justify-between gap-2 ${isLight ? "bg-white/80 border-slate-200 text-slate-500" : "bg-black/40 border-white/10 text-slate-500"}`}>
        <div className="flex items-center gap-3">
          <span className={`flex items-center gap-1.5 ${isLight ? "text-slate-700" : "text-slate-400"}`}>
            <Radio className="w-3.5 h-3.5 text-emerald-500" /> iPhone USB ASIO Driver Daemon
          </span>
          <span>·</span>
          <span>Windows 10/11 ASIO 2.3</span>
        </div>
        <div className="flex items-center gap-4">
          <span>{connected ? "状态: 48kHz / 32-bit Float" : "待机 (Idle)"}</span>
          <span className="text-emerald-500 font-medium">Buffer: {bufferSize} samples</span>
        </div>
      </footer>
    </div>
  );
}
