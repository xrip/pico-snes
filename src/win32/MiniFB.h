#ifndef MINI_FB_H
#define MINI_FB_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>

//////////////////////////////////////////////////////////////////
// ENUM FOR COLOR FORMAT
//////////////////////////////////////////////////////////////////
typedef enum {
    MFB_FORMAT_INDEXED8,
    MFB_FORMAT_RGB555,
    MFB_FORMAT_RGB565,
    MFB_FORMAT_RGB888
} mfb_color_format_t;

//////////////////////////////////////////////////////////////////
// INTERNAL STATE
//////////////////////////////////////////////////////////////////
static struct {
    HWND hwnd;
    HDC hdc;
    void *framebuffer;
    int width;
    int height;
    int scale;
    int should_close;
    unsigned char key_status[512];
    BITMAPINFO *bitmap_info;
    mfb_color_format_t color_format;
} mfb;

//////////////////////////////////////////////////////////////////
// USER CALLBACKS
//////////////////////////////////////////////////////////////////
extern void HandleInput(WPARAM wParam, BOOL isKeyDown);
extern void HandleMouse(int x, int y, int buttons);

//////////////////////////////////////////////////////////////////
// INTERNAL WINDOW PROCEDURE
//////////////////////////////////////////////////////////////////
static LRESULT CALLBACK mfb_window_proc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            if (mfb.framebuffer) {
                StretchDIBits(
                    mfb.hdc,
                    0, 0, mfb.width * mfb.scale, mfb.height * mfb.scale,
                    0, 0, mfb.width, mfb.height,
                    mfb.framebuffer,
                    mfb.bitmap_info, DIB_RGB_COLORS, SRCCOPY
                );
                ValidateRect(hwnd, NULL);
            }
            break;

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
            const unsigned char down = !(lParam >> 31 & 1);
            HandleInput(wParam, down);
            mfb.key_status[wParam] = down;
            if ((wParam & 0xFF) == VK_ESCAPE) mfb.should_close = 1;
            break;
        }

        case WM_CLOSE:
            mfb.should_close = 1;
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

//////////////////////////////////////////////////////////////////
// OPEN WINDOW
//////////////////////////////////////////////////////////////////
static int mfb_open(const char *title, const int width, const int height, const int scale, const mfb_color_format_t format) {
    mfb.width = width;
    mfb.height = height;
    mfb.scale = scale;
    mfb.color_format = format;
    mfb.should_close = 0;
    mfb.framebuffer = NULL;

    WNDCLASS wc = {0};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = mfb_window_proc;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = title;
    RegisterClass(&wc);

    RECT rect = {0, 0, width * scale, height * scale};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, 0);

    mfb.hwnd = CreateWindowEx(
        0, title, title,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        -1980  + (GetSystemMetrics(SM_CXSCREEN) - (rect.right - rect.left)) / 2,
        +200  + (GetSystemMetrics(SM_CYSCREEN) - (rect.bottom - rect.top)) / 2,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, NULL, NULL
    );

    if (!mfb.hwnd) return 0;

    ShowWindow(mfb.hwnd, SW_NORMAL);
    mfb.hdc = GetDC(mfb.hwnd);

    // Setup BITMAPINFO
    mfb.bitmap_info = (BITMAPINFO *)malloc(256 * sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD));
    mfb.bitmap_info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    mfb.bitmap_info->bmiHeader.biWidth = width;
    mfb.bitmap_info->bmiHeader.biHeight = -height; // top-down
    mfb.bitmap_info->bmiHeader.biPlanes = 1;

    switch (format) {
        case MFB_FORMAT_INDEXED8:
            mfb.bitmap_info->bmiHeader.biBitCount = 8;
            mfb.bitmap_info->bmiHeader.biClrUsed = 256;
            mfb.bitmap_info->bmiHeader.biCompression = BI_RGB;
            RGBQUAD* palette = &mfb.bitmap_info->bmiColors[0];
            for (int i = 0; i < 256; ++i)
            {
            RGBQUAD rgb = {0};
            rgb.rgbRed =  ~i;
            rgb.rgbGreen =  ~i;
            rgb.rgbBlue =  ~i;
            palette[i] = rgb;
            }

            break;
        case MFB_FORMAT_RGB555:
            mfb.bitmap_info->bmiHeader.biBitCount = 16;
            mfb.bitmap_info->bmiHeader.biCompression = BI_BITFIELDS;
            ((DWORD *)mfb.bitmap_info->bmiColors)[0] = 0x7C00;
            ((DWORD *)mfb.bitmap_info->bmiColors)[1] = 0x03E0;
            ((DWORD *)mfb.bitmap_info->bmiColors)[2] = 0x001F;
            break;
        case MFB_FORMAT_RGB565:
            mfb.bitmap_info->bmiHeader.biBitCount = 16;
            mfb.bitmap_info->bmiHeader.biCompression = BI_BITFIELDS;
            ((DWORD *)mfb.bitmap_info->bmiColors)[0] = 0xF800;
            ((DWORD *)mfb.bitmap_info->bmiColors)[1] = 0x07E0;
            ((DWORD *)mfb.bitmap_info->bmiColors)[2] = 0x001F;
            break;
        case MFB_FORMAT_RGB888:
            mfb.bitmap_info->bmiHeader.biBitCount = 24;
            mfb.bitmap_info->bmiHeader.biCompression = BI_RGB;
            break;
    }

    return 1;
}

//////////////////////////////////////////////////////////////////
// PALETTE
//////////////////////////////////////////////////////////////////
static inline void mfb_set_palette(const uint8_t index, const uint32_t color) {
    if (mfb.color_format != MFB_FORMAT_INDEXED8) return;
    ((DWORD *)mfb.bitmap_info->bmiColors)[index] = color;
}

static void mfb_set_palette_array(const uint32_t *palette, const uint8_t start, const uint8_t count) {
    for (int i = 0; i < count; i++) mfb_set_palette(start + i, palette[i]);
}

//////////////////////////////////////////////////////////////////
// UPDATE WINDOW WITH HIGH-PRECISION FPS
//////////////////////////////////////////////////////////////////
static int mfb_update(void *framebuffer, const int fpsLimit) {
    static LARGE_INTEGER prevTime = {0};
    static LARGE_INTEGER freq = {0};
    MSG msg;

    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    if (!prevTime.QuadPart) QueryPerformanceCounter(&prevTime);

    mfb.framebuffer = framebuffer;

    InvalidateRect(mfb.hwnd, NULL, TRUE);
    SendMessage(mfb.hwnd, WM_PAINT, 0, 0);

    while (PeekMessage(&msg, mfb.hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (mfb.should_close) return 0;

    if (fpsLimit > 0) {
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);

        double elapsed = (double)(currentTime.QuadPart - prevTime.QuadPart) / (double)freq.QuadPart;
        const double target = 1.0 / fpsLimit;

        const double remaining = target - elapsed;
        if (remaining > 0) {
            if (remaining > 0.002) { // sleep if more than ~2ms left
                Sleep((DWORD)((remaining - 0.001) * 1000)); // leave 1ms for spin
            }

            // spin-wait for high precision
            do {
                QueryPerformanceCounter(&currentTime);
                elapsed = (double)(currentTime.QuadPart - prevTime.QuadPart) / (double)freq.QuadPart;
            } while (elapsed < target);
        }

        prevTime = currentTime;
    }

    return 1;
}

//////////////////////////////////////////////////////////////////
// CLOSE WINDOW
//////////////////////////////////////////////////////////////////
static void mfb_close() {
    if (mfb.bitmap_info) free(mfb.bitmap_info);
    if (mfb.hwnd && mfb.hdc) {
        ReleaseDC(mfb.hwnd, mfb.hdc);
        DestroyWindow(mfb.hwnd);
    }

    mfb.hwnd = NULL;
    mfb.hdc = NULL;
    mfb.framebuffer = NULL;
}

//////////////////////////////////////////////////////////////////
// KEY STATUS
//////////////////////////////////////////////////////////////////
static unsigned char *mfb_keystatus() {
    return mfb.key_status;
}

#endif // !PICO_ON_DEVICE
