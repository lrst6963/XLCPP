/*
 * xlcppl.cpp - Linux Port of XLCPP (Heart Rate Monitor)
 * 
 * Dependencies: gtk+-3.0, appindicator3-0.1
 * Compile with: 
 *   g++ -o xlcppl xlcppl.cpp $(pkg-config --cflags --libs gtk+-3.0 appindicator3-0.1) -lpthread
 */

#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <libappindicator/app-indicator.h>
#include <gio/gio.h>
#include "edlock.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

// ================= 配置 =================
const int HTTP_PORT = 8080;
const bool SHOW_CONSOLE = true;

// ================= 全局状态 =================
std::atomic<int> g_sharedHeartRate{0};
std::string g_sharedDeviceName = "扫描中...";
std::string g_displayText = "等待中...";
std::mutex g_dataMutex;

GtkWidget *g_window = nullptr;
GMainLoop *g_mainLoop = nullptr;
AppIndicator *g_indicator = nullptr;

// ================= 辅助函数 =================
void UpdateGuiText(const std::string& text) {
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        g_displayText = text;
    }
    // 在主线程调度重绘
    g_idle_add([](gpointer) -> gboolean {
        if (g_window && gtk_widget_get_visible(g_window)) {
            gtk_widget_queue_draw(g_window);
        }
        return G_SOURCE_REMOVE;
    }, nullptr);
}

// ================= HTTP 服务器 =================
void HttpServerThread() {
    int listenSocket = socket(AF_INET6, SOCK_STREAM, 0);
    if (listenSocket < 0) return;

    int v6only = 0;
    setsockopt(listenSocket, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    int reuse = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in6 serverAddr = {0};
    serverAddr.sin6_family = AF_INET6;
    serverAddr.sin6_port = htons(HTTP_PORT);
    serverAddr.sin6_addr = in6addr_any;

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(listenSocket);
        return;
    }
    if (listen(listenSocket, 10) < 0) {
        close(listenSocket);
        return;
    }

    if (SHOW_CONSOLE) std::cout << "HTTP 服务已启动，端口: " << HTTP_PORT << std::endl;

    while (true) {
        sockaddr_in6 clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &addrLen);
        if (clientSocket < 0) continue;

        char buffer[2048] = {0};
        read(clientSocket, buffer, 2047);
        std::string request(buffer);

        int currentHr = g_sharedHeartRate.load();
        std::string currentDev;
        {
            std::lock_guard<std::mutex> lock(g_dataMutex);
            currentDev = g_sharedDeviceName;
        }

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        long long timestamp = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

        std::string jsonBody = "{\"heart_rate\": " + std::to_string(currentHr) + 
                               ", \"device\": \"" + currentDev + 
                               "\", \"timestamp\": " + std::to_string(timestamp) + "}";
        std::string httpResponse;

        if (request.find("GET /api") != std::string::npos) {
            httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
                           "Access-Control-Allow-Origin: *\r\nConnection: close\r\n"
                           "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n\r\n" + jsonBody;
        } else {
            std::string htmlBody = R"(<!DOCTYPE html><html lang="zh-CN"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>心率监控</title><style>body{background-color:#1a1a1a;color:white;font-family:"Microsoft YaHei",sans-serif;display:flex;flex-direction:column;justify-content:center;align-items:center;height:100vh;margin:0;overflow:hidden}body.transparent{background-color:transparent}#container{text-align:center}.row-container{display:flex;flex-direction:row;justify-content:center;align-items:center;gap:20px}#heart-icon{font-size:80px;color:#ff3333;transition:transform 0.1s;margin-top:10px}#heart-rate{font-size:100px;font-weight:bold;color:#ffffff;line-height:1}#device-name{font-size:24px;color:#cccccc;margin-top:10px;font-weight:normal}@keyframes beat{from{transform:scale(1)}to{transform:scale(1.15)}}</style></head><body><div id="container"><div class="row-container"><div id="heart-icon">&#x2764;</div><div id="heart-rate">--</div></div></div><script>const icon=document.getElementById('heart-icon');const rateText=document.getElementById('heart-rate');const devText=document.getElementById('device-name');async function update(){try{const res=await fetch('/api');const data=await res.json();if(data.heart_rate>0){rateText.innerText=data.heart_rate;if(devText)devText.innerText=data.device;const beatDuration=60/data.heart_rate;icon.style.animation=`beat ${beatDuration/2}s infinite alternate`}else{rateText.innerText="--";if(devText)devText.innerHTML="&#x7B49;&#x5F85;&#x6570;&#x636E;...";icon.style.animation="none"}}catch(e){console.log("Connection lost")}}if(window.location.search.includes('transparent')){document.body.classList.add('transparent')}setInterval(update,1000);update();</script></body></html>)";
            httpResponse = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n"
                           "Content-Length: " + std::to_string(htmlBody.size()) + "\r\n\r\n" + htmlBody;
        }

        send(clientSocket, httpResponse.c_str(), httpResponse.size(), 0);
        close(clientSocket);
    }
    close(listenSocket);
}

// ================= 蓝牙 (BlueZ/DBus) =================
GDBusConnection *g_dbusConn = nullptr;
guint g_propChangedSignalId = 0;
guint g_interfacesAddedSignalId = 0;
bool g_isScanning = false;

void StartDiscovery() {
    if (!g_dbusConn) return;

    GVariant *result = g_dbus_connection_call_sync(
        g_dbusConn,
        "org.bluez",
        "/org/bluez/hci0",
        "org.bluez.Adapter1",
        "StartDiscovery",
        nullptr,
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        nullptr
    );

    if (result) {
        g_variant_unref(result);
        if (SHOW_CONSOLE) std::cout << "已启动蓝牙扫描。" << std::endl;
        g_isScanning = true;
    } else {
        std::cerr << "无法启动扫描。请检查 bluetoothd 是否运行。" << std::endl;
    }
}

void ProcessDeviceProperties(GVariant *props) {
    if (!props) return;

    GVariantDict dict;
    g_variant_dict_init(&dict, props);

    // Check ManufacturerData
    GVariant *manufacturerData = g_variant_dict_lookup_value(&dict, "ManufacturerData", G_VARIANT_TYPE("a{qv}"));
    if (manufacturerData) {
        GVariantIter iter;
        g_variant_iter_init(&iter, manufacturerData);
        uint16_t key;
        GVariant *value;
        while (g_variant_iter_next(&iter, "{qv}", &key, &value)) {
            if (key == 0x0157) { // Target Company ID
                gsize n_elements = 0;
                const guchar *data = (const guchar *)g_variant_get_fixed_array(value, &n_elements, sizeof(guchar));
                if (n_elements >= 4) {
                    uint8_t heartRate = data[3];
                    if (heartRate > 0) {
                        g_sharedHeartRate.store(heartRate);
                        
                        // Parse Name
                        std::string name = "(未知)";
                        GVariant *nameVar = g_variant_dict_lookup_value(&dict, "Name", G_VARIANT_TYPE_STRING); // try Name
                        if (!nameVar) nameVar = g_variant_dict_lookup_value(&dict, "Alias", G_VARIANT_TYPE_STRING); // try Alias
                        
                        if (nameVar) {
                            name = g_variant_get_string(nameVar, nullptr);
                            g_variant_unref(nameVar);
                        }
                        
                        int rssi = 0;
                        GVariant *rssiVar = g_variant_dict_lookup_value(&dict, "RSSI", G_VARIANT_TYPE_INT16);
                        if (rssiVar) {
                            rssi = g_variant_get_int16(rssiVar);
                            g_variant_unref(rssiVar);
                        }

                        {
                            std::lock_guard<std::mutex> lock(g_dataMutex);
                            g_sharedDeviceName = name;
                        }

                        std::string uiText = "\u2764 " + std::to_string(heartRate);
                        UpdateGuiText(uiText);

                        if (SHOW_CONSOLE) {
                            std::cout << "\r" << std::left << std::setw(20) << name << " (" << rssi << "dBm) HR: " << std::setw(3) << (int)heartRate << "    " << std::flush;
                        }
                    }
                }
            }
            g_variant_unref(value);
        }
        g_variant_unref(manufacturerData);
    }
    g_variant_dict_clear(&dict);
}

void OnInterfacesAdded(GDBusConnection *conn, const gchar *sender, const gchar *path,
                       const gchar *iface, const gchar *signal, GVariant *params, gpointer user_data) {
    // params 格式为 (oa{sa{sv}}) - 对象路径，及接口+属性
    GVariantIter *interfaces;
    const gchar *object_path;
    g_variant_get(params, "(&oa{sa{sv}})", &object_path, &interfaces);

    const gchar *interface_name;
    GVariant *properties;
    while (g_variant_iter_next(interfaces, "{&s@a{sv}}", &interface_name, &properties)) {
        if (std::string(interface_name) == "org.bluez.Device1") {
            ProcessDeviceProperties(properties);
        }
        g_variant_unref(properties);
    }
    g_variant_iter_free(interfaces);
}

void OnPropertiesChanged(GDBusConnection *conn, const gchar *sender, const gchar *path,
                         const gchar *iface, const gchar *signal, GVariant *params, gpointer user_data) {
    const gchar *interface_name;
    GVariant *changed_properties = nullptr;
    GVariant *invalidated_properties = nullptr;
    
    // 修复：移除 as 前的 ^ 以获取 GVariant* 而非 gchar**
    g_variant_get(params, "(&s@a{sv}@as)", &interface_name, &changed_properties, &invalidated_properties);

    if (std::string(interface_name) == "org.bluez.Device1") {
        if (changed_properties) {
            ProcessDeviceProperties(changed_properties);
        }
    }

    if (changed_properties) g_variant_unref(changed_properties);
    if (invalidated_properties) g_variant_unref(invalidated_properties);
}


void InitBluetooth() {
    g_dbusConn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    if (!g_dbusConn) {
        std::cerr << "无法连接到系统总线。" << std::endl;
        return;
    }

    g_interfacesAddedSignalId = g_dbus_connection_signal_subscribe(
        g_dbusConn,
        "org.bluez",
        "org.freedesktop.DBus.ObjectManager",
        "InterfacesAdded",
        nullptr,
        nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        OnInterfacesAdded,
        nullptr,
        nullptr
    );

    g_propChangedSignalId = g_dbus_connection_signal_subscribe(
        g_dbusConn,
        "org.bluez",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        nullptr,
        nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        OnPropertiesChanged,
        nullptr,
        nullptr
    );

    StartDiscovery();
}


// ================= GTK 界面 =================
gboolean OnDraw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    // 清除背景以完全透明
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // 使用 Pango 绘制文本（更好的 Unicode 支持）
    std::string text;
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        text = g_displayText;
    }

    // 编辑模式（未锁定）下的视觉反馈
    if (!EdLock_IsLocked()) {
        cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 0.5); // 半透明灰色
        cairo_paint(cr);
    }

    PangoLayout *layout = gtk_widget_create_pango_layout(widget, text.c_str());
    
    // 设置字体描述
    GtkSettings *settings = gtk_settings_get_default();
    gchar *font_name = nullptr;
    g_object_get(settings, "gtk-font-name", &font_name, nullptr);

    PangoFontDescription *fontDesc = nullptr;
    if (font_name) {
        fontDesc = pango_font_description_from_string(font_name);
        g_free(font_name);
    } else {
        fontDesc = pango_font_description_from_string("Sans");
    }

    // 强制设置字体大小为 40pt
    pango_font_description_set_size(fontDesc, 40 * PANGO_SCALE);
    pango_font_description_set_weight(fontDesc, PANGO_WEIGHT_BOLD);
    
    pango_layout_set_font_description(layout, fontDesc);
    pango_font_description_free(fontDesc);

    // 获取文本尺寸
    int width, height;
    pango_layout_get_pixel_size(layout, &width, &height);

    double x = (gtk_widget_get_allocated_width(widget) / 2.0) - (width / 2.0);
    double y = (gtk_widget_get_allocated_height(widget) / 2.0) - (height / 2.0);

    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0); // 红色
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    
    g_object_unref(layout);

    return FALSE;
}

// 保存窗口位置
int g_savedX = -1;
int g_savedY = -1;

void ToggleWindow(GtkWidget *widget, gpointer data) {
    if (gtk_widget_get_visible(g_window)) {
        // 隐藏前保存位置
        gtk_window_get_position(GTK_WINDOW(g_window), &g_savedX, &g_savedY);
        gtk_widget_hide(g_window);
    } else {
        if (g_savedX != -1 && g_savedY != -1) {
            gtk_window_move(GTK_WINDOW(g_window), g_savedX, g_savedY);
        }
        gtk_widget_show_all(g_window);
        // 某些 WM 可能在 show 后重置，再次移动以确保位置
        if (g_savedX != -1 && g_savedY != -1) {
            gtk_window_move(GTK_WINDOW(g_window), g_savedX, g_savedY);
        }
    }
}

void InitUi(int argc, char **argv) {
    GdkScreen *screen = gdk_screen_get_default();
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    
    if (!visual) {
        std::cerr << "屏幕不支持透明通道！" << std::endl;
        visual = gdk_screen_get_system_visual(screen);
    }

    g_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_visual(g_window, visual);
    
    // 用于透明窗口背景的 CSS
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, "window { background-color: rgba(0,0,0,0); }", -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(g_window);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    
    // 窗口配置
    gtk_window_set_title(GTK_WINDOW(g_window), "心率监控");
    gtk_window_set_default_size(GTK_WINDOW(g_window), 400, 150);
    
    // 使用 NORMAL 提示以允许拖动。DOCK/UTILITY 通常在某些 WM 上强制固定位置。
    gtk_window_set_type_hint(GTK_WINDOW(g_window), GDK_WINDOW_TYPE_HINT_NORMAL);
    
    gtk_window_set_decorated(GTK_WINDOW(g_window), FALSE); // 无边框
    gtk_widget_set_app_paintable(g_window, TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(g_window), TRUE); // 始终置顶

    // 由 EdLock 管理的事件
    EdLock_Init(g_window);
    
    // 初始默认为未锁定（编辑模式）以便定位？还是锁定？
    // 用户请求“在托盘显示编辑/锁定模式”。默认为锁定更利于“鼠标穿透”。
    EdLock_SetLocked(true); 

    // 连接绘制信号
    g_signal_connect(G_OBJECT(g_window), "draw", G_CALLBACK(OnDraw), nullptr);
    g_signal_connect(G_OBJECT(g_window), "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    // 初始显示
    gtk_widget_show_all(g_window);

    // 托盘图标 (AppIndicator)
    g_indicator = app_indicator_new("heart-rate-monitor", "heart", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_status(g_indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_icon(g_indicator, "system-monitor");

    GtkWidget *menu = gtk_menu_new();
    
    GtkWidget *toggleItem = gtk_menu_item_new_with_label("显示/隐藏 (Toggle)");
    g_signal_connect(toggleItem, "activate", G_CALLBACK(ToggleWindow), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), toggleItem);

    // 编辑/锁定模式切换
    GtkWidget *lockItem = gtk_check_menu_item_new_with_label("锁定模式 (Lock Mode)");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(lockItem), EdLock_IsLocked());
    g_signal_connect(lockItem, "toggled", G_CALLBACK(+[](GtkCheckMenuItem *item, gpointer) {
        bool active = gtk_check_menu_item_get_active(item);
        EdLock_SetLocked(active);
    }), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), lockItem);

    GtkWidget *quitItem = gtk_menu_item_new_with_label("退出 (Quit)");
    g_signal_connect(quitItem, "activate", G_CALLBACK(gtk_main_quit), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quitItem);

    gtk_widget_show_all(menu);
    app_indicator_set_menu(g_indicator, GTK_MENU(menu));
}

// ================= 单实例检查 =================
#include <sys/file.h>
#include <errno.h>

int g_lockFileFd = -1;

void CheckSingleInstance() {
    g_lockFileFd = open("/tmp/xlcppl.lock", O_CREAT | O_RDWR, 0666);
    if (g_lockFileFd < 0) {
        std::cerr << "无法打开锁文件: " << strerror(errno) << std::endl;
        exit(1);
    }

    int rc = flock(g_lockFileFd, LOCK_EX | LOCK_NB);
    if (rc < 0) {
        if (errno == EWOULDBLOCK) {
            std::cerr << "程序已在运行中 (Instance already running)." << std::endl;
            exit(1);
        } else {
            std::cerr << "无法锁定文件: " << strerror(errno) << std::endl;
            exit(1);
        }
    }
}

int main(int argc, char **argv) {
    CheckSingleInstance();

    gtk_init(&argc, &argv);

    InitBluetooth();
    std::thread(HttpServerThread).detach();
    InitUi(argc, argv);

    gtk_main();

    return 0;
}
