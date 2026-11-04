#pragma once

#include "demo/gallery.hpp"
#include "demo/gallery_page_specs.hpp"
#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"

#include <cmath>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Helpers shared by every gallery page test translation unit under
// tests/gallery_pages/. Page tests define `test_<page>(Window&)` in this
// namespace; tests/gallery_pages/main.cpp builds the windows and sequences
// them exactly as the former single-file test did.
namespace gallery_pages_tests {

using namespace gui_forms;

inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Predicate>
void require_eventually(Predicate predicate, const char* message) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error(message);
        }
        std::this_thread::yield();
    }
}

class DispatcherHasOnePending final {
public:
    explicit DispatcherHasOnePending(Window& window) noexcept : window_(window) {}

    bool operator()() const {
        return window_.dispatcher_snapshot().pending == 1U;
    }

private:
    Window& window_;
};

class GalleryDialogHandler final {
public:
    explicit GalleryDialogHandler(bool& cancel_save) noexcept
        : cancel_save_(cancel_save) {}

    HostDialogResult operator()(
        const HostDialogRequest& request,
        host::HeadlessHostServices&) const {
        if (std::holds_alternative<HostMessageDialogRequest>(request.payload)) {
            return {{}, request.request_id,
                    HostMessageDialogResult{HostDialogOutcome::accepted,
                                            HostDialogChoice::yes}};
        }

        if (std::holds_alternative<HostOpenFileDialogRequest>(request.payload)) {
            const HostOpenFileDialogRequest& payload =
                std::get<HostOpenFileDialogRequest>(request.payload);
            std::vector<std::string> paths{"/tmp/alpha.txt"};
            if (payload.allow_multiple) paths.push_back("/tmp/beta.log");
            return {{}, request.request_id,
                    HostPathDialogResult{HostDialogOutcome::accepted,
                                         std::move(paths)}};
        }

        if (std::holds_alternative<HostSaveFileDialogRequest>(request.payload)) {
            return cancel_save_
                ? HostDialogResult{{}, request.request_id,
                      HostPathDialogResult{HostDialogOutcome::cancelled, {}}}
                : HostDialogResult{{}, request.request_id,
                      HostPathDialogResult{HostDialogOutcome::accepted,
                                           {"/tmp/gui-forms-evidence.txt"}}};
        }

        if (std::holds_alternative<HostFolderDialogRequest>(request.payload)) {
            return {{}, request.request_id,
                    HostPathDialogResult{HostDialogOutcome::accepted,
                                         {"/tmp/evidence"}}};
        }

        return {{}, request.request_id,
                HostColorDialogResult{HostDialogOutcome::accepted,
                                      0x315F89FFU}};
    }

private:
    bool& cancel_save_;
};

class CountingPainter final : public Painter {
public:
    void save() override { ++commands; }
    void restore() override { ++commands; }
    void translate(Point) override { ++commands; }
    void clip_rect(Rect) override { ++commands; }
    void fill_rect(Rect, Color) override { ++commands; }
    void stroke_rect(Rect, Color, double) override { ++commands; }
    void draw_line(Point, Point, Color, double) override { ++commands; }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { ++commands; }
    void draw_image(ImageId, Rect, double) override {
        ++commands;
        ++images;
    }
    void draw_image_region(ImageId, Rect, Rect, double) override {
        ++commands;
        ++image_regions;
    }
    void fill_image_pattern(ImageId, Size, Rect, Size,
                            ImagePatternWrap, double) override {
        ++commands;
        ++image_patterns;
    }
    std::uint64_t commands{};
    std::uint64_t images{};
    std::uint64_t image_regions{};
    std::uint64_t image_patterns{};
};

class FillTracePainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect rect, Color) override { fills.push_back(rect); }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    std::vector<Rect> fills;
};

inline std::vector<Rect> paint_fill_trace(const Control::Ptr& control) {
    FillTracePainter painter;
    const Rect arranged = (*control).committed_arranged_bounds();
    (*control).on_paint(painter, {0.0, 0.0, arranged.width, arranged.height});
    return painter.fills;
}

inline Point center(const Control::Ptr& control) {
    const Rect bounds = (*control).absolute_bounds();
    return {bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
}

// A page taller than the shell's viewport scrolls (sweep defect D3): bring
// the target into the page viewport first, as a user would scroll to it.
inline void reveal_in_page_viewport(Window& window, const Control::Ptr& control) {
    for (Control::Ptr ancestor = (*control).parent(); ancestor; ancestor = (*ancestor).parent()) {
        const std::shared_ptr<ScrollableControl> viewport =
            std::dynamic_pointer_cast<ScrollableControl>(ancestor);
        const std::string_view id = (*ancestor).stable_id().value();
        if (!viewport || !id.ends_with(".viewport")) continue;
        (*viewport).scroll_control_into_view(control);
        window.perform_layout();
        return;
    }
}

inline void click(Window& window, const Control::Ptr& control) {
    if (!control) throw std::runtime_error("gallery click target is missing");
    reveal_in_page_viewport(window, control);
    const Point point = center(control);
    const bool down = window.dispatch_pointer(
        {PointerAction::down, PointerButton::primary, point});
    const bool up = window.dispatch_pointer(
        {PointerAction::up, PointerButton::primary, point});
    if (!down || !up) {
        const Rect bounds = (*control).absolute_bounds();
        const Control::Ptr hit = window.hit_test(point);
        const Control::Ptr captured = window.captured_control();
        throw std::runtime_error("gallery click failed for " +
                                 std::string((*control).stable_id().value()) +
                                 " (down=" + (down ? "true" : "false") +
                                 ", up=" + (up ? "true" : "false") +
                                 ", bounds=" + std::to_string(bounds.x) + "," +
                                 std::to_string(bounds.y) + "," +
                                 std::to_string(bounds.width) + "," +
                                 std::to_string(bounds.height) +
                                 ", hit=" + (hit ? std::string((*hit).stable_id().value())
                                                : std::string("none")) +
                                 ", eligible=" +
                                 ((*control).eligible_for_input() ? "true" : "false") +
                                 ", visible=" +
                                 ((*control).effectively_visible() ? "true" : "false") +
                                 ", enabled=" +
                                 ((*control).effectively_enabled() ? "true" : "false") +
                                 ", captured=" +
                                 (captured ? std::string((*captured).stable_id().value())
                                           : std::string("none")) +
                                 ")");
    }
}

// Selects a page the way a user does (click its navigation row), then proves
// exactly that page's retained surface is visible.
inline void select_page(Window& window, std::string_view page_id) {
    const Control::Ptr navigation = window.find(
        std::string(gallery::navigation_id_prefix) + std::string(page_id));
    require(navigation != nullptr, "gallery navigation stable ID must resolve");
    // The sidebar scrolls; bring the row into the viewport as a user would.
    if (const std::shared_ptr<ScrollableControl> sidebar =
            std::dynamic_pointer_cast<ScrollableControl>((*navigation).parent())) {
        const Rect row = (*navigation).requested_bounds();
        const Rect viewport = (*sidebar).viewport_rectangle();
        const Point position = (*sidebar).scroll_position();
        double target = position.y;
        if (row.y < position.y + 8.0) target = row.y - 8.0;
        else if (row.y + row.height > position.y + viewport.height - 8.0) {
            target = row.y + row.height + 8.0 - viewport.height;
        }
        target = std::max(0.0, target);
        if (target != position.y) static_cast<void>((*sidebar).scroll_to({position.x, target}));
        window.perform_layout();
    }
    click(window, navigation);
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        const Control::Ptr surface = window.find(
            std::string(gallery::surface_id_prefix) + std::string(page.id));
        require(surface && (*surface).effectively_visible() == (page.id == page_id),
                "gallery navigation must expose exactly one retained page");
    }
    require(gallery::selected_gallery_page(window) == page_id,
            "the selected page must be observable by stable id");
}

} // namespace gallery_pages_tests
