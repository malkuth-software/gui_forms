// Exercise the Windows host's raster (stage SK3: the Skia CPU painter behind
// the window's DIB) and its clipboard images in the same translation unit.
#include "gui_forms/canvas.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/window.hpp"
#include "../src/host/windows/application/windows_host.cpp"
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <chrono>
#include <string_view>

using namespace gui_forms;

void require_at(bool value, int line) { if (!value) { std::cerr << "failed at " << line << std::endl; std::abort(); } }
#define require(value) require_at((value), __LINE__)

void compare(ImageSampling sampling, double zoom, double scale, bool edited) {
    const std::shared_ptr<gui_drawing::Bitmap> bitmap = std::make_shared<gui_drawing::Bitmap>(1025, 1025);
    gui_drawing::BitmapEditView edit = (*bitmap).begin_edit({0, 0, 1025, 1025});
    for (int y = 0; y < 1025; ++y) for (int x = 0; x < 1025; ++x) {
        std::byte* pixel = edit.writable_data + y * edit.row_bytes + x * 4;
        const unsigned alpha = 80 + (x * 3 + y * 5) % 176;
        pixel[0] = std::byte((x % 251) * alpha / 255);
        pixel[1] = std::byte((y % 251) * alpha / 255);
        pixel[2] = std::byte(((x + y) % 251) * alpha / 255);
        pixel[3] = std::byte(alpha);
    }
    static_cast<void>((*bitmap).commit_edit(edit.token));
    const std::shared_ptr<RasterCanvas> canvas = make_control<RasterCanvas>(StableId("raster"));
    (*canvas).bitmap = bitmap;
    (*canvas).transparency_grid = false;
    (*canvas).canvas_background = Color::rgba(23, 37, 51);
    (*canvas).sampling = sampling;
    (*canvas).set_view(zoom, {470.3, 480.7});
    const Size size{180, 150};
    Window window(canvas, size);
    // The window is told its device scale, as a host does: painted
    // rectangles grow to whole pixels of that scale.
    window.set_scale(scale);
    window.perform_layout();
    host::WindowsRaster tiled;
    require(tiled.resize(size, scale));
    require(tiled.synchronize_images(window.image_resources()));
    DamageRegion damage;
    damage.add({0, 0, size.width, size.height});
    tiled.begin_frame();
    require(window.paint(tiled.painter()).has_value());

    if (edited) {
        (*bitmap).set_pixel(511, 511, gui_drawing::Color::from_name("red"));
        (*bitmap).set_pixel(512, 512, gui_drawing::Color::from_name("blue"));
        require((*canvas).synchronize_bitmap());
        require(tiled.synchronize_images(window.image_resources()));
        tiled.begin_frame();
        require(window.paint(tiled.painter()).has_value());
    
    }
    const gui_drawing::ImageSnapshot snapshot = (*bitmap).snapshot();
    ImageRegistry registry;
    const ImageLoadResult loaded = registry.load_bgra32_premultiplied(1025, 1025, snapshot.row_bytes(), snapshot.pixels());
    require(bool(loaded));
    host::WindowsRaster reference;
    require(reference.resize(size, scale));
    require(reference.synchronize_images(registry));
    reference.begin_frame();
    reference.painter().fill_rect({0, 0, size.width, size.height}, Color::rgba(23, 37, 51));
    reference.painter().draw_image_region_sampled(loaded.image, {0, 0, 1025, 1025},
        {-470.3 * zoom, -480.7 * zoom, 1025 * zoom, 1025 * zoom}, sampling, 1);

    require(tiled.save_bmp(L"canvas_tiled_test.bmp"));
    require(reference.save_bmp(L"canvas_reference_test.bmp"));
    std::ifstream af("canvas_tiled_test.bmp", std::ios::binary);
    std::ifstream bf("canvas_reference_test.bmp", std::ios::binary);
    std::vector<unsigned char> a((std::istreambuf_iterator<char>(af)), {});
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(bf)), {});
    require(a.size() == b.size());
    af.close(); bf.close();
    std::remove("canvas_tiled_test.bmp"); std::remove("canvas_reference_test.bmp");
    unsigned maximum{};
    std::size_t differences{};
    for (std::size_t index = 0; index < a.size(); ++index) {
        const unsigned distance = static_cast<unsigned>(std::abs(int(a[index]) - int(b[index])));
        maximum = std::max(maximum, distance);
        if (distance > 1) {

            ++differences;
        }
    }
    std::cout << "sampling=" << int(sampling) << " zoom=" << zoom << " scale=" << scale
              << " edited=" << edited << " max_channel_error=" << maximum << " differences=" << differences << '\n';
    require(maximum <= 1);
}
// The clipboard is one per desktop: copies of this test running at the same
// time (a parallel ctest, other checkouts on the machine) would replace each
// other's contents between the write and the read. A named mutex serializes
// them; the adapter's retrying open covers other processes that hold the
// clipboard open for a moment after it changes.
class ClipboardTestLock final {
public:
    ClipboardTestLock() : mutex_(CreateMutexW(nullptr, FALSE, L"Local\\GUI.Forms.tests.clipboard")) {
        require(mutex_ != nullptr);
        const DWORD waited = WaitForSingleObject(mutex_, 120000U);
        require(waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED);
    }
    ~ClipboardTestLock() {
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
    }
    ClipboardTestLock(const ClipboardTestLock&) = delete;
    ClipboardTestLock& operator=(const ClipboardTestLock&) = delete;

private:
    HANDLE mutex_;
};

void clipboard_files() {
    const ClipboardTestLock lock;
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("clipboard-root"));
    Window model(root, {1, 1});
    const HWND owner = CreateWindowExW(0, L"STATIC", L"Clipboard file fixture", 0,
                                        0, 0, 1, 1, HWND_MESSAGE, nullptr, nullptr, nullptr);
    require(owner != nullptr);
    const wchar_t path[] = L"C:\\Pictures\\Rainstar copied image Ω.png\0";
    const SIZE_T size = sizeof(DROPFILES) + sizeof(path);
    HGLOBAL allocation = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    require(allocation != nullptr);
    void* memory = GlobalLock(allocation);
    require(memory != nullptr);
    DROPFILES* header = static_cast<DROPFILES*>(memory);
    (*header).pFiles = sizeof(DROPFILES);
    (*header).fWide = TRUE;
    std::memcpy(static_cast<std::byte*>(memory) + sizeof(DROPFILES), path, sizeof(path));
    GlobalUnlock(allocation);
    require(host::detail::open_clipboard_with_retry(owner));
    require(EmptyClipboard());
    require(SetClipboardData(CF_HDROP, allocation) != nullptr);
    CloseClipboard();
    host::WindowsHostServices services;
    services.bind_owner(owner, model);
    const HostClipboardFilesResult files = services.read_clipboard_files();
    require(files.status.accepted());
    require(files.paths_utf8.size() == 1);
    require(files.paths_utf8.front() == "C:\\Pictures\\Rainstar copied image Ω.png");
    require(host::detail::open_clipboard_with_retry(owner));
    require(EmptyClipboard());
    CloseClipboard();
    require(services.read_clipboard_files().paths_utf8.empty());
    require(DestroyWindow(owner));
}
// Publishes `bytes` as `format` on the open clipboard.
void publish_clipboard(UINT format, const std::vector<std::byte>& bytes) {
    HGLOBAL allocation = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    require(allocation != nullptr);
    void* memory = GlobalLock(allocation);
    require(memory != nullptr);
    std::memcpy(memory, bytes.data(), bytes.size());
    GlobalUnlock(allocation);
    require(SetClipboardData(format, allocation) != nullptr);
}

// A host image (premultiplied BGRA8) round-trips exactly through the private
// format; through CF_DIBV5 alone (straight alpha) a valid premultiplied image
// is read back exactly too; a version 1 private image (straight RGBA8, written
// before SK2) is premultiplied into BGRA8.
void clipboard_images() {
    const ClipboardTestLock lock;
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("clipboard-image-root"));
    Window model(root, {1, 1});
    const HWND owner = CreateWindowExW(0, L"STATIC", L"Clipboard image fixture", 0,
                                        0, 0, 1, 1, HWND_MESSAGE, nullptr, nullptr, nullptr);
    require(owner != nullptr);
    host::WindowsHostServices services;
    services.bind_owner(owner, model);
    // Opaque blue, then red 200, green 100, blue 50 at alpha 128.
    const std::vector<std::byte> pixels{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255},
                                        std::byte{25}, std::byte{50}, std::byte{100}, std::byte{128}};
    require(services.write_clipboard_image({2, 1, 8, pixels}).accepted());
    HostClipboardImageResult read = services.read_clipboard_image();
    require(read.status().accepted() && read.image() != nullptr && (*read.image()).pixels == pixels);

    require(host::detail::open_clipboard_with_retry(owner));
    require(IsClipboardFormatAvailable(RegisterClipboardFormatW(L"GUI.Forms.BGRA8.v2")) != FALSE);
    const HANDLE dib = GetClipboardData(CF_DIBV5);
    require(dib != nullptr);
    const SIZE_T dib_size = GlobalSize(dib);
    const std::byte* const dib_bytes = static_cast<const std::byte*>(GlobalLock(dib));
    require(dib_bytes != nullptr && dib_size >= 132U);
    const std::vector<std::byte> dib_copy(dib_bytes, dib_bytes + dib_size);
    GlobalUnlock(dib);
    // The DIB holds straight alpha: 25, 50 and 100 at alpha 128 unpremultiply.
    require(dib_copy[128] == std::byte{50} && dib_copy[129] == std::byte{100} && dib_copy[130] == std::byte{199} &&
            dib_copy[131] == std::byte{128});
    require(EmptyClipboard());
    publish_clipboard(CF_DIBV5, dib_copy);
    CloseClipboard();
    read = services.read_clipboard_image();
    require(read.status().accepted() && read.image() != nullptr && (*read.image()).pixels == pixels);

    const std::vector<std::byte> version_one{
        std::byte{'G'}, std::byte{'F'}, std::byte{'I'}, std::byte{'M'}, std::byte{1}, std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{2}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}, std::byte{200},
        std::byte{100}, std::byte{50}, std::byte{128}};
    require(host::detail::open_clipboard_with_retry(owner));
    require(EmptyClipboard());
    publish_clipboard(RegisterClipboardFormatW(L"GUI.Forms.RGBA8.v1"), version_one);
    CloseClipboard();
    read = services.read_clipboard_image();
    require(read.status().accepted() && read.image() != nullptr && (*read.image()).pixels == pixels);
    require(host::detail::open_clipboard_with_retry(owner));
    require(EmptyClipboard());
    CloseClipboard();
    require(DestroyWindow(owner));
}

#include "support/edge_snapping_scene.hpp"

// Owner decision L-Q8 on the window's raster: each box edge rounds to the
// nearest device pixel, so neighbours share an edge (support/edge_snapping_scene.hpp).
void edge_snapping_scene() {
    for (const double scale : edge_snapping::scales) {
        host::WindowsRaster painter;
        require(painter.resize(edge_snapping::scene_size, scale));
        painter.begin_frame();
        edge_snapping::paint_scene(painter.painter());
        require(painter.save_bmp(L"edge-snapping.bmp"));
        std::ifstream file("edge-snapping.bmp", std::ios::binary);
        const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
        file.close();
        std::remove("edge-snapping.bmp");
        edge_snapping::PixelImage image{};
        image.width = static_cast<int>(std::ceil(edge_snapping::scene_size.width * scale));
        image.height = static_cast<int>(std::ceil(edge_snapping::scene_size.height * scale));
        image.row_bytes = static_cast<std::size_t>(image.width) * 4U;
        // A top-down 32-bit BMP: the pixels follow the 54-byte headers.
        require(bytes.size() >= 54U + image.row_bytes * static_cast<std::size_t>(image.height));
        image.bytes = bytes.data() + 54U;
        require(edge_snapping::check_scene(image, scale));
    }
}

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    edge_snapping_scene();
    clipboard_files();
    clipboard_images();
    for (ImageSampling sampling : {ImageSampling::nearest, ImageSampling::linear})
        for (double zoom : {1.0 / 64, 0.1, 0.25, 1.0, 1.25, 3.1})
            for (double scale : {1.0, 1.5, 2.0})
                for (bool edited : {false, true}) compare(sampling, zoom, scale, edited);
    CoUninitialize();
}
