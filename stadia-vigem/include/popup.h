/*
 * popup.h -- Custom flyout window shown when the tray icon is clicked.
 */

#ifndef POPUP_H
#define POPUP_H

#include <windows.h>

#define POPUP_MAX_ITEMS 4

struct popup_item
{
    LPTSTR icon; /* nombre del recurso ICON del mando */
    TCHAR name[32];
    TCHAR conn[16];
};

BOOL popup_init(void (*refresh_cb)(void), void (*quit_cb)(void));
void popup_show(POINT anchor, const struct popup_item *items, int count, int max_count);
void popup_hide(void);
void popup_set_provider(int (*fn)(struct popup_item *out, int *max_count));
void popup_request_refresh(void);

#endif /* POPUP_H */
