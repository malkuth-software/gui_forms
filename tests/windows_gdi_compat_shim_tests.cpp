#include "gui_forms/c/gf_abi.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {

// The shim exports user32's and gdi32's own names and signatures, so each
// entry-point type comes from that function's declaration (house style
// section 2).
using ShimGetDC = decltype(&::GetDC);
using ShimReleaseDC = decltype(&::ReleaseDC);
using ShimBitBlt = decltype(&::BitBlt);
using ShimCreateCompatibleBitmap = decltype(&::CreateCompatibleBitmap);
using ShimCreateCompatibleDC = decltype(&::CreateCompatibleDC);
using ShimScrollWindowEx = decltype(&::ScrollWindowEx);
using ShimSendMessageW = decltype(&::SendMessageW);
using ShimSelectObject = decltype(&::SelectObject);
using ShimDeleteObject = decltype(&::DeleteObject);
using ShimDeleteDC = decltype(&::DeleteDC);

// The C ABI tables the test drives: objects (dispose and release), custom
// controls (an endpoint binds to one) and the Windows paint endpoint.
struct AbiTables final {
    gf_objects_table objects{};
    gf_custom_controls_table custom_controls{};
    gf_windows_paint_endpoint_table endpoints{};
};

bool negotiate_tables(AbiTables* tables) {
    (*tables).objects.struct_size = sizeof((*tables).objects);
    (*tables).custom_controls.struct_size = sizeof((*tables).custom_controls);
    (*tables).endpoints.struct_size = sizeof((*tables).endpoints);
    if (gf_abi_get_table(GF_TABLE_OBJECTS, GF_ABI_MAJOR, GF_ABI_MINOR, &(*tables).objects,
                         sizeof((*tables).objects)) != GF_STATUS_OK) return false;
    if (gf_abi_get_table(GF_TABLE_CUSTOM_CONTROLS, GF_ABI_MAJOR, GF_ABI_MINOR, &(*tables).custom_controls,
                         sizeof((*tables).custom_controls)) != GF_STATUS_OK) return false;
    return gf_abi_get_table(GF_TABLE_WINDOWS_PAINT_ENDPOINT, GF_ABI_MAJOR, GF_ABI_MINOR, &(*tables).endpoints,
                            sizeof((*tables).endpoints)) == GF_STATUS_OK;
}

// The endpoint's control paints nothing of its own (the endpoint's frames
// replace its paint callback) and declares no properties.
void paint_nothing(void* context, gf_object control, const gf_paint_target* target) {
    static_cast<void>(context);
    static_cast<void>(control);
    static_cast<void>(target);
}

gf_status get_no_property(void* context, gf_object control, std::uint64_t index, gf_value* value) {
    static_cast<void>(context);
    static_cast<void>(control);
    static_cast<void>(index);
    static_cast<void>(value);
    return GF_STATUS_NOT_FOUND;
}

gf_status create_custom_control(const AbiTables& tables, const char* stable_id, gf_object* control) {
    gf_custom_control_spec spec{};
    spec.struct_size = sizeof(spec);
    spec.base = GF_CUSTOM_BASE_USER_CONTROL;
    spec.property_size = sizeof(gf_custom_property);
    spec.callbacks.struct_size = sizeof(spec.callbacks);
    spec.callbacks.paint = &paint_nothing;
    spec.callbacks.get_property = &get_no_property;
    const gf_utf8_view id{stable_id, static_cast<std::uint64_t>(std::strlen(stable_id))};
    return tables.custom_controls.create(&spec, id, control);
}

void dispose_control(const AbiTables& tables, gf_object control) {
    static_cast<void>(tables.objects.dispose(control));
    static_cast<void>(tables.objects.release(control));
}

// A compatibility handle is a virtual window handle the shim understands,
// widened to 64 bits by the ABI; device contexts are widened the same way.
HWND as_window(std::uint64_t handle) {
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(handle));
}

template <typename Function>
Function entry(HMODULE module, const char* name) {
    const FARPROC raw = GetProcAddress(module, name);
    Function result{};
    static_assert(sizeof(result) == sizeof(raw));
    std::memcpy(&result, &raw, sizeof(result));
    return result;
}

void run_resize_writer(std::atomic<bool>* start,
                       std::atomic<bool>* writer_ok,
                       ShimBitBlt bit_blt,
                       HDC endpoint_dc,
                       HDC producer) {
    while (!(*start).load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    for (int frame = 0; frame < 4000; ++frame) {
        if (bit_blt(endpoint_dc, 0, 0, 24, 48, producer,
                    frame % 100, frame % 100, SRCCOPY) == FALSE) {
            (*writer_ok).store(false, std::memory_order_release);
            return;
        }
    }
}

void run_release_writer(const gf_windows_paint_endpoint_table* endpoints,
                        std::uint64_t endpoint_dc,
                        std::atomic<bool>* lease_acquired,
                        std::atomic<bool>* allow_end,
                        std::atomic<bool>* writer_ok) {
    std::uint64_t write_lease{};
    if ((*endpoints).begin_write(endpoint_dc, &write_lease) != GF_STATUS_OK) {
        (*lease_acquired).store(true, std::memory_order_release);
        return;
    }
    (*lease_acquired).store(true, std::memory_order_release);
    while (!(*allow_end).load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    (*writer_ok).store(
        (*endpoints).end_write(write_lease, 0U) == GF_STATUS_OK,
        std::memory_order_release);
}

void run_endpoint_releaser(const gf_windows_paint_endpoint_table* endpoints,
                           std::uint64_t endpoint,
                           std::atomic<bool>* lease_acquired,
                           std::atomic<bool>* release_started,
                           std::atomic<bool>* release_ok) {
    while (!(*lease_acquired).load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    (*release_started).store(true, std::memory_order_release);
    (*release_ok).store(
        (*endpoints).release(endpoint) == GF_STATUS_OK,
        std::memory_order_release);
}

bool run_lifetime(const AbiTables& api, HMODULE shim, int lifetime,
                  int width, int height, int resized_width,
                  int resized_height) {
    const ShimGetDC get_dc = entry<ShimGetDC>(shim, "GetDC");
    const ShimReleaseDC release_dc =
        entry<ShimReleaseDC>(shim, "ReleaseDC");
    const ShimBitBlt bit_blt = entry<ShimBitBlt>(shim, "BitBlt");
    const ShimCreateCompatibleBitmap create_bitmap =
        entry<ShimCreateCompatibleBitmap>(
        shim, "CreateCompatibleBitmap");
    const ShimCreateCompatibleDC create_dc = entry<ShimCreateCompatibleDC>(
        shim, "CreateCompatibleDC");
    const ShimSelectObject select_object =
        entry<ShimSelectObject>(shim, "SelectObject");
    const ShimDeleteObject delete_object =
        entry<ShimDeleteObject>(shim, "DeleteObject");
    const ShimDeleteDC delete_dc = entry<ShimDeleteDC>(shim, "DeleteDC");
    const ShimScrollWindowEx scroll_window =
        entry<ShimScrollWindowEx>(shim, "ScrollWindowEx");
    const ShimSendMessageW send_message =
        entry<ShimSendMessageW>(shim, "SendMessageW");
    if (!get_dc || !release_dc || !bit_blt || !create_bitmap || !create_dc ||
        !select_object || !delete_object || !delete_dc || !scroll_window ||
        !send_message) {
        std::fprintf(stderr, "shim-gate=failure|stage:exports\n");
        return false;
    }

    gf_object control{};
    std::uint64_t endpoint{};
    std::uint64_t window_value{};
    HDC producer{};
    HBITMAP producer_bitmap{};
    HGDIOBJ producer_previous{};
    HDC endpoint_dc{};
    bool ok = false;

    if (create_custom_control(api, "shim.endpoint.test", &control) != GF_STATUS_OK) {
        std::fprintf(stderr,
                     "shim-gate=failure|lifetime:%d|stage:control-create\n",
                     lifetime);
        goto cleanup;
    }
    if (api.endpoints.acquire(
            control, static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height), &endpoint,
            &window_value) != GF_STATUS_OK || endpoint == 0U || window_value == 0U) {
        std::fprintf(stderr,
                     "shim-gate=failure|lifetime:%d|stage:endpoint-acquire\n",
                     lifetime);
        goto cleanup;
    }
    if (IsWindow(as_window(window_value)) != FALSE) {
        std::fprintf(stderr,
                     "shim-gate=failure|lifetime:%d|stage:allocated-hwnd\n",
                     lifetime);
        goto cleanup;
    }

    endpoint_dc = get_dc(as_window(window_value));
    if (endpoint_dc == nullptr) {
        std::fprintf(stderr,
                     "shim-gate=failure|lifetime:%d|stage:get-dc\n", lifetime);
        goto cleanup;
    }
    producer_bitmap = create_bitmap(endpoint_dc, width, height);
    producer = create_dc(endpoint_dc);
    if (producer_bitmap == nullptr || producer == nullptr) {
        std::fprintf(stderr,
                     "shim-gate=failure|lifetime:%d|stage:compatible-create\n",
                     lifetime);
        goto cleanup;
    }
    producer_previous = select_object(producer, producer_bitmap);
    if (producer_previous == nullptr || producer_previous == HGDI_ERROR) {
        std::fprintf(stderr,
                     "shim-gate=failure|lifetime:%d|stage:select\n", lifetime);
        goto cleanup;
    }

    for (int frame = 0; frame < 64; ++frame) {
        const COLORREF color = RGB((frame * 37 + lifetime) & 0xff,
                                   (frame * 67 + lifetime * 3) & 0xff,
                                   (frame * 97 + lifetime * 7) & 0xff);
        HBRUSH brush = CreateSolidBrush(color);
        RECT bounds{0, 0, width, height};
        if (brush == nullptr || FillRect(producer, &bounds, brush) == 0) {
            if (brush != nullptr) DeleteObject(brush);
            std::fprintf(stderr,
                         "shim-gate=failure|lifetime:%d|frame:%d|stage:fill\n",
                         lifetime, frame);
            goto cleanup;
        }
        DeleteObject(brush);
        if (bit_blt(endpoint_dc, 0, 0, width, height, producer, 0, 0,
                    SRCCOPY) == FALSE) {
            std::fprintf(stderr,
                         "shim-gate=failure|lifetime:%d|frame:%d|stage:bitblt\n",
                         lifetime, frame);
            goto cleanup;
        }
    }

    {
        std::array<char, 256> snapshot{};
        std::uint64_t required{};
        if (api.endpoints.snapshot(
                endpoint, snapshot.data(), snapshot.size(), &required) != GF_STATUS_OK) {
            std::fprintf(stderr,
                         "shim-gate=failure|lifetime:%d|stage:snapshot\n",
                         lifetime);
            goto cleanup;
        }
        const std::string state(snapshot.data(),
                                static_cast<std::size_t>(required));
        if (state.find("state:live") == std::string::npos ||
            state.find("content:64") == std::string::npos) {
            std::fprintf(stderr,
                         "shim-gate=failure|lifetime:%d|stage:publish|state:%s\n",
                         lifetime, state.c_str());
            goto cleanup;
        }
    }

    if (lifetime == 0) {
        const COLORREF marker = RGB(241, 17, 93);
        if (SetPixel(producer, 0, 0, marker) == CLR_INVALID ||
            bit_blt(endpoint_dc, 0, 0, width, height, producer, 0, 0,
                    SRCCOPY) == FALSE) {
            std::fprintf(stderr, "shim-gate=failure|stage:scroll-seed\n");
            goto cleanup;
        }
        RECT scroll_bounds{0, 0, width, height};
        RECT exposed{};
        const COLORREF producer_pixel = GetPixel(producer, 0, 0);
        const COLORREF before_scroll = GetPixel(endpoint_dc, 0, 0);
        if (scroll_window(as_window(window_value), 1, 0,
                          &scroll_bounds, &scroll_bounds, nullptr, &exposed,
                          SW_INVALIDATE) == ERROR ||
            GetPixel(endpoint_dc, 1, 0) != marker) {
            std::fprintf(stderr,
                         "shim-gate=failure|stage:virtual-scroll|before:%lu|"
                         "after:%lu|producer:%lu|marker:%lu\n",
                         static_cast<unsigned long>(before_scroll),
                         static_cast<unsigned long>(GetPixel(endpoint_dc, 1, 0)),
                         static_cast<unsigned long>(producer_pixel),
                         static_cast<unsigned long>(marker));
            DIBSECTION producer_section{};
            DIBSECTION endpoint_section{};
            const HGDIOBJ endpoint_bitmap =
                GetCurrentObject(endpoint_dc, OBJ_BITMAP);
            const int producer_object_size = GetObjectW(
                producer_bitmap, sizeof(producer_section), &producer_section);
            const int endpoint_object_size = GetObjectW(
                endpoint_bitmap, sizeof(endpoint_section), &endpoint_section);
            std::fprintf(stderr,
                         "shim-gate=diagnostic|producer-object:%d|bm-height:%ld|"
                         "bi-height:%ld|endpoint-object:%d|bm-height:%ld|"
                         "bi-height:%ld\n",
                         producer_object_size,
                         static_cast<long>(producer_section.dsBm.bmHeight),
                         static_cast<long>(producer_section.dsBmih.biHeight),
                         endpoint_object_size,
                         static_cast<long>(endpoint_section.dsBm.bmHeight),
                         static_cast<long>(endpoint_section.dsBmih.biHeight));
            goto cleanup;
        }
        if (send_message(as_window(window_value), 0x83f1U,
                         0, 0) != 0) {
            std::fprintf(stderr, "shim-gate=failure|stage:virtual-message\n");
            goto cleanup;
        }
    }

    if (api.endpoints.configure(
            endpoint, static_cast<std::uint32_t>(resized_width),
            static_cast<std::uint32_t>(resized_height)) != GF_STATUS_OK ||
        get_dc(as_window(window_value)) != endpoint_dc) {
        std::fprintf(stderr,
                     "shim-gate=failure|lifetime:%d|stage:resize-stability\n",
                     lifetime);
        goto cleanup;
    }
    ok = true;

cleanup:
    if (producer != nullptr && producer_previous != nullptr &&
        producer_previous != HGDI_ERROR) {
        select_object(producer, producer_previous);
    }
    if (producer_bitmap != nullptr) delete_object(producer_bitmap);
    if (producer != nullptr) delete_dc(producer);
    if (endpoint_dc != nullptr) {
        release_dc(as_window(window_value), endpoint_dc);
    }
    if (endpoint != 0U) api.endpoints.release(endpoint);
    if (control != 0U) dispose_control(api, control);
    return ok;
}

bool run_concurrent_resize_gate(const AbiTables& api, HMODULE shim) {
    const ShimGetDC get_dc = entry<ShimGetDC>(shim, "GetDC");
    const ShimReleaseDC release_dc =
        entry<ShimReleaseDC>(shim, "ReleaseDC");
    const ShimBitBlt bit_blt = entry<ShimBitBlt>(shim, "BitBlt");
    const ShimCreateCompatibleBitmap create_bitmap =
        entry<ShimCreateCompatibleBitmap>(
        shim, "CreateCompatibleBitmap");
    const ShimCreateCompatibleDC create_dc = entry<ShimCreateCompatibleDC>(
        shim, "CreateCompatibleDC");
    const ShimSelectObject select_object =
        entry<ShimSelectObject>(shim, "SelectObject");
    const ShimDeleteObject delete_object =
        entry<ShimDeleteObject>(shim, "DeleteObject");
    const ShimDeleteDC delete_dc = entry<ShimDeleteDC>(shim, "DeleteDC");
    if (!get_dc || !release_dc || !bit_blt || !create_bitmap || !create_dc ||
        !select_object || !delete_object || !delete_dc) return false;

    gf_object control{};
    std::uint64_t endpoint{};
    std::uint64_t window_value{};
    if (create_custom_control(api, "shim.race.test", &control) != GF_STATUS_OK ||
        api.endpoints.acquire(
            control, 332U, 324U, &endpoint, &window_value) != GF_STATUS_OK) {
        std::fprintf(stderr, "shim-gate=failure|stage:race-acquire\n");
        return false;
    }

    HDC endpoint_dc = get_dc(as_window(window_value));
    HDC producer = create_dc(endpoint_dc);
    HBITMAP producer_bitmap = create_bitmap(endpoint_dc, 332, 324);
    HGDIOBJ producer_previous = producer != nullptr && producer_bitmap != nullptr
        ? select_object(producer, producer_bitmap) : nullptr;
    if (endpoint_dc == nullptr || producer == nullptr ||
        producer_bitmap == nullptr || producer_previous == nullptr ||
        producer_previous == HGDI_ERROR) {
        std::fprintf(stderr, "shim-gate=failure|stage:race-backing\n");
        return false;
    }
    RECT producer_bounds{0, 0, 332, 324};
    HBRUSH brush = CreateSolidBrush(RGB(25, 80, 170));
    const bool filled = brush != nullptr &&
        FillRect(producer, &producer_bounds, brush) != 0;
    if (brush != nullptr) DeleteObject(brush);

    std::atomic<bool> start{};
    std::atomic<bool> writer_ok{filled};
    std::thread writer(&run_resize_writer, &start, &writer_ok, bit_blt,
                       endpoint_dc, producer);

    constexpr std::array<std::array<std::uint32_t, 2>, 6> sizes{{
        {{150U, 150U}}, {{90U, 150U}}, {{30U, 150U}},
        {{332U, 324U}}, {{303U, 324U}}, {{128U, 64U}},
    }};
    start.store(true, std::memory_order_release);
    bool configure_ok = true;
    for (int resize = 0; resize < 1200; ++resize) {
        const std::array<std::uint32_t, 2> size =
            sizes[static_cast<std::size_t>(resize) % sizes.size()];
        if (api.endpoints.configure(
                endpoint, size[0], size[1]) != GF_STATUS_OK) {
            configure_ok = false;
            break;
        }
    }
    writer.join();

    if (producer_previous != nullptr && producer_previous != HGDI_ERROR) {
        select_object(producer, producer_previous);
    }
    delete_object(producer_bitmap);
    delete_dc(producer);
    release_dc(as_window(window_value), endpoint_dc);
    api.endpoints.release(endpoint);
    dispose_control(api, control);
    if (!configure_ok || !writer_ok.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "shim-gate=failure|stage:race-operation\n");
        return false;
    }
    return true;
}

// GDI recycles DC handle values, and the shim keys its DC classification by
// value. A module that does not import the shim may hand it a DC (classified
// ordinary after the ABI declines it) and later destroy that DC with gdi32.
// Repeat that cycle across the handle table so the endpoint DC acquired next
// is likely to reuse a value the shim still records as ordinary. The shim
// must still give that endpoint DC a top-down compatible bitmap.
bool seed_recycled_dc_classifications(HMODULE shim) {
    const ShimBitBlt bit_blt = entry<ShimBitBlt>(shim, "BitBlt");
    if (bit_blt == nullptr) return false;
    constexpr int recycled_dc_count = 4096;
    for (int index = 0; index < recycled_dc_count; ++index) {
        HDC dc = CreateCompatibleDC(nullptr);
        if (dc == nullptr) return false;
        const BOOL copied = bit_blt(dc, 0, 0, 1, 1, dc, 0, 0, SRCCOPY);
        DeleteDC(dc);
        if (copied == FALSE) return false;
    }
    return true;
}

// The bitmap a dib-overlap case scrolls, and for the top-down cases the route
// by which the endpoint DC reaches the test.
enum class DibOverlapSource : std::uint8_t {
    // Operating-system bottom-up DIB section.
    bottom_up_section,
    // Endpoint DC from the shim's GetDC, the consumer route.
    top_down_shim_get_dc,
    // Endpoint DC from the internal ABI bridge, which the shim never sees
    // until CreateCompatibleBitmap receives it.
    top_down_abi_bridge,
};

// Scrolls a 32-bit DIB down one row onto itself through the shim's BitBlt.
// The bottom-up case uses an operating-system DIB section; the top-down cases
// use the shim's compatible bitmap for a GUI.Forms endpoint DC, which the
// shim creates as a tracked top-down DIB section. Windows reports a positive
// biHeight for both orientations, so the case verifies the orientation it
// fills before checking the overlapping copy.
bool run_dib_overlap_case(const AbiTables& api, HMODULE shim,
                          DibOverlapSource source) {
    const ShimGetDC get_dc = entry<ShimGetDC>(shim, "GetDC");
    const ShimReleaseDC release_dc =
        entry<ShimReleaseDC>(shim, "ReleaseDC");
    const ShimBitBlt bit_blt = entry<ShimBitBlt>(shim, "BitBlt");
    const ShimCreateCompatibleBitmap create_bitmap =
        entry<ShimCreateCompatibleBitmap>(
        shim, "CreateCompatibleBitmap");
    const ShimCreateCompatibleDC create_dc = entry<ShimCreateCompatibleDC>(
        shim, "CreateCompatibleDC");
    const ShimSelectObject select_object =
        entry<ShimSelectObject>(shim, "SelectObject");
    const ShimDeleteObject delete_object =
        entry<ShimDeleteObject>(shim, "DeleteObject");
    const ShimDeleteDC delete_dc = entry<ShimDeleteDC>(shim, "DeleteDC");
    if (get_dc == nullptr || release_dc == nullptr || bit_blt == nullptr ||
        create_bitmap == nullptr || create_dc == nullptr ||
        select_object == nullptr || delete_object == nullptr ||
        delete_dc == nullptr) {
        return false;
    }

    constexpr int width = 8;
    constexpr int height = 6;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const bool top_down = source != DibOverlapSource::bottom_up_section;
    const char* orientation = "bottom-up";
    // A bottom-up section keeps the name above.
    if (source == DibOverlapSource::top_down_shim_get_dc) {
        orientation = "top-down";
    } else if (source == DibOverlapSource::top_down_abi_bridge) {
        orientation = "top-down-abi-bridge";
    }
    gf_object endpoint_control{};
    std::uint64_t endpoint{};
    std::uint64_t endpoint_window{};
    HDC endpoint_dc{};
    void* storage{};
    HBITMAP bitmap{};
    HDC dc{};
    HGDIOBJ previous{};
    DIBSECTION observed{};
    std::uint32_t* pixels{};
    COLORREF first_row{};
    bool rows_match = false;

    if (top_down) {
        if (!seed_recycled_dc_classifications(shim)) {
            std::fprintf(stderr,
                         "shim-gate=failure|stage:dib-overlap-seed\n");
            goto cleanup;
        }
        if (create_custom_control(api, "shim.top-down.overlap", &endpoint_control) != GF_STATUS_OK ||
            api.endpoints.acquire(
                endpoint_control, width, height, &endpoint,
                &endpoint_window) != GF_STATUS_OK) {
            std::fprintf(stderr,
                         "shim-gate=failure|stage:dib-overlap-acquire\n");
            goto cleanup;
        }
        // The seeded value may still be recorded as ordinary. GetDC
        // reclassifies it; on the bridge route only CreateCompatibleBitmap's
        // own ABI query keeps the shim from creating a bottom-up bitmap.
        if (source == DibOverlapSource::top_down_shim_get_dc) {
            endpoint_dc = get_dc(as_window(endpoint_window));
        } else {
            std::uint64_t bridge_dc{};
            if (api.endpoints.get_dc(
                    endpoint_window, &bridge_dc) == GF_STATUS_OK) {
                endpoint_dc = reinterpret_cast<HDC>(static_cast<std::uintptr_t>(bridge_dc));
            }
        }
        if (endpoint_dc == nullptr) {
            std::fprintf(stderr,
                         "shim-gate=failure|stage:dib-overlap-get-dc\n");
            goto cleanup;
        }
        bitmap = create_bitmap(endpoint_dc, width, height);
        dc = create_dc(endpoint_dc);
    } else {
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS,
                                  &storage, nullptr, 0);
        dc = create_dc(nullptr);
    }
    if (bitmap == nullptr || dc == nullptr) {
        std::fprintf(stderr,
                     "shim-gate=failure|stage:dib-overlap-create|"
                     "orientation:%s\n", orientation);
        goto cleanup;
    }
    previous = select_object(dc, bitmap);
    if (previous == nullptr || previous == HGDI_ERROR) {
        previous = nullptr;
        std::fprintf(stderr,
                     "shim-gate=failure|stage:dib-overlap-select|"
                     "orientation:%s\n", orientation);
        goto cleanup;
    }
    if (GetObjectW(bitmap, sizeof(observed), &observed) <
            static_cast<int>(sizeof(observed)) ||
        observed.dsBm.bmBits == nullptr) {
        std::fprintf(stderr,
                     "shim-gate=failure|stage:dib-overlap-object|"
                     "orientation:%s\n", orientation);
        goto cleanup;
    }

    // Logical row y holds gray level y + 1 in every column.
    pixels = static_cast<std::uint32_t*>(observed.dsBm.bmBits);
    for (int y = 0; y < height; ++y) {
        const int storage_y = top_down ? y : height - 1 - y;
        for (int x = 0; x < width; ++x) {
            pixels[storage_y * width + x] =
                0xff000000U | static_cast<std::uint32_t>((y + 1) * 0x010101);
        }
    }
    first_row = GetPixel(dc, 0, 0);
    if (first_row != RGB(1, 1, 1)) {
        std::fprintf(stderr,
                     "shim-gate=failure|stage:dib-orientation|"
                     "orientation:%s|logical-row-0:%06lx\n",
                     orientation, static_cast<unsigned long>(first_row));
        goto cleanup;
    }

    rows_match = bit_blt(dc, 0, 1, width, height - 1,
                         dc, 0, 0, SRCCOPY) != FALSE;
    for (int y = 1; y < height && rows_match; ++y) {
        const std::uint32_t expected =
            0xff000000U | static_cast<std::uint32_t>(y * 0x010101);
        const int storage_y = top_down ? y : height - 1 - y;
        for (int x = 0; x < width; ++x) {
            const std::uint32_t actual = pixels[storage_y * width + x];
            if (actual != expected) {
                std::fprintf(stderr,
                             "shim-gate=diagnostic|stage:dib-overlap|y:%d|"
                             "x:%d|actual:%08lx|expected:%08lx\n",
                             y, x, static_cast<unsigned long>(actual),
                             static_cast<unsigned long>(expected));
                rows_match = false;
                break;
            }
        }
    }
    if (!rows_match) {
        std::fprintf(stderr,
                     "shim-gate=failure|stage:dib-overlap|orientation:%s\n",
                     orientation);
    }

cleanup:
    // Objects the shim created are destroyed through the shim so that it
    // forgets their handle values before GDI recycles them.
    if (previous != nullptr) select_object(dc, previous);
    if (bitmap != nullptr) delete_object(bitmap);
    if (dc != nullptr) delete_dc(dc);
    if (endpoint_dc != nullptr &&
        source == DibOverlapSource::top_down_shim_get_dc) {
        release_dc(as_window(endpoint_window), endpoint_dc);
    } else if (endpoint_dc != nullptr) {
        api.endpoints.release_dc(
            endpoint_window, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(endpoint_dc)));
    }
    if (endpoint != 0U) api.endpoints.release(endpoint);
    if (endpoint_control != 0U) dispose_control(api, endpoint_control);
    return rows_match;
}

bool run_dib_overlap_gate(const AbiTables& api, HMODULE shim) {
    return run_dib_overlap_case(
               api, shim, DibOverlapSource::bottom_up_section) &&
        run_dib_overlap_case(
            api, shim, DibOverlapSource::top_down_shim_get_dc) &&
        run_dib_overlap_case(
            api, shim, DibOverlapSource::top_down_abi_bridge);
}

bool run_concurrent_release_gate(const AbiTables& api) {
    gf_object control{};
    std::uint64_t endpoint{};
    std::uint64_t window_value{};
    std::uint64_t endpoint_dc{};
    if (create_custom_control(api, "shim.release.test", &control) != GF_STATUS_OK ||
        api.endpoints.acquire(
            control, 332U, 324U, &endpoint, &window_value) != GF_STATUS_OK ||
        api.endpoints.get_dc(
            window_value, &endpoint_dc) != GF_STATUS_OK) {
        std::fprintf(stderr, "shim-gate=failure|stage:release-acquire\n");
        return false;
    }

    std::atomic<bool> lease_acquired{};
    std::atomic<bool> allow_end{};
    std::atomic<bool> release_started{};
    std::atomic<bool> writer_ok{};
    std::atomic<bool> release_ok{};
    std::thread writer(&run_release_writer, &api.endpoints, endpoint_dc, &lease_acquired,
                       &allow_end, &writer_ok);
    std::thread releaser(&run_endpoint_releaser, &api.endpoints, endpoint, &lease_acquired,
                         &release_started, &release_ok);
    while (!release_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    // Give release an opportunity to reach the endpoint state lock. It must
    // wait there until the destination operation relinquishes its short lease.
    for (int spin = 0; spin < 1000; ++spin) std::this_thread::yield();
    allow_end.store(true, std::memory_order_release);
    writer.join();
    releaser.join();
    dispose_control(api, control);
    if (!writer_ok.load(std::memory_order_acquire) ||
        !release_ok.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "shim-gate=failure|stage:release-operation\n");
        return false;
    }
    return true;
}

} // namespace

int main() {
    AbiTables api{};
    if (!negotiate_tables(&api)) {
        std::fprintf(stderr, "shim-gate=failure|stage:api\n");
        return 1;
    }
    HMODULE shim = LoadLibraryW(L"gui_forms_win32_compat.dll");
    if (shim == nullptr) {
        std::fprintf(stderr, "shim-gate=failure|stage:load\n");
        return 1;
    }
    constexpr std::array<std::array<int, 2>, 6> sizes{{
        {{150, 150}}, {{90, 150}}, {{30, 150}},
        {{332, 324}}, {{303, 324}}, {{128, 64}},
    }};
    for (int lifetime = 0; lifetime < 500; ++lifetime) {
        const std::array<int, 2> size =
            sizes[static_cast<std::size_t>(lifetime) % sizes.size()];
        const std::array<int, 2> resized = sizes[
            (static_cast<std::size_t>(lifetime) + 1U) % sizes.size()];
        if (!run_lifetime(api, shim, lifetime, size[0], size[1], resized[0],
                          resized[1])) {
            FreeLibrary(shim);
            return 1;
        }
    }
    if (!run_concurrent_resize_gate(api, shim)) {
        FreeLibrary(shim);
        return 1;
    }
    if (!run_dib_overlap_gate(api, shim)) {
        FreeLibrary(shim);
        return 1;
    }
    if (!run_concurrent_release_gate(api)) {
        FreeLibrary(shim);
        return 1;
    }
    FreeLibrary(shim);
    std::puts("shim-gate=pass|lifetimes:500|frames:36000|resize:concurrent|release:serialized");
    return 0;
}
