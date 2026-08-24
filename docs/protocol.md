# iPhone USB Microphone - 通信协议文档

## 概述

本协议定义了 iPhone (iOS) 与 Windows 之间通过 USB 隧道传输实时音频数据的二进制帧格式。

## 传输层

```
iPhone App (TCP Server :8730)
    ↕ usbmuxd (Apple Mobile Device Service)
Windows (TCP Client → 127.0.0.1:8730 via iproxy)
```

- 传输协议: TCP
- 端口: 8730
- 字节序: Little-Endian
- USB 隧道: Apple usbmuxd → iproxy 端口转发

## 数据帧格式

### 帧结构

每个数据帧由固定长度的 **Header (24 bytes)** 和可变长度的 **Payload** 组成。

```
┌───────────────────────────────────────────────────┐
│                Header (24 bytes)                  │
├─────────┬─────────┬──────┬──────────┬─────────────┤
│ Magic   │ Version │ Type │ Payload  │ Reserved +  │
│ 4B      │ 2B      │ 2B   │ Size 4B  │ Timestamp   │
│         │         │      │          │ 4B + 8B     │
├─────────┴─────────┴──────┴──────────┴─────────────┤
│          Payload (PayloadSize bytes)               │
└───────────────────────────────────────────────────┘
```

### Header 字段

| 偏移 | 大小 | 字段 | 类型 | 描述 |
|------|------|------|------|------|
| 0 | 4 | magic | uint32 LE | 固定值 `0x4D485049` ("IPHM") |
| 4 | 2 | version | uint16 LE | 协议版本，当前为 `1` |
| 6 | 2 | type | uint16 LE | 帧类型（见下表） |
| 8 | 4 | payload_size | uint32 LE | Payload 字节数（可为 0） |
| 12 | 4 | reserved | uint32 LE | 保留字段，填充 0 |
| 16 | 8 | timestamp | uint64 LE | 发送时间戳（微秒） |

### 帧类型

| Type 值 | 名称 | 方向 | Payload |
|---------|------|------|---------|
| `0x01` | AudioData | iPhone → Windows | PCM 音频采样数据 |
| `0x02` | Config | 双向 | JSON 配置信息 |
| `0x03` | Heartbeat | 双向 | 无 (payload_size = 0) |
| `0x04` | ConfigAck | Windows → iPhone | 无 (payload_size = 0) |

## PCM 音频数据格式

### AudioData Payload

音频数据帧的 Payload 包含原始 PCM 采样数据:

| 参数 | 值 |
|------|------|
| 采样率 | 48,000 Hz |
| 位深 | 16-bit signed integer (Little-Endian) |
| 字节序 | Little-Endian |
| 声道 | 1 (Mono) 或 2 (Stereo) |
| 交织方式 | Interleaved (L R L R ...) |
| 每采样字节数 | 2 |
| 每帧字节数 | 2 × channels |
| ASIO 输出映射 | 驱动内部映射至 32-bit signed int (`ASIOSTInt32LSB`) |

### 采样值范围

| 值 | 含义 |
|----|------|
| `0x7FFF` (+32,767) | 最大正值 |
| `0x0000` (0) | 静音 |
| `0x8000` (-32,768) | 最大负值 |

### 典型 Payload 大小

| Buffer Size | Mono (1 ch) | Stereo (2 ch) |
|-------------|-------------|---------------|
| 64 samples  | 128 bytes   | 256 bytes     |
| 128 samples | 256 bytes   | 512 bytes     |
| 256 samples | 512 bytes   | 1,024 bytes   |
| 512 samples | 1,024 bytes | 2,048 bytes   |

## Config 帧

### Config Payload (JSON)

```json
{
    "sampleRate": 48000,
    "bitDepth": 16,
    "channels": 1,
    "bufferSize": 256
}
```

### 配置交换流程

```
iPhone                          Windows
  │                                │
  │──── Config ──────────────────→ │  (连接后立即发送)
  │                                │
  │←──── ConfigAck ────────────── │  (确认收到)
  │                                │
  │──── AudioData ───────────────→ │  (开始流式传输)
  │──── AudioData ───────────────→ │
  │──── AudioData ───────────────→ │
  │          ...                   │
  │                                │
  │──── Heartbeat ───────────────→ │  (每秒一次)
  │←──── Heartbeat ────────────── │  (回复)
  │                                │
```

## 错误处理

### 连接断开

- Windows 客户端每 1 秒检测连接状态
- 断开后自动重连 (1 秒间隔)
- 重连后 iPhone 重新发送 Config 帧

### 数据完整性

- Magic 字段用于流同步和错误恢复
- Parser 遇到无效 Magic 时逐字节扫描寻找下一个有效帧
- Payload 大小上限 1MB (防止内存溢出)

### 缓冲区溢出

- Ring Buffer 满时丢弃最旧数据
- ASIO 回调读取不足数据时填充静音
- 客户端统计丢包数量

## 延迟分析

| 环节 | 延迟 |
|------|------|
| iOS 音频采集 (256 samples @ 48kHz) | ~5.3 ms |
| TCP + usbmuxd 传输 | ~1-3 ms |
| Ring Buffer + ASIO (256 samples) | ~5.3 ms |
| **总计 (典型)** | **~12-14 ms** |
