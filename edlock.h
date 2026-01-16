#ifndef EDLOCK_H
#define EDLOCK_H

#include <gtk/gtk.h>

void EdLock_Init(GtkWidget *window);
void EdLock_SetLocked(bool locked);
bool EdLock_IsLocked();

#endif
