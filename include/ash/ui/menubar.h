#ifndef ASH_UI_MENUBAR_H
#define ASH_UI_MENUBAR_H

#include <stdint.h>

#include "ash/base/api.h"
#include "ash/fb/fb.h"
#include "ash/term/input.h"

typedef struct ash_menu_item {
    const char *label;
    uint32_t    accel;
    const char *shortcut;
    int8_t      checked;
    int8_t      separator;
} ash_menu_item;

typedef struct ash_menu {
    const char          *title;
    uint32_t             accel;
    const ash_menu_item *items;
    int                  nitems;
} ash_menu;

typedef struct ash_menubar {
    const ash_menu *menus;
    int             nmenus;
    int             open;
    int             hi;
} ash_menubar;

typedef struct ash_menubar_event {
    int menu;
    int item;
} ash_menubar_event;

ASH_API void ash_menubar_init(ash_menubar *mb, const ash_menu *menus, int nmenus);

ASH_API ash_menubar_event ash_menubar_handle(ash_menubar *mb,
                                             const ash_input_event *ev, int w);

ASH_API void ash_menubar_render(const ash_menubar *mb, ash_fb *fb, int w);

#endif
