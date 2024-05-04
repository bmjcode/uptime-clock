/*
 * Clock with system uptime display.
 * Copyright (c) 2023, 2024 Benjamin Johnson <bmjcode@gmail.com>
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

#define WINVER 0x400        // Windows 95
#define _WIN32_WINNT 0x501  // Windows XP (for EXECUTION_STATE)
#include <windows.h>

#ifdef UNICODE
#  include <wchar.h>
#  define SNPRINTF swprintf
#  define STRFTIME wcsftime
#  define STRLEN   wcslen
#else
#  include <stdio.h>
#  define SNPRINTF snprintf
#  define STRFTIME strftime
#  define STRLEN   strlen
#endif
#include <time.h>

// Window class name
#define CLASS_NAME TEXT("Uptime Clock")

// Clock format: 03/30/2023 12:34:56 AM (22 chars)
#define CLOCK_FMT TEXT("%m/%d/%Y %I:%M:%S %p")
#define CLOCK_LEN 22

// Uptime format: 365 d, 23 hr, 59 min, 59 sec (28 chars)
// We're unlikely to see more than a three-digit day count. If Windows has
// really been running that long without rebooting, we've got other problems.
#define UPTIME_FMT TEXT("%lld d, %lld hr, %lld min, %lld sec")
#define UPTIME_LEN 28

// Label for the uptime display
#define UPTIME_LABEL TEXT("System Uptime")
#define UPTIME_LABEL_LEN 13

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
    TCHAR szClock[CLOCK_LEN + 1];
    TCHAR szUptime[UPTIME_LEN + 1];
} CLOCKWINDOW, *HCLOCKWINDOW;

static LRESULT CALLBACK ClockWindowProc(HWND hwnd, UINT uMsg,
                                        WPARAM wParam, LPARAM lParam);
static int CreateClockWindow(HWND hwnd);
static void DestroyClockWindow(HCLOCKWINDOW window);
static void PaintClockWindow(HCLOCKWINDOW window);

static void StartClock(HCLOCKWINDOW window);
static void StopClock(HCLOCKWINDOW window);
static void RedrawClock(HCLOCKWINDOW window);
static void UpdateClock(HCLOCKWINDOW window);

/*
 * GetTickCount64() (available on Windows Vista and newer) is preferred
 * because GetTickCount() overflows around 49.7 days, but we will fall back
 * for compatiblity with older Windows versions. Plenty of legacy systems
 * still run these obsolete OSes, and someone may find this tool useful for
 * troubleshooting such a system.
 */
typedef unsigned long long (__cdecl *PROC_GTC64)(void);
PROC_GTC64 pGetTickCount64;
#define GetTickCount64OrOtherwise() \
    ((pGetTickCount64 == NULL) ? GetTickCount() : pGetTickCount64())

/*
 * SetThreadExecutionState() (available on Windows XP and newer) is
 * also nice to have, but we can function without it.
 */
typedef EXECUTION_STATE (__cdecl *PROC_STES)(EXECUTION_STATE);
PROC_STES pSetThreadExecutionState;

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

        case WM_PAINT:
            PaintClockWindow(window);
            return 0;

        case WM_SIZE:
            RedrawClock(window);
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
    free(window);
}

/*
 * Paint the clock window.
 * This is the handler for the WM_PAINT message; if you need to retrigger
 * painting manually, use RedrawClock().
 *
 * We draw text directly on the window rather than use static controls
 * to prevent flicker caused by SetWindowText() erasing and redrawing the
 * latter. (This is especially noticeable on larger screens.) This is a
 * textbook application of double-buffering: We make all our changes in a
 * second, offscreen buffer, then blit them back all at once to display.
 */
void
PaintClockWindow(HCLOCKWINDOW window)
{
    RECT rect;
    PAINTSTRUCT ps;
    HDC hdc, memDC;
    HBITMAP memBM;
    HFONT hFontClock, hFontUptime;
    int cHeightClock, cHeightUptime;
    long x, y, displayHeight;

    // Initialize handles to NULL for safety
    hdc = NULL;
    memDC = NULL;
    memBM = NULL;
    hFontClock = NULL;
    hFontUptime = NULL;

    // Get the window area
    // Bottom and right coordinates are our height and width, respectively
    GetClientRect(window->hwnd, &rect);

    // Get our window's device context
    hdc = BeginPaint(window->hwnd, &ps);

    // Create a compatible memory context to work in
    memDC = CreateCompatibleDC(hdc);
    if (memDC == NULL)
        goto cleanup;

    // Create a bitmap to hold the display content
    memBM = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
    if (memBM == NULL)
        goto cleanup;
    SelectObject(memDC, memBM);

    // Scale the font size with the window height
    cHeightClock = rect.bottom / 8;
    cHeightUptime = rect.bottom / 12;
    hFontClock = CreateFont(
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
    hFontUptime = CreateFont(
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
    if ((hFontClock == NULL) || (hFontUptime == NULL))
        goto cleanup;

    // Center the display in the window
    displayHeight = cHeightClock + 3 * cHeightUptime;
    x = rect.right / 2;
    y = (rect.bottom - displayHeight) / 2;

    // Fill the window with the background color
    FillRect(memDC, &rect, GetSysColorBrush(COLOR_BTNFACE));

    // Set text alignment and colors
    SetTextAlign(memDC, TA_TOP | TA_CENTER | TA_NOUPDATECP);
    SetTextColor(memDC, GetSysColor(COLOR_BTNTEXT));
    SetBkColor(memDC, GetSysColor(COLOR_BTNFACE));
    SetBkMode(memDC, TRANSPARENT);

    // Display the date and time
    SelectObject(memDC, hFontClock);
    TextOut(memDC, x, y, window->szClock, STRLEN(window->szClock));
    y += cHeightClock + cHeightUptime;

    // Display the system uptime
    SelectObject(memDC, hFontUptime);
    TextOut(memDC, x, y, UPTIME_LABEL, UPTIME_LABEL_LEN);
    y += cHeightUptime;
    TextOut(memDC, x, y, window->szUptime, STRLEN(window->szUptime));

    // Blit our changes back into the window's device context
    BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);

cleanup:
    DeleteDC(memDC);
    DeleteObject(hFontUptime);
    DeleteObject(hFontClock);
    DeleteObject(memBM);
    EndPaint(window->hwnd, &ps);
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
 * Redraw the clock window without updating the time.
 */
void
RedrawClock(HCLOCKWINDOW window)
{
    RECT rect;

    // Force repainting the window
    GetClientRect(window->hwnd, &rect);
    RedrawWindow(window->hwnd, &rect, NULL, RDW_INVALIDATE);
}

/*
 * Update the clock display.
 */
void
UpdateClock(HCLOCKWINDOW window)
{
    time_t now;
    struct tm *timeinfo;
    unsigned long long ticks, days, hours, minutes, seconds;

    // Update the date and time
    // Don't free timeinfo -- it's a pointer to static memory
    time(&now);
    timeinfo = localtime(&now);

    memset(window->szClock, 0, (CLOCK_LEN + 1) * sizeof(TCHAR));
    if (STRFTIME(window->szClock, CLOCK_LEN + 1, CLOCK_FMT, timeinfo) == 0)
        return;

    // Now do the uptime display
    ticks = GetTickCount64OrOtherwise();
    days = ticks / MSEC_PER_DAY;
    ticks %= MSEC_PER_DAY;
    hours = ticks / MSEC_PER_HR;
    ticks %= MSEC_PER_HR;
    minutes = ticks / MSEC_PER_MIN;
    ticks %= MSEC_PER_MIN;
    seconds = ticks / MSEC_PER_SEC;

    memset(window->szUptime, 0, (UPTIME_LEN + 1) * sizeof(TCHAR));
    if (SNPRINTF(window->szUptime, UPTIME_LEN + 1,
                 UPTIME_FMT, days, hours, minutes, seconds) == 0)
        return;

    RedrawClock(window);
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
        LPSTR lpCmdLine, int nCmdShow)
{
    HINSTANCE hinstKernel32;
    HACCEL hAccTable;
    WNDCLASS wc = { };
    MSG msg = { };
    HWND hwndClockWindow;

    // Dynamically load functions added in newer Windows versions
    hinstKernel32 = LoadLibrary(TEXT("kernel32.dll"));
    if (hinstKernel32 == NULL) {
        pGetTickCount64 = NULL;
        pSetThreadExecutionState = NULL;
    } else {
        pGetTickCount64 = (PROC_GTC64)
            GetProcAddress(hinstKernel32, "GetTickCount64");
        pSetThreadExecutionState = (PROC_STES)
            GetProcAddress(hinstKernel32, "SetThreadExecutionState");
    }

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
    if (pSetThreadExecutionState != NULL)
        pSetThreadExecutionState(ES_DISPLAY_REQUIRED
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
    if (pSetThreadExecutionState != NULL)
        pSetThreadExecutionState(ES_CONTINUOUS);

    // Clean up and exit
    DestroyAcceleratorTable(hAccTable);
    if (hinstKernel32 != NULL)
        FreeLibrary(hinstKernel32);
    return 0;
}
