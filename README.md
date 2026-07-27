# iPhone USB Microphone ASIO

将 iPhone 通过 USB 数据线连接到 Windows 电脑，作为低延迟专业麦克风输入设备，在 DAW 中使用。

## 系统架构

```
iPhone 麦克风 → AVAudioEngine → 16bit PCM → TCP → USB(usbmuxd) → Windows TCP Client (内置 usbmux) → Ring Buffer → ASIO Driver → DAW
```

## 功能特性

- **48kHz / 16-bit** 专业音频质量
- **< 20ms** 端到端延迟
- **ASIO 驱动**，可被 Studio One / Cubase / Ableton 直接识别
- **USB 有线传输**，稳定可靠
- 支持 **64 / 128 / 256 / 512** samples 缓冲区
- Mono / Stereo 可选
- 无锁环形缓冲区，零拷贝音频路径

## 快速开始

### 前置条件

| 组件 | 要求 |
|------|------|
| Windows | 10/11, 64-bit |
| Visual Studio | 2022 (C++ 桌面开发工作负载) |
| CMake | 3.25+ |
| iTunes | 最新版 (提供 Apple Mobile Device Service USB 驱动) |
| macOS + Xcode | 15+ (编译 iOS App) |
| iPhone | iOS 16+, Lightning/USB-C 数据线 |

### 步骤 1: 编译 Windows 端

```batch
cd e:\mic\scripts
build_all.bat
```

### 步骤 2: 运行单元测试

```batch
cd e:\mic\windows\build\bin\Release
ring_buffer_test.exe
protocol_test.exe
audio_format_test.exe
```

### 步骤 3: 注册 ASIO 驱动

以管理员身份运行:
```batch
cd e:\mic\windows\tools
register_driver.bat
```

### 步骤 4: 编译 iOS App

1. 在 macOS 上打开 `ios/iPhoneMic/iPhoneMic.xcodeproj`
2. 选择目标设备（你的 iPhone）
3. Build & Run (⌘R)
4. 首次运行会请求麦克风权限，点击"允许"

### 步骤 5: 建立 USB 连接

1. 用 USB 数据线连接 iPhone 到 Windows 电脑
2. 在 iPhone 上信任此电脑
*(注：无需运行任何外部代理，程序已内置 usbmuxd 客户端，自动建立 USB 隧道)*

### 步骤 6: 启动音频流

1. 在 iPhone 上打开 iPhoneMic App
2. 点击"开始采集"
3. 在 Windows 上运行测试客户端:

```batch
iphone_mic_client.exe --save test.raw --duration 5 --verbose
```

4. 使用 Audacity 验证: File > Import > Raw Data
   - Encoding: Signed 16-bit PCM
   - Byte order: Little-endian  
   - Channels: 1 (Mono)
   - Sample rate: 48000

### 步骤 7: 在 DAW 中使用

1. 打开 Studio One / Cubase / Ableton
2. 音频设置 → ASIO 驱动
3. 选择 **"iPhone USB Microphone ASIO"**
4. 创建音频轨道，选择 iPhone Mic 作为输入
5. 开始录音！

## 项目结构

```
e:\mic\
├── ios\                           # iOS 工程
│   └── iPhoneMic\
│       └── iPhoneMic\
│           ├── App\               # SwiftUI 入口和界面
│           ├── Audio\             # 音频采集和 PCM 转换
│           ├── Network\           # TCP Server 和协议
│           └── Models\            # 配置模型
│
├── windows\                       # Windows 工程
│   ├── common\                    # 共享代码
│   │   ├── include\               # 协议、Ring Buffer、音频格式
│   │   └── src\                   # 实现
│   ├── client\                    # 独立测试客户端
│   ├── asio_driver\               # ASIO 驱动 DLL
│   │   ├── include\               # ASIO 接口定义
│   │   └── src\                   # 驱动实现、COM、注册
│   ├── tests\                     # 单元测试
│   └── CMakeLists.txt             # 构建配置
│
├── scripts\                       # 构建和部署脚本
└── docs\                          # 文档
```

## 通信协议

详见 [docs/protocol.md](docs/protocol.md)

## 调试方法

### iOS 端调试

1. Xcode 中运行 App，观察控制台输出
2. 使用 macOS 终端连接: `nc <iphone-ip> 8730`
3. 检查音频电平表是否响应麦克风

### Windows 端调试

1. 使用测试客户端保存 raw 文件
2. 查看统计信息: 接收字节数、丢包率、缓冲区使用率
3. 使用 Audacity 验证录制的音频

### ASIO 驱动调试

1. 使用 Visual Studio 附加到 DAW 进程
2. 驱动控制面板: DAW 中点击 ASIO 设置
3. 检查注册表: `HKLM\SOFTWARE\ASIO\iPhone USB Microphone ASIO`

## 故障排除

| 问题 | 解决方案 |
|------|---------|
| 找不到设备 | 确认 iTunes 已安装且 iPhone 已信任此电脑 |
| DAW 看不到 ASIO 驱动 | 以管理员身份运行 `regsvr32` 注册 DLL |
| 无声音 | 确认 iPhone App 已开始采集且 USB 连接正常 |
| 音频断断续续 | 增大 buffer size (256 或 512) |
| 延迟过高 | 减小 buffer size (64 或 128) |

## 许可证

- ASIO 技术: Steinberg GPLv3 / 专有双许可
- 本项目代码: MIT License
