# XLCPP Linux

这是一个 Windows 心率监控小工具 (`XLCPP`) 的 Linux 移植版本。
它能够通过蓝牙 (BLE) 读取小米手环4-7的实时心率，并在桌面上以**透明悬浮窗**的形式显示，同时提供 HTTP API 供 OBS 等软件调用。

## ✨ 功能特点

*   **❤️ 实时心率**: 连接 BLE 设备读取心率数据。
*   **🖼️ 透明悬浮**: 无边框、背景完全透明的桌面覆盖层（Overlay）。
*   **🖱️ 鼠标穿透**: 默认“锁定模式”下，鼠标点击直达下方窗口，不影响正常操作（Click-Through）。
*   **🖐️ 自由拖拽**: 可切换至“编辑模式”自由拖动窗口位置。
*   **🌐 HTTP 服务**: 内置 Web 服务器 (端口 8080)，提供 JSON API 和网页视图。
*   **Tray 托盘**: 系统托盘图标支持，方便控制显隐和退出。

## 🛠️ 依赖项

构建和运行本项目需要安装以下依赖（以 Ubuntu/Debian 为例）：

```bash
sudo apt install build-essential libgtk-3-dev libappindicator3-dev pkg-config
```

*   `libgtk-3-dev`: GUI 界面
*   `libappindicator3-dev`: 系统托盘图标支持
*   `pkg-config`: 编译配置工具

## 🚀 编译与安装

在终端运行：

```bash
make
```

编译成功后会生成可执行文件 `xlcppl` 和启动脚本 `start.sh`。

### 权限配置
Linux 下蓝牙扫描通常需要特权。为了避免每次都使用 sudo，请运行以下命令授予程序蓝牙权限：

```bash
make setcap
# 或者手动执行: sudo setcap cap_net_raw,cap_net_admin+eip ./xlcppl
```

## 🎮 使用指南

为了在 Linux (特别是 Wayland 环境) 下获得最佳的“无边框”和“鼠标穿透”体验，**强烈建议**使用提供的启动脚本运行：

```bash
./start.sh
```

> **注意**: 该脚本强制使用 `GDK_BACKEND=x11` (XWayland)，这是目前在 GNOME/KDE Wayland 下实现完美透明和点击穿透的最可靠方案。

### 移动窗口 (编辑模式)
程序启动后默认处于 **锁定模式 (Lock Mode)**，此时窗口是“透明”的，无法被点击或拖动。

若要移动窗口：
1.  在**系统托盘**的心形图标上**右键**点击。
2.  取消勾选 **“锁定模式 (Lock Mode)”**。
3.  此时窗口会显示半透明灰色背景，**按住左键**即可拖动窗口。
4.  调整到合适位置后，再次右键勾选 **“锁定模式”** 以恢复穿透并固定位置。

## 🔌 HTTP 接口

程序启动后监听 `8080` 端口：

*   **API 数据**: `http://localhost:8080/api`
    *   返回 JSON: `{"heart_rate": 90, "device": "Mi Smart Band", "timestamp": 12345678}`
*   **网页视图**: `http://localhost:8080/`
    *   一个简单的网页，动态显示心率跳动效果，可用于 OBS 浏览器源。

## 📝 常见问题

**Q: 运行后窗口有黑色边框？**
A: 请确保使用 `./start.sh` 启动。纯 Wayland 模式下窗口装饰很难完全去除。

**Q: 点击图标没反应？**
A: 检查托盘区（某些桌面环境需要额外插件才能显示 AppIndicator 图标，如 GNOME 的 AppIndicator Support 扩展）。

**Q: 蓝牙扫描不到设备？**
A: 请确保已执行 `make setcap`，且蓝牙适配器未被其他程序独占占用。确保您的设备正在广播 BLE 信号。

## 📄 许可证
MIT
