#include "gui_forms/drawing_c_api.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "drawing_windows_probe:%d: %s\n", __LINE__, #expression); \
        return 1; \
    } \
} while (0)

int main(void) {
    gd_api_v0 api;
    gd_handle source = {0};
    gd_handle imported = {0};
    gd_handle captured = {0};
    gd_handle presented = {0};
    gd_handle rejected = {0};
    gd_color pixel = {0};
    uintptr_t hbitmap = 0;
    BITMAP details;
    gd_rect surface_bounds = {0};
    HDC memory = NULL;
    HGDIOBJ previous = NULL;
    HWND window = NULL;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
    uint64_t generation = 0;
    uintptr_t leased_hdc = 0;
    uint64_t lease_token = 0;

    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_1, &api) == GD_OK);
    CHECK(api.bitmap_export_hbitmap != NULL && api.bitmap_import_hbitmap != NULL);
    CHECK(api.bitmap_create(3, 2, GD_PIXEL_BGRA32_PREMULTIPLIED, &source) == GD_OK);
    CHECK(api.bitmap_set_pixel(source, 0, 0,
                               (gd_color){UINT32_C(0xffff0000), 0}) == GD_OK);
    CHECK(api.bitmap_set_pixel(source, 1, 0,
                               (gd_color){UINT32_C(0x80008000), 0}) == GD_OK);
    CHECK(api.bitmap_export_hbitmap(
              source, (gd_color){UINT32_C(0xff0000ff), 0}, &hbitmap) == GD_OK);
    CHECK(hbitmap != 0);
    memset(&details, 0, sizeof(details));
    CHECK(GetObjectW((HBITMAP)hbitmap, sizeof(details), &details) == sizeof(details));
    CHECK(details.bmWidth == 3 && details.bmHeight == 2 && details.bmBitsPixel == 32);

    CHECK(api.bitmap_import_hbitmap(hbitmap, &imported) == GD_OK);
    CHECK(api.bitmap_dimensions(imported, &width, &height, &format, &generation) == GD_OK);
    CHECK(width == 3 && height == 2 && format == GD_PIXEL_BGRA32_PREMULTIPLIED);
    CHECK(api.bitmap_get_pixel(imported, 0, 0, &pixel) == GD_OK);
    CHECK(pixel.argb == UINT32_C(0xffff0000));
    CHECK(api.bitmap_acquire_hdc(imported, &leased_hdc, &lease_token) == GD_OK);
    CHECK(leased_hdc != 0 && lease_token != 0);
    CHECK(api.dispose(imported) == GD_ERROR_INVALID_ARGUMENT);
    CHECK(SetPixel((HDC)leased_hdc, 2, 1, RGB(0, 0, 255)) != CLR_INVALID);
    CHECK(api.bitmap_release_hdc(imported, lease_token) == GD_OK);
    CHECK(api.bitmap_release_hdc(imported, lease_token) == GD_ERROR_INVALID_ARGUMENT);
    CHECK(api.bitmap_get_pixel(imported, 2, 1, &pixel) == GD_OK);
    CHECK((pixel.argb & UINT32_C(0x00ffffff)) == UINT32_C(0x000000ff));

    /* Alpha through a lease is explicit, not a colour key: black GDI writes
     * onto a transparent bitmap become opaque black, and untouched
     * transparent pixels stay transparent. */
    {
        gd_handle clear = {0};
        uintptr_t clear_hdc = 0;
        uint64_t clear_token = 0;
        CHECK(api.bitmap_create(3, 1, GD_PIXEL_BGRA32_PREMULTIPLIED, &clear) == GD_OK);
        CHECK(api.bitmap_acquire_hdc(clear, &clear_hdc, &clear_token) == GD_OK);
        CHECK(SetPixel((HDC)clear_hdc, 0, 0, RGB(0, 0, 0)) != CLR_INVALID);
        CHECK(SetPixel((HDC)clear_hdc, 2, 0, RGB(0, 128, 0)) != CLR_INVALID);
        CHECK(api.bitmap_release_hdc(clear, clear_token) == GD_OK);
        CHECK(api.bitmap_get_pixel(clear, 0, 0, &pixel) == GD_OK);
        CHECK(pixel.argb == UINT32_C(0xff000000));
        CHECK(api.bitmap_get_pixel(clear, 1, 0, &pixel) == GD_OK);
        CHECK((pixel.argb >> 24) == 0U);
        CHECK(api.bitmap_get_pixel(clear, 2, 0, &pixel) == GD_OK);
        CHECK(pixel.argb == UINT32_C(0xff008000));
        CHECK(api.dispose(clear) == GD_OK);
        puts("drawing-windows-lease-alpha=pass");
    }

    /* Bitmaps are BGRA8 only: the value RGBA had (1) is outside the enum. */
    CHECK(api.bitmap_create(2, 1, UINT32_C(1), &rejected) == GD_ERROR_INVALID_ARGUMENT);
    puts("drawing-windows-bgra-only=pass");

    memory = CreateCompatibleDC(NULL);
    CHECK(memory != NULL);
    previous = SelectObject(memory, (HBITMAP)hbitmap);
    CHECK(previous != NULL && previous != HGDI_ERROR);
    CHECK(api.native_surface_capture((uintptr_t)memory, GD_NATIVE_SURFACE_HDC,
                                     &captured, &surface_bounds) == GD_OK);
    CHECK(surface_bounds.width == 3 && surface_bounds.height == 2);
    CHECK(api.bitmap_set_pixel(captured, 2, 1,
                               (gd_color){UINT32_C(0xff0000ff), 0}) == GD_OK);
    CHECK(api.native_surface_present((uintptr_t)memory, GD_NATIVE_SURFACE_HDC,
                                     captured) == GD_OK);
    CHECK(api.bitmap_import_hbitmap(hbitmap, &presented) == GD_OK);
    CHECK(api.bitmap_get_pixel(presented, 2, 1, &pixel) == GD_OK);
    CHECK(pixel.argb == UINT32_C(0xff0000ff));

    window = CreateWindowExW(0, L"STATIC", L"GUI.Drawing D8",
                             WS_POPUP, 0, 0, 3, 2,
                             NULL, NULL, GetModuleHandleW(NULL), NULL);
    CHECK(window != NULL);
    CHECK(api.release(captured) == GD_OK);
    captured = (gd_handle){0};
    surface_bounds = (gd_rect){0};
    CHECK(api.native_surface_capture((uintptr_t)window, GD_NATIVE_SURFACE_HWND,
                                     &captured, &surface_bounds) == GD_OK);
    CHECK(surface_bounds.width == 3 && surface_bounds.height == 2);
    CHECK(api.native_surface_present((uintptr_t)window, GD_NATIVE_SURFACE_HWND,
                                     captured) == GD_OK);
    CHECK(DestroyWindow(window) != 0);

    CHECK(SelectObject(memory, previous) == (HGDIOBJ)hbitmap);
    CHECK(DeleteDC(memory) != 0);

    CHECK(DeleteObject((HBITMAP)hbitmap) != 0);
    CHECK(api.release(presented) == GD_OK);
    CHECK(api.release(captured) == GD_OK);
    CHECK(api.release(imported) == GD_OK);
    CHECK(api.release(source) == GD_OK);
    puts("drawing-windows-hbitmap=pass");
    return 0;
}
