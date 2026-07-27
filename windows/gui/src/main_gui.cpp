#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <shellapi.h>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <string>

#include "usbmux_client.h"
#include "protocol.h"
#include "ring_buffer.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

using namespace iphone_mic;

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// iPhone Mic Application State
std::atomic<bool> g_AppRunning = true;
std::atomic<bool> g_IsConnected = false;
std::atomic<float> g_PeakL = 0.0f;
std::atomic<float> g_PeakR = 0.0f;
std::mutex g_WaveformMutex;
std::vector<float> g_Waveform;

// Audio Playback
std::atomic<bool> g_MonitorAudio = false;
std::unique_ptr<RingBuffer> g_AudioRingBuffer;
ma_device g_AudioDevice;

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    (void)pInput; // Unused
    if (g_MonitorAudio && g_AudioRingBuffer) {
        size_t bytesToRead = frameCount * 2 * sizeof(float); // Stereo f32
        size_t bytesRead = g_AudioRingBuffer->read(reinterpret_cast<uint8_t*>(pOutput), bytesToRead);
        if (bytesRead < bytesToRead) {
            // Fill remainder with zeros (silence) to avoid audio glitches
            std::memset(reinterpret_cast<uint8_t*>(pOutput) + bytesRead, 0, bytesToRead - bytesRead);
        }
    } else {
        // Output silence
        std::memset(pOutput, 0, frameCount * 2 * sizeof(float));
        // Keep clearing the buffer so old audio doesn't queue up when disabled
        if (g_AudioRingBuffer) {
            uint8_t dummy[4096];
            while (g_AudioRingBuffer->read(dummy, sizeof(dummy)) > 0) {}
        }
    }
}

void BackgroundUSBThread() {
    while (g_AppRunning) {
        g_IsConnected = false;
        
        // Wait for device
        SOCKET sock = UsbMuxClient::connect_to_device(8730);
        if (sock == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        g_IsConnected = true;

        while (g_AppRunning && g_IsConnected) {
            PacketHeader header;
            int ret = recv(sock, reinterpret_cast<char*>(&header), sizeof(header), MSG_WAITALL);
            if (ret != sizeof(header)) {
                g_IsConnected = false;
                break;
            }

            if (header.magic == PROTOCOL_MAGIC && header.type == static_cast<uint16_t>(PacketType::AudioData)) {
                std::vector<uint8_t> payload(header.payload_size);
                ret = recv(sock, reinterpret_cast<char*>(payload.data()), header.payload_size, MSG_WAITALL);
                if (ret != header.payload_size) {
                    g_IsConnected = false;
                    break;
                }

                size_t pcmSamples = header.payload_size / 3; // 3 bytes per 24-bit sample
                uint8_t* pcm = payload.data();
                
                float maxL = 0.0f;
                float maxR = 0.0f;
                
                std::vector<float> audioFrames;
                if (g_MonitorAudio) {
                    audioFrames.reserve(pcmSamples);
                }

                // Extract Peak and Waveform
                std::lock_guard<std::mutex> lock(g_WaveformMutex);
                for (size_t i = 0; i < pcmSamples; i += 2) { // Assuming Stereo
                    if (i + 1 < pcmSamples) {
                        int32_t valL = pcm[i*3] | (pcm[i*3+1] << 8) | (pcm[i*3+2] << 16);
                        if (valL & 0x800000) valL |= 0xFF000000;
                        float fL = valL / 8388608.0f;

                        int32_t valR = pcm[(i+1)*3] | (pcm[(i+1)*3+1] << 8) | (pcm[(i+1)*3+2] << 16);
                        if (valR & 0x800000) valR |= 0xFF000000;
                        float fR = valR / 8388608.0f;

                        maxL = std::max(maxL, std::abs(fL));
                        maxR = std::max(maxR, std::abs(fR));
                        g_Waveform.push_back(fL);
                        
                        if (g_MonitorAudio) {
                            audioFrames.push_back(fL);
                            audioFrames.push_back(fR);
                        }
                    }
                }
                
                g_PeakL = maxL;
                g_PeakR = maxR;

                // Push to playback buffer if monitoring
                if (g_MonitorAudio && g_AudioRingBuffer) {
                    g_AudioRingBuffer->write(reinterpret_cast<const uint8_t*>(audioFrames.data()), audioFrames.size() * sizeof(float));
                }

                if (g_Waveform.size() > 2000) {
                    size_t to_remove = g_Waveform.size() - 2000;
                    g_Waveform.erase(g_Waveform.begin(), g_Waveform.begin() + to_remove);
                }
            } else {
                // Read and discard other payloads
                std::vector<uint8_t> discard(header.payload_size);
                recv(sock, reinterpret_cast<char*>(discard.data()), header.payload_size, MSG_WAITALL);
            }
        }
        closesocket(sock);
    }
}

// Main code
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Initialize audio ring buffer (48000 * 2 channels * 4 bytes * 1 second = ~384KB)
    g_AudioRingBuffer = std::make_unique<RingBuffer>(48000 * 8);

    // Initialize miniaudio
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = 48000;
    config.dataCallback      = data_callback;
    config.pUserData         = nullptr;

    if (ma_device_init(NULL, &config, &g_AudioDevice) != MA_SUCCESS) {
        // Failed to initialize audio device
    } else {
        ma_device_start(&g_AudioDevice);
    }

    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"iPhoneMic GUI", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"iPhoneMic Control Center", WS_OVERLAPPEDWINDOW, 100, 100, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, nCmdShow);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // Tweak to look more modern
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Start background USB thread
    std::thread usbThread(BackgroundUSBThread);

    // Main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // -------------------------------------------------------------
        // iPhoneMic Dashboard UI
        // -------------------------------------------------------------
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Dashboard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "iPhoneMic Control Center");
        ImGui::Separator();
        ImGui::Spacing();

        if (g_IsConnected) {
            ImGui::Text("Status: Connected to iPhone");
        } else {
            ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "Status: Waiting for iPhone (Plug in USB & Open App)");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Audio Levels");
        
        float pL = g_PeakL;
        float pR = g_PeakR;
        ImGui::ProgressBar(pL, ImVec2(-1.0f, 0.0f), "Left Peak");
        ImGui::ProgressBar(pR, ImVec2(-1.0f, 0.0f), "Right Peak");

        ImGui::Spacing();
        ImGui::Text("Real-time Waveform (Left Channel)");
        {
            std::lock_guard<std::mutex> lock(g_WaveformMutex);
            if (g_Waveform.size() > 0) {
                ImGui::PlotLines("", g_Waveform.data(), (int)g_Waveform.size(), 0, nullptr, -1.0f, 1.0f, ImVec2(-1, 100.0f));
            } else {
                ImGui::Text("No data...");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("ASIO Driver Management");
        
        if (ImGui::Button("Install / Register ASIO Driver", ImVec2(250, 40))) {
            ShellExecuteA(NULL, "runas", "register_driver.bat", NULL, NULL, SW_SHOWNORMAL);
        }
        
        if (ImGui::Button("Uninstall ASIO Driver", ImVec2(250, 40))) {
            ShellExecuteA(NULL, "runas", "unregister_driver.bat", NULL, NULL, SW_SHOWNORMAL);
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Audio Monitoring");
        
        bool monitor = g_MonitorAudio;
        if (ImGui::Checkbox("Monitor Audio (Listen to iPhone Microphone)", &monitor)) {
            g_MonitorAudio = monitor;
            if (monitor) {
                // Clear the buffer when turning it on to prevent playing old burst
                uint8_t dummy[4096];
                while (g_AudioRingBuffer->read(dummy, sizeof(dummy)) > 0) {}
            }
        }

        ImGui::End();
        // -------------------------------------------------------------

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // Present with vsync
    }

    g_AppRunning = false;
    usbThread.join();

    // Cleanup audio
    ma_device_uninit(&g_AudioDevice);
    g_AudioRingBuffer.reset();

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
