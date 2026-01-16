#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <algorithm> 
#include <gdiplus.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Storage.Streams.h>
#include <iostream>
#include <string>
#include <sstream>
#include <mutex>
#include <vector>
#include <iomanip>
#include <atomic>
#include <thread>

// 修复宏定义冲突
#undef min
#undef max

// 链接库
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Storage::Streams;
using namespace Gdiplus;

// ================= 配置区域 =================
const bool SHOW_CONSOLE = false;
const int  HTTP_PORT = 8080;

// ================= 定义消息和ID =================
#define WM_USER_REPAINT (WM_USER + 1) // 重绘消息
#define WM_TRAYICON     (WM_USER + 2) // 托盘图标消息
#define ID_TRAY_EXIT    9001          // 退出菜单ID
#define ID_TRAY_TOGGLE  9002          // [新增] 切换显示/隐藏ID
#define ID_TRAY_ICON    1001          // 图标ID

// ================= 全局变量 =================
HWND g_hWnd = nullptr;
NOTIFYICONDATA g_nid = { 0 };
std::wstring g_displayText = L"Waiting...";
std::mutex g_guiMutex;
std::atomic<int> g_sharedHeartRate{ 0 };
std::string g_sharedDeviceName = "Scanning...";
std::mutex g_dataMutex;
BluetoothLEAdvertisementWatcher g_watcher{ nullptr };
ULONG_PTR g_gdiplusToken;

// ================= 编码转换辅助函数 =================
std::string AnsiToUtf8(const std::string& str) {
    if (str.empty()) return "";
    int wLen = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
    if (wLen <= 0) return str;
    std::vector<wchar_t> wStr(wLen);
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, wStr.data(), wLen);
    int uLen = WideCharToMultiByte(CP_UTF8, 0, wStr.data(), -1, NULL, 0, NULL, NULL);
    if (uLen <= 0) return str;
    std::vector<char> uStr(uLen);
    WideCharToMultiByte(CP_UTF8, 0, wStr.data(), -1, uStr.data(), uLen, NULL, NULL);
    return std::string(uStr.data());
}

// ================= HTTP 服务器 =================
void HttpServerThread() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;
    SOCKET listenSocket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) { WSACleanup(); return; }

    int v6only = 0;
    setsockopt(listenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v6only, sizeof(v6only));

    sockaddr_in6 serverAddr = { 0 };
    serverAddr.sin6_family = AF_INET6;
    serverAddr.sin6_port = htons(HTTP_PORT);
    serverAddr.sin6_addr = in6addr_any;

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(listenSocket); WSACleanup(); return;
    }
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listenSocket); WSACleanup(); return;
    }
    if (SHOW_CONSOLE) std::cout << "HTTP Server running on port " << HTTP_PORT << std::endl;

    while (true) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) continue;
        char buffer[2048] = { 0 };
        recv(clientSocket, buffer, 2048, 0);
        std::string request(buffer);

        int currentHr = g_sharedHeartRate.load();
        std::string currentDev;
        {
            std::lock_guard<std::mutex> lock(g_dataMutex);
            currentDev = g_sharedDeviceName;
        }

        std::string jsonBody = "{\"heart_rate\": " + std::to_string(currentHr) + ", \"device\": \"" + currentDev + "\", \"timestamp\": " + std::to_string(GetTickCount64()) + "}";
        std::string httpResponse;

        if (request.find("GET /api") != std::string::npos) {
            httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(jsonBody.size()) + "\r\n\r\n" + jsonBody;
        }
        else {
            std::string htmlBody = R"(<!DOCTYPE html><html lang="zh-CN"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>心率监控</title><style>body{background-color:#1a1a1a;color:white;font-family:"Microsoft YaHei",sans-serif;display:flex;flex-direction:column;justify-content:center;align-items:center;height:100vh;margin:0;overflow:hidden}body.transparent{background-color:transparent}#container{text-align:center}.row-container{display:flex;flex-direction:row;justify-content:center;align-items:center;gap:20px}#heart-icon{font-size:80px;color:#ff3333;transition:transform 0.1s;margin-top:10px}#heart-rate{font-size:100px;font-weight:bold;color:#ffffff;line-height:1}#device-name{font-size:24px;color:#cccccc;margin-top:10px;font-weight:normal}@keyframes beat{from{transform:scale(1)}to{transform:scale(1.15)}}</style></head><body><div id="container"><div class="row-container"><div id="heart-icon">&#x2764;</div><div id="heart-rate">--</div></div></div><script>const icon=document.getElementById('heart-icon');const rateText=document.getElementById('heart-rate');const devText=document.getElementById('device-name');async function update(){try{const res=await fetch('/api');const data=await res.json();if(data.heart_rate>0){rateText.innerText=data.heart_rate;if(devText)devText.innerText=data.device;const beatDuration=60/data.heart_rate;icon.style.animation=`beat ${beatDuration/2}s infinite alternate`}else{rateText.innerText="--";if(devText)devText.innerHTML="&#x7B49;&#x5F85;&#x6570;&#x636E;...";icon.style.animation="none"}}catch(e){console.log("Connection lost")}}if(window.location.search.includes('transparent')){document.body.classList.add('transparent')}setInterval(update,1000);update();</script></body></html>)";
            std::string utf8Html = AnsiToUtf8(htmlBody);
            httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " + std::to_string(utf8Html.size()) + "\r\n\r\n" + utf8Html;
        }
        send(clientSocket, httpResponse.c_str(), (int)httpResponse.size(), 0);
        closesocket(clientSocket);
    }
    closesocket(listenSocket);
    WSACleanup();
}

// ================= 渲染 (GDI+) =================
void RenderLayeredWindow() {
    if (!g_hWnd || !IsWindowVisible(g_hWnd)) return; // 优化：如果窗口不可见则不绘制

    std::wstring text;
    {
        std::lock_guard<std::mutex> lock(g_guiMutex);
        text = g_displayText;
    }

    RECT rc;
    GetWindowRect(g_hWnd, &rc);
    SIZE winSize = { rc.right - rc.left, rc.bottom - rc.top };

    HDC hScreenDC = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = winSize.cx;
    bmi.bmiHeader.biHeight = winSize.cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBitmap = CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HBITMAP hOldBitmap = nullptr;
    if (hBitmap) hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);

    {
        Graphics graphics(hMemDC);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.Clear(Color(0, 0, 0, 0));

        Gdiplus::Font fontFamily(L"Microsoft YaHei", 40, FontStyleBold, UnitPoint);
        SolidBrush solidBrush(Color(255, 255, 0, 0));
        RectF layoutRect(0.0f, 0.0f, (REAL)winSize.cx, (REAL)winSize.cy);
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        graphics.DrawString(text.c_str(), -1, &fontFamily, layoutRect, &format, &solidBrush);
    }

    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION blend = { 0 };
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(g_hWnd, hScreenDC, NULL, &winSize, hMemDC, &ptSrc, 0, &blend, ULW_ALPHA);

    if (hBitmap) {
        SelectObject(hMemDC, hOldBitmap);
        DeleteObject(hBitmap);
    }
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
}

void UpdateGuiText(const std::wstring& text) {
    {
        std::lock_guard<std::mutex> lock(g_guiMutex);
        g_displayText = text;
    }
    PostMessage(g_hWnd, WM_USER_REPAINT, 0, 0);
}

// ================= 蓝牙逻辑 =================
void OnAdvertisementReceived(BluetoothLEAdvertisementWatcher const&, BluetoothLEAdvertisementReceivedEventArgs const& args) {
    auto manufacturerSections = args.Advertisement().ManufacturerData();
    for (auto const& section : manufacturerSections) {
        if (section.CompanyId() == 0x0157) {
            DataReader reader = DataReader::FromBuffer(section.Data());
            if (reader.UnconsumedBufferLength() >= 4) {
                std::vector<uint8_t> data;
                while (reader.UnconsumedBufferLength() > 0) data.push_back(reader.ReadByte());

                uint8_t heartRate = data[3];
                if (heartRate == 0) return;

                std::wstring wName = args.Advertisement().LocalName().c_str();
                if (wName.empty()) wName = L"(unknown)";
                int rssi = args.RawSignalStrengthInDBm();

                g_sharedHeartRate.store(heartRate);
                {
                    std::lock_guard<std::mutex> lock(g_dataMutex);
                    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wName.c_str(), (int)wName.length(), NULL, 0, NULL, NULL);
                    std::string strTo(size_needed, 0);
                    WideCharToMultiByte(CP_UTF8, 0, wName.c_str(), (int)wName.length(), &strTo[0], size_needed, NULL, NULL);
                    g_sharedDeviceName = strTo;
                }

                std::wstringstream wss;
                wss << L"\u2764 " << heartRate;
                UpdateGuiText(wss.str());

                if (SHOW_CONSOLE) {
                    std::wcout << L"\r" << std::left << std::setw(20) << wName << L" (" << rssi << L"dBm) HR: " << std::setw(3) << heartRate << L"    " << std::flush;
                }
            }
        }
    }
}

void StartScanning() {
    g_watcher = BluetoothLEAdvertisementWatcher();
    g_watcher.ScanningMode(BluetoothLEScanningMode::Active);
    g_watcher.Received(OnAdvertisementReceived);
    g_watcher.Start();
    if (SHOW_CONSOLE) std::wcout << L"Scanning..." << std::endl;
}

// ================= 托盘图标 =================
void InitTrayIcon(HWND hWnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hWnd;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Mi Band Heart Rate");
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

// ================= 窗口过程 =================
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        StartScanning();
        InitTrayIcon(hWnd);
        std::thread(HttpServerThread).detach();
        break;

    case WM_USER_REPAINT:
        RenderLayeredWindow();
        break;

    case WM_TRAYICON:
        // [新增] 左键双击切换显示/隐藏
        if (lParam == WM_LBUTTONDBLCLK) {
            SendMessage(hWnd, WM_COMMAND, ID_TRAY_TOGGLE, 0);
        }
        else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();

            // [新增] 根据当前状态动态改变菜单文本
            if (IsWindowVisible(hWnd)) {
                AppendMenu(hMenu, MF_STRING, ID_TRAY_TOGGLE, L"隐藏悬浮窗 (Hide)");
            }
            else {
                AppendMenu(hMenu, MF_STRING, ID_TRAY_TOGGLE, L"开启屏幕显示 (Show)");
            }

            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出 (Exit)");

            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
        }
        break;

    case WM_COMMAND:
        // [新增] 处理显示/隐藏命令
        if (LOWORD(wParam) == ID_TRAY_TOGGLE) {
            if (IsWindowVisible(hWnd)) {
                ShowWindow(hWnd, SW_HIDE);
            }
            else {
                ShowWindow(hWnd, SW_SHOW);
                // 显示后立即重绘一次，防止出现空白
                RenderLayeredWindow();
            }
        }
        else if (LOWORD(wParam) == ID_TRAY_EXIT) {
            PostQuitMessage(0);
        }
        break;

    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProc(hWnd, message, wParam, lParam);
        if (hit == HTCLIENT) return HTCAPTION;
        return hit;
    }

    case WM_DESTROY:
        RemoveTrayIcon();
        if (g_watcher) g_watcher.Stop();
        PostQuitMessage(0);
        break;

    case WM_CONTEXTMENU:
    {
        // 窗口本身的右键菜单（保留退出，也可以加 toggle，这里保持原样）
        POINT pt;
        GetCursorPos(&pt);
        HMENU hMenu = CreatePopupMenu();
        AppendMenu(hMenu, MF_STRING, ID_TRAY_TOGGLE, L"隐藏 (Hide)"); // 窗口上右键肯定是想隐藏
        AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出 (Exit)");
        SetForegroundWindow(hWnd);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
        DestroyMenu(hMenu);
    }
    break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    init_apartment();
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

    if (SHOW_CONSOLE) {
        AllocConsole();
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        std::wcout.clear(); std::cout.clear();
        SetConsoleOutputCP(CP_UTF8);
    }

    const wchar_t CLASS_NAME[] = L"HeartRateWindow";
    WNDCLASS wc = { };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CLASS_NAME, L"Heart Rate", WS_POPUP,
        100, 100, 400, 150,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;
    g_hWnd = hwnd;

    RenderLayeredWindow();
    ShowWindow(hwnd, SW_SHOW);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(g_gdiplusToken);
    if (SHOW_CONSOLE) FreeConsole();
    return 0;
}
