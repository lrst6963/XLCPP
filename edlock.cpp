#include "edlock.h"
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

static GtkWidget *g_targetWindow = nullptr;
static bool g_isLocked = true; // 默认为锁定（鼠标穿透）

// 拖动处理程序的前置声明
static gboolean OnButtonPress(GtkWidget *widget, GdkEventButton *event, gpointer data);

void EdLock_UpdateShape() {
    if (!g_targetWindow) return;

    // 仅适用于 X11/XWayland
    GdkScreen *screen = gtk_widget_get_screen(g_targetWindow);
    if (GDK_IS_X11_SCREEN(screen)) {
        if (g_isLocked) {
            // 锁定：输入形状为空 -> 鼠标穿透
            cairo_region_t *region = cairo_region_create();
            gtk_widget_input_shape_combine_region(g_targetWindow, region);
            cairo_region_destroy(region);
        } else {
            // 未锁定：输入形状为 NULL -> 默认（可点击）
            gtk_widget_input_shape_combine_region(g_targetWindow, nullptr);
        }
    }
}

void EdLock_Init(GtkWidget *window) {
    g_targetWindow = window;
    
    // 启用事件
    gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(G_OBJECT(window), "button-press-event", G_CALLBACK(OnButtonPress), nullptr);

    // 初始状态
    EdLock_UpdateShape();
}

void EdLock_SetLocked(bool locked) {
    g_isLocked = locked;
    EdLock_UpdateShape();
    
    // 强制重绘以显示视觉反馈（可选）
    if (g_targetWindow) {
        gtk_widget_queue_draw(g_targetWindow);
    }
}

bool EdLock_IsLocked() {
    return g_isLocked;
}

static gboolean OnButtonPress(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (g_isLocked) {
        // 在锁定模式下，如果输入遮罩工作正常，我们甚至不应该收到此事件。
        // 但如果收到了（例如非 X11），则返回 FALSE 以传播。
        return FALSE; 
    }

    if (event->type == GDK_BUTTON_PRESS && event->button == 1) { // 左键点击
        // 编辑模式：允许拖动
        gtk_window_begin_move_drag(GTK_WINDOW(widget), event->button, event->x_root, event->y_root, event->time);
        return TRUE;
    }
    
    return FALSE; 
}
