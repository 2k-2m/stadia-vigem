/*
 * popup.c -- Custom flyout window shown when the tray icon is clicked.
 */

#include <stdio.h>
#include <tchar.h>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include "popup.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

#define POPUP_CLASS_NAME TEXT("StadiaViGEmPopup")
#define TITLE_TEXT TEXT("Stadia Controller")
#define STATUS_TEXT TEXT("Connected")
#define NONE_TEXT TEXT("No controller connected")
#define NONE_ICON TEXT("ICON_ERROR")
#define GLYPH_REFRESH TEXT("\xE72C")
#define GLYPH_QUIT TEXT("\xE711")
#define GLYPH_HIDE TEXT("\xE70D")

/* medidas en pixeles a 100% de escala */
#define PAD 16
#define HEADER_H 40
#define ROW_H 52
#define ICON_PX 40
#define GAP 12
#define FOOT_H 38
#define MIN_W 220

#define SC(v) MulDiv((v), dpi, 96)

enum hit
{
    HIT_NONE = 0,
    HIT_REFRESH,
    HIT_QUIT,
    HIT_HIDE
};

struct theme
{
    COLORREF bg, text, subtext, ok, hover, sep;
};

static const struct theme theme_light = {RGB(252, 252, 252), RGB(30, 30, 30), RGB(110, 114, 120),
                                         RGB(46, 125, 50), RGB(235, 238, 242), RGB(222, 224, 228)};
static const struct theme theme_dark = {RGB(44, 44, 44), RGB(240, 240, 240), RGB(160, 164, 170),
                                        RGB(110, 200, 120), RGB(64, 64, 64), RGB(72, 72, 72)};

struct layout
{
    int w, h;
    int header_y;
    int sep1_y;
    int rows_y;
    int sep2_y;
    int foot_y;
    RECT refresh, quit, hide;
};

static HWND hwnd_popup = NULL;
static HINSTANCE hinst = NULL;
static void (*refresh_action)(void) = NULL;
static void (*quit_action)(void) = NULL;

static struct popup_item items[POPUP_MAX_ITEMS];
static int item_count = 0;
static int max_items = POPUP_MAX_ITEMS;
static int hover = HIT_NONE;
static BOOL tracking = FALSE;
static DWORD last_hide_tick = 0;
static int (*provider)(struct popup_item *out, int *max_count) = NULL;
#define WM_POPUP_REFRESH (WM_APP + 1)
static int dpi = 96;
static struct layout L;
static HFONT font_text = NULL, font_bold = NULL, font_small = NULL, font_icon = NULL, font_btn = NULL;

static HFONT make_font(const TCHAR *face, int px, int weight)
{
    return CreateFont(-SC(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static BOOL system_uses_light_theme(void)
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    if (RegGetValue(HKEY_CURRENT_USER, TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                    TEXT("SystemUsesLightTheme"), RRF_RT_REG_DWORD, NULL, &value, &size) != ERROR_SUCCESS)
    {
        return TRUE;
    }
    return value != 0;
}

static int text_width(HDC hdc, HFONT font, const TCHAR *text)
{
    SIZE sz;
    HGDIOBJ old = SelectObject(hdc, font);
    GetTextExtentPoint32(hdc, text, (int)_tcslen(text), &sz);
    SelectObject(hdc, old);
    return sz.cx;
}

static void compute_layout(HDC hdc)
{
    int pad = SC(PAD);
    int icon = SC(ICON_PX);
    int gap = SC(GAP);
    int rows = item_count > 0 ? item_count : 1;
    int content = 0;

    for (int i = 0; i < item_count; i++)
    {
        int w = text_width(hdc, font_text, items[i].name) + SC(16) + text_width(hdc, font_small, STATUS_TEXT);
        if (w > content)
        {
            content = w;
        }
    }
    if (item_count == 0)
    {
        content = text_width(hdc, font_text, NONE_TEXT);
    }
    int header = text_width(hdc, font_bold, TITLE_TEXT) + SC(24) + text_width(hdc, font_small, TEXT("4 / 4"));
    int inner = content + icon + gap;
    if (header > inner)
    {
        inner = header;
    }

    L.w = pad * 2 + inner;
    if (L.w < SC(MIN_W))
    {
        L.w = SC(MIN_W);
    }

    int y = SC(6);
    L.header_y = y;
    y += SC(HEADER_H);
    L.sep1_y = y;
    y += 1 + SC(4);
    L.rows_y = y;
    y += rows * SC(ROW_H);
    y += SC(4);
    L.sep2_y = y;
    y += 1 + SC(6);
    L.foot_y = y;
    y += SC(FOOT_H);
    L.h = y + SC(8);

    {
        int fx = SC(8);
        int fw = (L.w - SC(16)) / 3;
        SetRect(&L.refresh, fx, L.foot_y, fx + fw, L.foot_y + SC(FOOT_H));
        SetRect(&L.quit, fx + fw, L.foot_y, fx + fw * 2, L.foot_y + SC(FOOT_H));
        SetRect(&L.hide, fx + fw * 2, L.foot_y, L.w - SC(8), L.foot_y + SC(FOOT_H));
    }
}

static void fill_rect(HDC hdc, int l, int t, int r, int b, COLORREF c)
{
    RECT rc = {l, t, r, b};
    HBRUSH brush = CreateSolidBrush(c);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
}

static void fill_round(HDC hdc, const RECT *r, COLORREF c, int radius)
{
    HBRUSH brush = CreateSolidBrush(c);
    HGDIOBJ old_brush = SelectObject(hdc, brush);
    HGDIOBJ old_pen = SelectObject(hdc, GetStockObject(NULL_PEN));
    RoundRect(hdc, r->left, r->top, r->right + 1, r->bottom + 1, radius * 2, radius * 2);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(brush);
}

static void draw_icon(HDC hdc, LPTSTR name, int x, int y, int size)
{
    HICON icon = (HICON)LoadImage(hinst, name, IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    if (icon != NULL)
    {
        DrawIconEx(hdc, x, y, icon, size, size, 0, NULL, DI_NORMAL);
        DestroyIcon(icon);
    }
}

static void draw_text(HDC hdc, HFONT font, COLORREF color, const TCHAR *text, RECT *rc, UINT flags)
{
    HGDIOBJ old = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    DrawText(hdc, text, -1, rc, flags | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(hdc, old);
}

static void draw_footer_button(HDC hdc, const struct theme *th, const RECT *area, const TCHAR *glyph,
                               const TCHAR *label, BOOL hovered)
{
    if (hovered)
    {
        RECT r = *area;
        InflateRect(&r, -SC(2), -SC(2));
        fill_round(hdc, &r, th->hover, SC(6));
    }
    int glyph_w = SC(14);
    int gap = SC(6);
    int label_w = text_width(hdc, font_btn, label);
    int total = glyph_w + gap + label_w;
    int x = (area->left + area->right - total) / 2;

    RECT rg = {x, area->top, x + glyph_w, area->bottom};
    draw_text(hdc, font_icon, th->text, glyph, &rg, DT_CENTER);
    RECT rl = {x + glyph_w + gap, area->top, x + total + gap, area->bottom};
    draw_text(hdc, font_btn, th->text, label, &rl, DT_LEFT);
}

static void paint(HDC hdc)
{
    RECT rc;
    GetClientRect(hwnd_popup, &rc);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ old_bmp = SelectObject(mem, bmp);
    const struct theme *th = system_uses_light_theme() ? &theme_light : &theme_dark;

    fill_rect(mem, 0, 0, rc.right, rc.bottom, th->bg);
    SetBkMode(mem, TRANSPARENT);

    int pad = SC(PAD);
    int icon = SC(ICON_PX);
    int gap = SC(GAP);

    /* encabezado */
    RECT rt = {pad, L.header_y, L.w - pad, L.header_y + SC(HEADER_H)};
    draw_text(mem, font_bold, th->text, TITLE_TEXT, &rt, DT_LEFT);
    TCHAR count_text[16];
    _sntprintf(count_text, 16, TEXT("%d / %d"), item_count, max_items);
    draw_text(mem, font_small, th->subtext, count_text, &rt, DT_RIGHT);

    fill_rect(mem, SC(8), L.sep1_y, L.w - SC(8), L.sep1_y + 1, th->sep);

    /* mandos */
    if (item_count == 0)
    {
        int ry = L.rows_y;
        draw_icon(mem, NONE_ICON, pad, ry + (SC(ROW_H) - icon) / 2, icon);
        RECT rn = {pad + icon + gap, ry, L.w - pad, ry + SC(ROW_H)};
        draw_text(mem, font_text, th->text, NONE_TEXT, &rn, DT_LEFT);
    }
    for (int i = 0; i < item_count; i++)
    {
        int ry = L.rows_y + i * SC(ROW_H);
        draw_icon(mem, items[i].icon, pad, ry + (SC(ROW_H) - icon) / 2, icon);
        RECT rn = {pad + icon + gap, ry, L.w - pad, ry + SC(ROW_H)};
        RECT rname = {rn.left, ry + SC(6), rn.right, ry + SC(28)};
        RECT rconn = {rn.left, ry + SC(26), rn.right, ry + SC(46)};
        draw_text(mem, font_text, th->text, items[i].name, &rname, DT_LEFT);
        draw_text(mem, font_small, th->subtext, items[i].conn, &rconn, DT_LEFT);
        draw_text(mem, font_small, th->ok, STATUS_TEXT, &rn, DT_RIGHT);
    }

    /* pie: Refresh y Quit en una fila, sin separador entre ellos */
    fill_rect(mem, SC(8), L.sep2_y, L.w - SC(8), L.sep2_y + 1, th->sep);
    draw_footer_button(mem, th, &L.refresh, GLYPH_REFRESH, TEXT("Refresh"), hover == HIT_REFRESH);
    draw_footer_button(mem, th, &L.quit, GLYPH_QUIT, TEXT("Quit"), hover == HIT_QUIT);
    draw_footer_button(mem, th, &L.hide, GLYPH_HIDE, TEXT("Hide"), hover == HIT_HIDE);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static int hit_test(int x, int y)
{
    POINT p = {x, y};
    if (PtInRect(&L.hide, p))
    {
        return HIT_HIDE;
    }
    if (PtInRect(&L.refresh, p))
    {
        return HIT_REFRESH;
    }
    if (PtInRect(&L.quit, p))
    {
        return HIT_QUIT;
    }
    return HIT_NONE;
}

static void reload_visible(void)
{
    struct popup_item tmp[POPUP_MAX_ITEMS];
    int mx = max_items;
    int n = provider(tmp, &mx);
    if (n > POPUP_MAX_ITEMS)
    {
        n = POPUP_MAX_ITEMS;
    }
    item_count = n;
    max_items = mx;
    for (int i = 0; i < n; i++)
    {
        items[i] = tmp[i];
    }

    RECT old;
    GetWindowRect(hwnd_popup, &old);
    HDC hdc = GetDC(NULL);
    compute_layout(hdc);
    ReleaseDC(NULL, hdc);

    /* mantiene el borde inferior y el centro horizontal donde esta (o donde lo arrastraron) */
    int cx = (old.left + old.right) / 2;
    int x = cx - L.w / 2;
    int y = old.bottom - L.h;
    SetWindowPos(hwnd_popup, HWND_TOPMOST, x, y, L.w, L.h, SWP_NOACTIVATE);
    InvalidateRect(hwnd_popup, NULL, FALSE);
}

static LRESULT CALLBACK popup_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_POPUP_REFRESH:
        if (IsWindowVisible(hwnd) && provider != NULL)
        {
            reload_visible();
        }
        return 0;
    case WM_NCHITTEST:
    {
        /* se puede arrastrar desde cualquier parte, menos desde los botones */
        POINT p = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd, &p);
        if (hit_test(p.x, p.y) != HIT_NONE)
        {
            return HTCLIENT;
        }
        return HTCAPTION;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (!tracking)
        {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            tracking = TRUE;
        }
        int h = hit_test(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (h != hover)
        {
            hover = h;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking = FALSE;
        if (hover != HIT_NONE)
        {
            hover = HIT_NONE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
    {
        int h = hit_test(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (h == HIT_HIDE)
        {
            popup_hide();
        }
        else if (h == HIT_REFRESH)
        {
            if (refresh_action != NULL)
            {
                refresh_action();
            }
        }
        else if (h == HIT_QUIT)
        {
            popup_hide();
            if (quit_action != NULL)
            {
                quit_action();
            }
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE)
        {
            popup_hide();
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

BOOL popup_init(void (*refresh_cb)(void), void (*quit_cb)(void))
{
    refresh_action = refresh_cb;
    quit_action = quit_cb;
    hinst = GetModuleHandle(NULL);

    HDC hdc = GetDC(NULL);
    dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    if (dpi <= 0)
    {
        dpi = 96;
    }

    WNDCLASSEX wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = popup_wnd_proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = POPUP_CLASS_NAME;
    if (!RegisterClassEx(&wc))
    {
        return FALSE;
    }

    hwnd_popup = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, POPUP_CLASS_NAME, TITLE_TEXT, WS_POPUP, 0, 0, 10,
                                10, NULL, NULL, hinst, NULL);
    if (hwnd_popup == NULL)
    {
        return FALSE;
    }

    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_popup, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    font_text = make_font(TEXT("Segoe UI"), 15, FW_NORMAL);
    font_bold = make_font(TEXT("Segoe UI"), 15, FW_SEMIBOLD);
    font_small = make_font(TEXT("Segoe UI"), 12, FW_NORMAL);
    font_icon = make_font(TEXT("Segoe MDL2 Assets"), 12, FW_NORMAL);
    font_btn = make_font(TEXT("Segoe UI"), 13, FW_NORMAL);
    return TRUE;
}

void popup_set_provider(int (*fn)(struct popup_item *out, int *max_count))
{
    provider = fn;
}

/* seguro desde cualquier hilo */
void popup_request_refresh(void)
{
    if (hwnd_popup != NULL)
    {
        PostMessage(hwnd_popup, WM_POPUP_REFRESH, 0, 0);
    }
}

void popup_hide(void)
{
    if (hwnd_popup != NULL && IsWindowVisible(hwnd_popup))
    {
        ShowWindow(hwnd_popup, SW_HIDE);
        last_hide_tick = GetTickCount();
        hover = HIT_NONE;
    }
}

void popup_show(POINT anchor, const struct popup_item *src, int count, int max_count)
{
    if (hwnd_popup == NULL)
    {
        return;
    }
    /* el panel es fijo: otro clic en el icono lo oculta */
    if (IsWindowVisible(hwnd_popup))
    {
        popup_hide();
        return;
    }
    /* evita reabrir justo despues de ocultarlo */
    if (GetTickCount() - last_hide_tick < 250)
    {
        return;
    }

    item_count = count > POPUP_MAX_ITEMS ? POPUP_MAX_ITEMS : count;
    max_items = max_count;
    for (int i = 0; i < item_count; i++)
    {
        items[i] = src[i];
    }
    hover = HIT_NONE;
    tracking = FALSE;

    HDC hdc = GetDC(NULL);
    compute_layout(hdc);
    ReleaseDC(NULL, hdc);

    HMONITOR mon = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(mon, &mi);
    RECT wa = mi.rcWork;
    RECT mr = mi.rcMonitor;
    int margin = SC(12);
    int x, y;

    if (wa.bottom < mr.bottom) /* barra de tareas abajo */
    {
        x = anchor.x - L.w / 2;
        y = wa.bottom - L.h - margin;
    }
    else if (wa.top > mr.top) /* arriba */
    {
        x = anchor.x - L.w / 2;
        y = wa.top + margin;
    }
    else if (wa.left > mr.left) /* izquierda */
    {
        x = wa.left + margin;
        y = anchor.y - L.h / 2;
    }
    else /* derecha */
    {
        x = wa.right - L.w - margin;
        y = anchor.y - L.h / 2;
    }
    if (x > wa.right - L.w - margin)
    {
        x = wa.right - L.w - margin;
    }
    if (x < wa.left + margin)
    {
        x = wa.left + margin;
    }
    if (y > wa.bottom - L.h - margin)
    {
        y = wa.bottom - L.h - margin;
    }
    if (y < wa.top + margin)
    {
        y = wa.top + margin;
    }

    SetWindowPos(hwnd_popup, HWND_TOPMOST, x, y, L.w, L.h, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd_popup);
    InvalidateRect(hwnd_popup, NULL, FALSE);
}
