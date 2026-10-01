/*
 * tray.c -- Routines for providing the tray control.
 */

#include <tchar.h>
#include <windows.h>
#include <shellapi.h>
#include <dbt.h>

#include "tray.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")

#define WM_TRAY_CALLBACK_MESSAGE (WM_USER + 1)
#define WC_TRAY_CLASS_NAME TEXT("StadiaViGEmClass")
#define WC_TRAY_MUTEX_NAME TEXT("Stadia Controller")
#define ID_TRAY_FIRST 1000

static WNDCLASSEX wc;
static NOTIFYICONDATA nid;
static HWND window_handle = NULL;
static HMENU hmenu = NULL;
static HANDLE hmutex;
static HDEVNOTIFY hdevntf;

#define MAX_MENU_BITMAPS 16
static HBITMAP menu_bitmaps[MAX_MENU_BITMAPS];
static int menu_bitmap_count = 0;

static void (*devntf_cb)(UINT op, LPTSTR path) = NULL;
static void (*click_cb)(POINT anchor) = NULL;

static LRESULT CALLBACK _tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_CLOSE:
        if (hdevntf != NULL)
        {
            UnregisterDeviceNotification(hdevntf);
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_TRAY_CALLBACK_MESSAGE:
        if (lparam == WM_LBUTTONUP || lparam == WM_RBUTTONUP)
        {
            POINT p;
            GetCursorPos(&p);
            SetForegroundWindow(hwnd);
            if (click_cb != NULL)
            {
                click_cb(p);
                return 0;
            }
            BOOL cmd = TrackPopupMenu(hmenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                      p.x, p.y, 0, hwnd, NULL);
            SendMessage(hwnd, WM_COMMAND, cmd, 0);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (wparam >= ID_TRAY_FIRST)
        {
            MENUITEMINFO item = {
                .cbSize = sizeof(MENUITEMINFO),
                .fMask = MIIM_ID | MIIM_DATA,
            };
            if (GetMenuItemInfo(hmenu, wparam, FALSE, &item))
            {
                struct tray_menu *menu = (struct tray_menu *)item.dwItemData;
                if (menu != NULL && menu->cb != NULL)
                {
                    menu->cb(menu);
                }
            }
            return 0;
        }
        break;
    case WM_DEVICECHANGE:
        if (devntf_cb != NULL)
        {
            UINT op = DO_TRAY_UNKNOWN;
            switch (wparam)
            {
            case DBT_DEVICEARRIVAL:
                op = DO_TRAY_DEV_ATTACHED;
                break;
            case DBT_DEVICEREMOVECOMPLETE:
                op = DO_TRAY_DEV_REMOVED;
                break;
            }
            LPTSTR path = NULL;
            if (lparam != 0)
            {
                PDEV_BROADCAST_DEVICEINTERFACE bdi = (PDEV_BROADCAST_DEVICEINTERFACE)lparam;
                path = bdi->dbcc_name;
            }
            devntf_cb(op, path);
        }
        break;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

/* Convierte un icono en un bitmap ARGB premultiplicado para usarlo en un item de menu. */
static HBITMAP _icon_to_menu_bitmap(LPTSTR name)
{
    int size = GetSystemMetrics(SM_CXICON);
    HICON hicon = (HICON)LoadImage(wc.hInstance, name, IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    if (hicon == NULL)
    {
        return NULL;
    }

    HBITMAP result = NULL;
    ICONINFO ii;
    if (GetIconInfo(hicon, &ii))
    {
        BITMAP bm;
        if (ii.hbmColor != NULL && GetObject(ii.hbmColor, sizeof(bm), &bm) != 0 && bm.bmWidth > 0 && bm.bmHeight > 0)
        {
            int w = bm.bmWidth, h = bm.bmHeight;
            BITMAPINFO bi;
            memset(&bi, 0, sizeof(bi));
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h; /* de arriba hacia abajo */
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;

            BYTE *src = (BYTE *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)w * h * 4);
            HDC dc = GetDC(NULL);
            if (src != NULL && dc != NULL && GetDIBits(dc, ii.hbmColor, 0, h, src, &bi, DIB_RGB_COLORS) == h)
            {
                BYTE *dst = NULL;
                result = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&dst, NULL, 0);
                if (result != NULL && dst != NULL)
                {
                    BOOL has_alpha = FALSE;
                    for (int i = 0; i < w * h; i++)
                    {
                        if (src[i * 4 + 3] != 0)
                        {
                            has_alpha = TRUE;
                            break;
                        }
                    }
                    for (int i = 0; i < w * h; i++)
                    {
                        BYTE a = has_alpha ? src[i * 4 + 3] : 255;
                        dst[i * 4 + 0] = (BYTE)(src[i * 4 + 0] * a / 255);
                        dst[i * 4 + 1] = (BYTE)(src[i * 4 + 1] * a / 255);
                        dst[i * 4 + 2] = (BYTE)(src[i * 4 + 2] * a / 255);
                        dst[i * 4 + 3] = a;
                    }
                }
            }
            if (dc != NULL)
            {
                ReleaseDC(NULL, dc);
            }
            if (src != NULL)
            {
                HeapFree(GetProcessHeap(), 0, src);
            }
        }
        if (ii.hbmColor != NULL)
        {
            DeleteObject(ii.hbmColor);
        }
        if (ii.hbmMask != NULL)
        {
            DeleteObject(ii.hbmMask);
        }
    }
    DestroyIcon(hicon);
    return result;
}

static HMENU _tray_menu(struct tray_menu *m, UINT *id)
{
    HMENU new_menu = CreatePopupMenu();
    for (; m != NULL && m->text != NULL; m++, (*id)++)
    {
        if (_tcscmp(m->text, TEXT("-")) == 0)
        {
            InsertMenu(new_menu, *id, MF_SEPARATOR, TRUE, TEXT(""));
        }
        else
        {
            MENUITEMINFO item;
            memset(&item, 0, sizeof(item));
            item.cbSize = sizeof(MENUITEMINFO);
            item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STRING | MIIM_STATE | MIIM_DATA;
            item.fType = 0;
            item.fState = 0;
            if (m->submenu != NULL)
            {
                item.fMask = item.fMask | MIIM_SUBMENU;
                item.hSubMenu = _tray_menu(m->submenu, id);
            }
            if (m->disabled)
            {
                item.fState |= MFS_DISABLED;
            }
            if (m->checked)
            {
                item.fState |= MFS_CHECKED;
            }
            if (m->icon != NULL)
            {
                HBITMAP bmp = _icon_to_menu_bitmap(m->icon);
                if (bmp != NULL && menu_bitmap_count < MAX_MENU_BITMAPS)
                {
                    menu_bitmaps[menu_bitmap_count++] = bmp;
                    item.fMask |= MIIM_BITMAP;
                    item.hbmpItem = bmp;
                }
                else if (bmp != NULL)
                {
                    DeleteObject(bmp);
                }
            }
            item.wID = *id;
            item.dwTypeData = m->text;
            item.dwItemData = (ULONG_PTR)m;

            InsertMenuItem(new_menu, *id, TRUE, &item);
        }
    }
    return new_menu;
}

void tray_set_click_handler(void (*cb)(POINT anchor))
{
    click_cb = cb;
}

int tray_init(struct tray *tray)
{
    hmutex = CreateMutex(NULL, TRUE, WC_TRAY_MUTEX_NAME);
    if (hmutex == NULL)
    {
        return -1;
    }

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = _tray_wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = WC_TRAY_CLASS_NAME;
    if (!RegisterClassEx(&wc))
    {
        return -1;
    }

    window_handle = CreateWindowEx(0, WC_TRAY_CLASS_NAME, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (window_handle == NULL)
    {
        return -1;
    }
    UpdateWindow(window_handle);

    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = window_handle;
    nid.uID = 0;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_CALLBACK_MESSAGE;
    Shell_NotifyIcon(NIM_ADD, &nid);

    tray_update(tray);
    return 0;
}

int tray_loop(BOOLEAN blocking)
{
    MSG msg;
    if (blocking)
    {
        GetMessage(&msg, NULL, 0, 0);
    }
    else
    {
        PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
    }
    if (msg.message == WM_QUIT)
    {
        return -1;
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
    return 0;
}

void tray_update(struct tray *tray)
{
    HMENU prevmenu = hmenu;
    HBITMAP old_bitmaps[MAX_MENU_BITMAPS];
    int old_bitmap_count = menu_bitmap_count;
    memcpy(old_bitmaps, menu_bitmaps, sizeof(old_bitmaps));
    menu_bitmap_count = 0;
    UINT id = ID_TRAY_FIRST;
    hmenu = _tray_menu(tray->menu, &id);
    SendMessage(window_handle, WM_INITMENUPOPUP, (WPARAM)hmenu, 0);
    HICON hicon = (HICON)
    LoadImage(
        wc.hInstance, 
        tray->icon, 
        IMAGE_ICON, 
        GetSystemMetrics(SM_CXSMICON), 
        GetSystemMetrics(SM_CYSMICON), 
        LR_DEFAULTCOLOR
    );
    if (nid.hIcon)
    {
        DestroyIcon(nid.hIcon);
    }
    nid.hIcon = hicon;
    _tcscpy(nid.szTip, tray->tip);
    Shell_NotifyIcon(NIM_MODIFY, &nid);

    if (prevmenu != NULL)
    {
        DestroyMenu(prevmenu);
    }
    for (int i = 0; i < old_bitmap_count; i++)
    {
        DeleteObject(old_bitmaps[i]);
    }
}

void tray_exit()
{
    Shell_NotifyIcon(NIM_DELETE, &nid);
    if (nid.hIcon != 0)
    {
        DestroyIcon(nid.hIcon);
    }
    if (hmenu != 0)
    {
        DestroyMenu(hmenu);
    }
    PostQuitMessage(0);
    window_handle = NULL;
    UnregisterClass(WC_TRAY_CLASS_NAME, GetModuleHandle(NULL));
    ReleaseMutex(hmutex);
    CloseHandle(hmutex);
}

void tray_register_device_notification(GUID filter, void (*cb)(UINT, LPTSTR))
{
    if (window_handle == NULL)
    {
        return;
    }

    DEV_BROADCAST_DEVICEINTERFACE dbdi;
    memset(&dbdi, 0, sizeof(dbdi));
    dbdi.dbcc_size = sizeof(dbdi);
    dbdi.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    dbdi.dbcc_classguid = filter;

    hdevntf = RegisterDeviceNotification(window_handle, &dbdi, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (hdevntf != NULL)
    {
        devntf_cb = cb;
    }
}

void tray_show_notification(UINT type, LPTSTR title, LPTSTR text)
{
    if (window_handle == NULL)
    {
        return;
    }

    NOTIFYICONDATA nid_info;
    memmove(&nid_info, &nid, sizeof(NOTIFYICONDATA));
    nid_info.uFlags |= NIF_INFO;
    _tcscpy(nid_info.szInfoTitle, title);
    _tcscpy(nid_info.szInfo, text);
    switch (type)
    {
    case NT_TRAY_INFO:
        nid_info.dwInfoFlags = NIIF_INFO;
        break;
    case NT_TRAY_WARNING:
        nid_info.dwInfoFlags = NIIF_WARNING;
        break;
    case NT_TRAY_ERROR:
        nid_info.dwInfoFlags = NIIF_ERROR;
        break;
    }
    nid_info.uTimeout = 10000;
    Shell_NotifyIcon(NIM_MODIFY, &nid_info);
}
