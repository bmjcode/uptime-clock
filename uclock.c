/*
 * Clock with system uptime display.
 * Copyright (c) 2023 Benjamin Johnson <bmjcode@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * To compile:
 * gcc -Os -mwindows -o uclock.exe uclock.c
 */

#include <time.h>
#include <wchar.h>

#define UNICODE
#include <windows.h>

// Window class name
#define CLASS_NAME L"Uptime Clock"

// Clock format: 03/30/2023 12:34:56 AM (22 chars)
#define CLOCK_FMT L"%m/%d/%Y %I:%M:%S %p"

// Uptime format: 365 d, 23 hr, 59 min, 59 sec (28 chars)
// We're unlikely to see more than a three-digit day count, and if Windows
// has been running that long without rebooting, we've got other problems.
#define UPTIME_FMT L"%lld d, %lld hr, %lld min, %lld sec"

// Timer numbers
#define IDT_REFRESH 1

// Unit conversions
#define MSEC_PER_SEC 1000
#define MSEC_PER_MIN ((MSEC_PER_SEC) * 60)
#define MSEC_PER_HR ((MSEC_PER_MIN) * 60)
#define MSEC_PER_DAY ((MSEC_PER_HR) * 60)

// Structure to keep track of window elements
struct clock_window {
    HWND hwnd;
    HWND hwndClock;
    HWND hwndUptimeLabel;
    HWND hwndUptime;
    HFONT hFontClock;
    HFONT hFontUptime;
};

static int CreateClockWindow(HWND hwnd);
static void DestroyClockWindow(struct clock_window *window);

static LRESULT CALLBACK ClockWindowProc(HWND hwnd, UINT uMsg,
                                        WPARAM wParam, LPARAM lParam);
static void PaintClock(struct clock_window *window);
static void RefreshClock(struct clock_window *window);
static void ResizeClock(struct clock_window *window);

/*
 * Create the clock window.
 * Processes WM_CREATE for ClockWindowProc().
 * Returns 0 on success, -1 on failure.
 */
int
CreateClockWindow(HWND hwnd)
{
    struct clock_window *window;
    SYSTEMTIME lt;

    window = malloc(sizeof(struct clock_window));
    memset(window, 0, sizeof(struct clock_window));

    // Save a pointer to our window structure for ClockWindowProc()
    window->hwnd = hwnd;
    SetWindowLongPtr(window->hwnd, GWLP_USERDATA, (LONG_PTR) window);

    window->hwndClock = CreateWindowEx(
        /* dwExStyle */     0,
        /* lpClassName */   L"STATIC",
        /* lpWindowName */  NULL,
        /* dwStyle */       WS_CHILD | SS_CENTER,
        /* X */             CW_USEDEFAULT,
        /* Y */             CW_USEDEFAULT,
        /* nWidth */        CW_USEDEFAULT,
        /* nHeight */       CW_USEDEFAULT,
        /* hwndParent */    window->hwnd,
        /* hMenu */         NULL,
        /* hInstance */     NULL,
        /* lpParam */       NULL
    );

    if (window->hwndClock == NULL)
        return -1;

    window->hwndUptimeLabel = CreateWindowEx(
        /* dwExStyle */     0,
        /* lpClassName */   L"STATIC",
        /* lpWindowName */  L"System Uptime",
        /* dwStyle */       WS_CHILD | SS_CENTER,
        /* X */             CW_USEDEFAULT,
        /* Y */             CW_USEDEFAULT,
        /* nWidth */        CW_USEDEFAULT,
        /* nHeight */       CW_USEDEFAULT,
        /* hwndParent */    window->hwnd,
        /* hMenu */         NULL,
        /* hInstance */     NULL,
        /* lpParam */       NULL
    );

    if (window->hwndUptimeLabel == NULL)
        return -1;

    window->hwndUptime = CreateWindowEx(
        /* dwExStyle */     0,
        /* lpClassName */   L"STATIC",
        /* lpWindowName */  NULL,
        /* dwStyle */       WS_CHILD | SS_CENTER,
        /* X */             CW_USEDEFAULT,
        /* Y */             CW_USEDEFAULT,
        /* nWidth */        CW_USEDEFAULT,
        /* nHeight */       CW_USEDEFAULT,
        /* hwndParent */    window->hwnd,
        /* hMenu */         NULL,
        /* hInstance */     NULL,
        /* lpParam */       NULL
    );

    if (window->hwndUptime == NULL)
        return -1;

    // Size display widgets
    ResizeClock(window);
    ShowWindow(window->hwndClock, SW_SHOW);
    ShowWindow(window->hwndUptimeLabel, SW_SHOW);
    ShowWindow(window->hwndUptime, SW_SHOW);

    // Synchronize the display within 10ms
    do {
        GetLocalTime(&lt);
        Sleep(2);
    } while (lt.wMilliseconds % 1000 > 10);

    // Display the clock and set a timer to keep it updated
    RefreshClock(window);
    SetTimer(window->hwnd, IDT_REFRESH, 1000, (TIMERPROC) NULL);

    return 0;
}

/*
 * Destroy the clock window.
 * Processes WM_DESTROY for ClockWindowProc().
 */
void
DestroyClockWindow(struct clock_window *window)
{
    // Note that destroying a window also destroys its child windows,
    // so we don't have to clean all those up manually.

    if (window->hwnd != NULL)
        KillTimer(window->hwnd, IDT_REFRESH);

    // Delete the fonts after all window objects using them are gone
    if (window->hFontClock != NULL)
        DeleteObject(window->hFontClock);
    if (window->hFontUptime != NULL)
        DeleteObject(window->hFontUptime);

    // Delete the window structure last
    if (window != NULL)
        free(window);
}

/*
 * Process clock window messages.
 */
LRESULT CALLBACK
ClockWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    struct clock_window *window =
        (uMsg == WM_CREATE) ? NULL :
        (struct clock_window*) GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (uMsg) {
        case WM_CREATE:
            return CreateClockWindow(hwnd);

        case WM_KEYDOWN:
            switch (wParam) {
                case VK_ESCAPE:
                case VK_CONTROL | 'q':
                case VK_CONTROL | 'Q':
                case VK_CONTROL | 'w':
                case VK_CONTROL | 'W':
                    // Close the window when Esc, Ctrl+Q, or Ctrl+W is pressed
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;

        case WM_PAINT:
            PaintClock(window);
            return 0;

        case WM_SIZE:
            ResizeClock(window);
            return 0;

        case WM_TIMER:
            switch (wParam) {
                case IDT_REFRESH:
                    RefreshClock(window);
                    break;
            }
            return 0;

        case WM_DESTROY:
            DestroyClockWindow(window);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

/*
 * Paint the clock window.
 * Processes WM_PAINT for ClockWindowProc().
 */
static void
PaintClock(struct clock_window *window)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(window->hwnd, &ps);

    FillRect(hdc, &ps.rcPaint, (HBRUSH) (COLOR_3DFACE + 1));
    EndPaint(window->hwnd, &ps);
}

/*
 * Refresh the clock display.
 */
void
RefreshClock(struct clock_window *window)
{
#define BUF_LEN 29      // Large enough to hold both CLOCK_FMT and UPTIME_FMT
                        // plus a trailing '\0'
    static wchar_t buf[BUF_LEN];
    time_t now;
    struct tm *timeinfo;
    unsigned long long ticks, days, hours, minutes, seconds;

    // Refresh the clock display
    // Don't free timeinfo -- it's a pointer to static memory
    time(&now);
    timeinfo = localtime(&now);

    if (wcsftime(buf, BUF_LEN, CLOCK_FMT, timeinfo) != 0)
        SetWindowText(window->hwndClock, buf);

    // Refresh the uptime display
    // GetTickCount64() returns the system uptime in milliseconds
    ticks = GetTickCount64();
    days = ticks / MSEC_PER_DAY;
    ticks %= MSEC_PER_DAY;
    hours = ticks / MSEC_PER_HR;
    ticks %= MSEC_PER_HR;
    minutes = ticks / MSEC_PER_MIN;
    ticks %= MSEC_PER_MIN;
    seconds = ticks / MSEC_PER_SEC;

    if (swprintf(buf, BUF_LEN,
                 UPTIME_FMT, days, hours, minutes, seconds) != 0)
        SetWindowText(window->hwndUptime, buf);
#undef BUF_LEN
}

/*
 * Resize the clock display.
 * Processes WM_SIZE for ClockWindowProc().
 */
void
ResizeClock(struct clock_window *window)
{
    int cHeightClock, cHeightUptime, width, height, yPos;
    RECT rect;
    HFONT hOldFont;

    GetClientRect(window->hwnd, &rect);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;

    // Force redrawing the entire window area
    RedrawWindow(window->hwnd, &rect, NULL, RDW_ERASE | RDW_INVALIDATE);

    // Clock and uptime fonts:
    // 1/12 and 1/16 of the window height, respectively
    cHeightClock = height / 12;
    cHeightUptime = height / 16;

    // Center the clock display vertically in the window
    // Format:
    // hwndClock        - cHeightClock
    // (blank line)     - CHeightUptime
    // hwndUptimeLabel  - cHeightUptime
    // hwndUptime       - cHeightUptime
    yPos = (rect.bottom - rect.top - cHeightClock - (3 * cHeightUptime)) / 2;

    MoveWindow(window->hwndClock,
               rect.left, yPos, width, cHeightClock, TRUE);
    yPos += cHeightClock + cHeightUptime;

    MoveWindow(window->hwndUptimeLabel,
               rect.left, yPos, width, cHeightUptime, TRUE);
    yPos += cHeightUptime;

    MoveWindow(window->hwndUptime,
               rect.left, yPos, width, cHeightUptime, TRUE);

    // Swap out the clock font
    hOldFont = window->hFontClock;
    window->hFontClock = CreateFont(
        /* cHeight */           cHeightClock,
        /* cWidth */            0,
        /* cEscapement */       0,
        /* cOrientation */      0,
        /* cWeight */           FW_REGULAR,
        /* bItalic */           FALSE,
        /* bUnderline */        FALSE,
        /* bStrikeOut */        FALSE,
        /* iCharSet */          DEFAULT_CHARSET,
        /* iOutPrecision */     OUT_DEFAULT_PRECIS,
        /* iClipPrecision */    CLIP_DEFAULT_PRECIS,
        /* iQuality */          DEFAULT_QUALITY,
        /* iPitchAndFamily */   FF_DONTCARE,
        /* pszFaceName */       L"MS Shell Dlg"
    );

    SendMessage(window->hwndClock,
                WM_SETFONT, (WPARAM) window->hFontClock, (LPARAM) NULL);

    if (hOldFont != NULL)
        DeleteObject(hOldFont);

    // Now do the same for the uptime display
    hOldFont = window->hFontUptime;
    window->hFontUptime = CreateFont(
        /* cHeight */           cHeightUptime,
        /* cWidth */            0,
        /* cEscapement */       0,
        /* cOrientation */      0,
        /* cWeight */           FW_REGULAR,
        /* bItalic */           FALSE,
        /* bUnderline */        FALSE,
        /* bStrikeOut */        FALSE,
        /* iCharSet */          DEFAULT_CHARSET,
        /* iOutPrecision */     OUT_DEFAULT_PRECIS,
        /* iClipPrecision */    CLIP_DEFAULT_PRECIS,
        /* iQuality */          DEFAULT_QUALITY,
        /* iPitchAndFamily */   FF_DONTCARE,
        /* pszFaceName */       L"MS Shell Dlg"
    );

    SendMessage(window->hwndUptimeLabel,
                WM_SETFONT, (WPARAM) window->hFontUptime, (LPARAM) NULL);
    SendMessage(window->hwndUptime,
                WM_SETFONT, (WPARAM) window->hFontUptime, (LPARAM) NULL);

    if (hOldFont != NULL)
        DeleteObject(hOldFont);
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
        PSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSW wc = { };
    MSG msg = { };
    HWND hwndClockWindow;

    // Register the Uptime Clock window class
    wc.lpfnWndProc = ClockWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    // Create the clock window
    hwndClockWindow = CreateWindowEx(
        /* dwExStyle */     0,
        /* lpClassName */   CLASS_NAME,
        /* lpWindowName */  L"Uptime Clock",
        /* dwStyle */       WS_OVERLAPPEDWINDOW,
        /* X */             CW_USEDEFAULT,
        /* Y */             CW_USEDEFAULT,
        /* nWidth */        CW_USEDEFAULT,
        /* nHeight */       CW_USEDEFAULT,
        /* hwndParent */    NULL,
        /* hMenu */         NULL,
        /* hInstance */     hInstance,
        /* lpParam */       NULL
    );
    if (hwndClockWindow == NULL)
        return 1;

    // Block screen blanking and sleep timeouts
    SetThreadExecutionState(ES_DISPLAY_REQUIRED
                            | ES_SYSTEM_REQUIRED
                            | ES_CONTINUOUS);

    // Show the clock window
    ShowWindow(hwndClockWindow, nCmdShow);
    SetForegroundWindow(hwndClockWindow);

    // Run the message loop
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Allow screen blanking and sleep timeouts
    SetThreadExecutionState(ES_CONTINUOUS);

    // Clean up and exit
    DestroyWindow(hwndClockWindow);
    return 0;
}
