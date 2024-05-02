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
 * gcc -Os -Wall -Werror -mwindows -o uclock.exe uclock.c
 */

#ifdef UNICODE
#  include <wchar.h>
#  define SNPRINTF swprintf
#  define STRFTIME wcsftime
#else
#  include <stdio.h>
#  define SNPRINTF snprintf
#  define STRFTIME strftime
#endif
#include <time.h>

#include <windows.h>

// Window class name
#define CLASS_NAME TEXT("Uptime Clock")

// Clock format: 03/30/2023 12:34:56 AM (22 chars)
#define CLOCK_FMT TEXT("%m/%d/%Y %I:%M:%S %p")

// Uptime format: 365 d, 23 hr, 59 min, 59 sec (28 chars)
// We're unlikely to see more than a three-digit day count. If Windows has
// really been running that long without rebooting, we've got other problems.
#define UPTIME_FMT TEXT("%lld d, %lld hr, %lld min, %lld sec")

// Timer numbers
#define IDT_REFRESH 1

// Unit conversions
#define MSEC_PER_SEC 1000
#define MSEC_PER_MIN ((MSEC_PER_SEC) * 60)
#define MSEC_PER_HR ((MSEC_PER_MIN) * 60)
#define MSEC_PER_DAY ((MSEC_PER_HR) * 24)

// Keyboard accelerators
#define cAccel 2
ACCEL accel[] = {
    { FVIRTKEY,             VK_ESCAPE,  IDCANCEL },
    { FCONTROL | FVIRTKEY,  'W',        IDCANCEL },
};

// Structure to keep track of window elements
typedef struct tagCLOCKWINDOW {
    HWND hwnd;
    HWND hwndClock;
    HWND hwndUptimeLabel;
    HWND hwndUptime;
    HFONT hFontClock;
    HFONT hFontUptime;
} CLOCKWINDOW, *HCLOCKWINDOW;

static LRESULT CALLBACK ClockWindowProc(HWND hwnd, UINT uMsg,
                                        WPARAM wParam, LPARAM lParam);
static int CreateClockWindow(HWND hwnd);
static void DestroyClockWindow(HCLOCKWINDOW window);
static void LayOutClockWindow(HCLOCKWINDOW window);

static void StartClock(HCLOCKWINDOW window);
static void StopClock(HCLOCKWINDOW window);
static void UpdateClock(HCLOCKWINDOW window);

/*
 * Process clock window messages.
 */
LRESULT CALLBACK
ClockWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HCLOCKWINDOW window =
        (uMsg == WM_CREATE) ? NULL :
        (HCLOCKWINDOW) GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (uMsg) {
        case WM_CREATE:
            return CreateClockWindow(hwnd);

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDCANCEL:
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;

        case WM_SIZE:
            LayOutClockWindow(window);
            return 0;

        case WM_TIMER:
            switch (wParam) {
                case IDT_REFRESH:
                    UpdateClock(window);
                    break;
            }
            return 0;

        case WM_SHOWWINDOW:
            if (wParam)
                StartClock(window);
            else
                StopClock(window);
            return 0;

        case WM_DESTROY:
            DestroyClockWindow(window);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

/*
 * Create the clock window.
 * Returns 0 on success, -1 on failure.
 */
int
CreateClockWindow(HWND hwnd)
{
    HCLOCKWINDOW window;

    window = malloc(sizeof(CLOCKWINDOW));
    if (window == NULL)
        return -1;
    memset(window, 0, sizeof(CLOCKWINDOW));

    window->hwnd = hwnd;
    SetWindowLongPtr(window->hwnd, GWLP_USERDATA, (LONG_PTR) window);

    window->hwndClock = CreateWindowEx(
        /* dwExStyle */     0,
        /* lpClassName */   TEXT("STATIC"),
        /* lpWindowName */  NULL,
        /* dwStyle */       WS_CHILD | WS_VISIBLE | SS_CENTER,
        /* pos, size */     0, 0, 0, 0,
        /* hwndParent */    window->hwnd,
        /* hMenu */         NULL,
        /* hInstance */     NULL,
        /* lpParam */       NULL
    );
    window->hwndUptimeLabel = CreateWindowEx(
        /* dwExStyle */     0,
        /* lpClassName */   TEXT("STATIC"),
        /* lpWindowName */  TEXT("System Uptime"),
        /* dwStyle */       WS_CHILD | WS_VISIBLE | SS_CENTER,
        /* pos, size */     0, 0, 0, 0,
        /* hwndParent */    window->hwnd,
        /* hMenu */         NULL,
        /* hInstance */     NULL,
        /* lpParam */       NULL
    );
    window->hwndUptime = CreateWindowEx(
        /* dwExStyle */     0,
        /* lpClassName */   TEXT("STATIC"),
        /* lpWindowName */  NULL,
        /* dwStyle */       WS_CHILD | WS_VISIBLE | SS_CENTER,
        /* pos, size */     0, 0, 0, 0,
        /* hwndParent */    window->hwnd,
        /* hMenu */         NULL,
        /* hInstance */     NULL,
        /* lpParam */       NULL
    );

    // Make sure all our widgets exist
    if ((window->hwndClock == NULL)
        || (window->hwndUptimeLabel == NULL)
        || (window->hwndUptime == NULL))
        return -1;

    // Lay out widgets
    LayOutClockWindow(window);
    return 0;
}

/*
 * Destroy the clock window.
 */
void
DestroyClockWindow(HCLOCKWINDOW window)
{
    if (window == NULL)
        return;

    StopClock(window);

    if (window->hFontClock != NULL)
        DeleteObject(window->hFontClock);
    if (window->hFontUptime != NULL)
        DeleteObject(window->hFontUptime);
    free(window);
}

/*
 * Lay out the clock display.
 * Called when the clock window is created or resized.
 */
void
LayOutClockWindow(HCLOCKWINDOW window)
{
    int cHeightClock, cHeightUptime, cHeightDisplay;
    RECT rect;
    HFONT hOldFont;

    GetClientRect(window->hwnd, &rect);

    // Scale the font size with the window height
    cHeightClock = rect.bottom / 8;
    cHeightUptime = rect.bottom / 12;

    // Center the clock display vertically in the window
    cHeightDisplay = cHeightClock + 3 * cHeightUptime;
    rect.top += (rect.bottom - cHeightDisplay) / 2;

    // Use a larger font for the time and date, and add a blank line after it
    MoveWindow(window->hwndClock,
               rect.left, rect.top, rect.right, cHeightClock, FALSE);
    rect.top += cHeightClock + cHeightUptime;

    MoveWindow(window->hwndUptimeLabel,
               rect.left, rect.top, rect.right, cHeightUptime, FALSE);
    rect.top += cHeightUptime;

    MoveWindow(window->hwndUptime,
               rect.left, rect.top, rect.right, cHeightUptime, FALSE);

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
        /* pszFaceName */       TEXT("MS Shell Dlg")
    );

    SendMessage(window->hwndClock,
                WM_SETFONT, (WPARAM) window->hFontClock, (LPARAM) FALSE);

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
        /* pszFaceName */       TEXT("MS Shell Dlg")
    );

    SendMessage(window->hwndUptimeLabel,
                WM_SETFONT, (WPARAM) window->hFontUptime, (LPARAM) FALSE);
    SendMessage(window->hwndUptime,
                WM_SETFONT, (WPARAM) window->hFontUptime, (LPARAM) FALSE);

    if (hOldFont != NULL)
        DeleteObject(hOldFont);

    // Force redrawing the entire window area
    GetClientRect(window->hwnd, &rect);
    RedrawWindow(window->hwnd, &rect, NULL, RDW_ERASE | RDW_INVALIDATE);
}

/*
 * Start the clock.
 * Called when the clock window is about to be shown.
 */
void
StartClock(HCLOCKWINDOW window)
{
    SYSTEMTIME lt;

    // Synchronize the display within 10ms
    do {
        GetLocalTime(&lt);
        Sleep(2);
    } while (lt.wMilliseconds % 1000 > 10);

    // Display the clock and set a timer to keep it updated
    UpdateClock(window);
    SetTimer(window->hwnd, IDT_REFRESH, 1000, (TIMERPROC) NULL);
}

/*
 * Stop the clock.
 * Called when the clock window is about to be hidden or destroyed.
 */
void
StopClock(HCLOCKWINDOW window)
{
    if (window->hwnd != NULL)
        KillTimer(window->hwnd, IDT_REFRESH);
}

/*
 * Update the clock display.
 */
void
UpdateClock(HCLOCKWINDOW window)
{
#define BUF_LEN 29      // Large enough to hold either CLOCK_FMT or UPTIME_FMT
                        // plus a trailing '\0'
    static TCHAR buf[BUF_LEN];
    time_t now;
    struct tm *timeinfo;
    unsigned long long ticks, days, hours, minutes, seconds;

    // Update the clock display
    // Don't free timeinfo -- it's a pointer to static memory
    time(&now);
    timeinfo = localtime(&now);

    if (STRFTIME(buf, BUF_LEN, CLOCK_FMT, timeinfo) != 0)
        SetWindowText(window->hwndClock, buf);

    // Now do the uptime display
    // GetTickCount64() returns the system uptime in milliseconds
    ticks = GetTickCount64();
    days = ticks / MSEC_PER_DAY;
    ticks %= MSEC_PER_DAY;
    hours = ticks / MSEC_PER_HR;
    ticks %= MSEC_PER_HR;
    minutes = ticks / MSEC_PER_MIN;
    ticks %= MSEC_PER_MIN;
    seconds = ticks / MSEC_PER_SEC;

    if (SNPRINTF(buf, BUF_LEN,
                 UPTIME_FMT, days, hours, minutes, seconds) != 0)
        SetWindowText(window->hwndUptime, buf);
#undef BUF_LEN
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
        LPSTR lpCmdLine, int nCmdShow)
{
    HACCEL hAccTable;
    WNDCLASS wc = { };
    MSG msg = { };
    HWND hwndClockWindow;

    // Create the accelerator table
    hAccTable = CreateAcceleratorTable(accel, cAccel);

    // Register the Uptime Clock window class
    wc.lpfnWndProc = ClockWindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    // Create the clock window
    hwndClockWindow = CreateWindowEx(
        /* dwExStyle */     0,
        /* lpClassName */   CLASS_NAME,
        /* lpWindowName */  TEXT("Uptime Clock"),
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
        if (!TranslateAccelerator(hwndClockWindow, hAccTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Allow screen blanking and sleep timeouts
    SetThreadExecutionState(ES_CONTINUOUS);

    // Clean up and exit
    DestroyAcceleratorTable(hAccTable);
    return 0;
}
