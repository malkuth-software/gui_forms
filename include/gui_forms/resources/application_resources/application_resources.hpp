#pragma once

#include "gui_forms/resources/image_registry/image_registry.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace gui_forms {

class Theme;
class Window;

// The image registry and the theme of an application (stage DK decision 8),
// shared by every Window constructed with it. In those windows an ImageId
// names the same image, an ImageList belongs to all of them and a theme set
// on one is the theme of all, so a control subtree moved from one window to
// another keeps its images and its look. A Window constructed without one
// makes its own, so a single-window program behaves as before.
//
// Ownership: shared by the windows (and by anyone else holding it); the
// registry and its images live until the last holder releases it. Each
// Window registers itself while it lives and the list only observes them.
// Every member runs on the thread that constructed the object, which must be
// the windows' UI thread.
class ApplicationResources final {
public:
    explicit ApplicationResources(ImageRegistryLimits limits = {});
    ~ApplicationResources();
    ApplicationResources(const ApplicationResources&) = delete;
    ApplicationResources& operator=(const ApplicationResources&) = delete;

    [[nodiscard]] bool check_access() const noexcept {
        const bool same = std::this_thread::get_id() == ui_thread_;
        return same;
    }
    [[nodiscard]] const ImageRegistry& images() const noexcept { return images_; }
    [[nodiscard]] const Theme& theme() const noexcept { return *theme_; }
    [[nodiscard]] std::shared_ptr<const Theme> theme_ptr() const noexcept { return theme_; }
    // Replaces the theme of every window sharing these resources; each
    // remeasures and repaints, and publishes its theme_changed. The same as
    // Window::set_theme on any of them. Null is invalid_argument, and so is
    // a theme against which an inline style defining custom properties, of
    // a control taking the windows' theme, does not compile: each such
    // style is compiled against the new theme first, and nothing changes
    // where one fails (P12b).
    void set_theme(std::shared_ptr<const Theme> theme);
    // The windows currently sharing these resources.
    [[nodiscard]] std::size_t window_count() const noexcept { return windows_.size(); }

    // Image operations, on the UI thread (std::logic_error elsewhere). Loads
    // change no window. A replacement or removal without a named consumer
    // repaints every sharing window, since any of them may show the image.
    [[nodiscard]] ImageLoadResult load_png(std::span<const std::byte> encoded);
    [[nodiscard]] ImageLoadResult load_bgra32_premultiplied(
        std::uint32_t width, std::uint32_t height, std::uint64_t row_bytes,
        std::span<const std::byte> pixels);
    [[nodiscard]] ImageLoadResult replace_png(ImageId image, std::span<const std::byte> encoded);
    [[nodiscard]] bool remove_image(ImageId image);

private:
    friend class Window;
    void require_access(const char* operation) const;
    void attach(Window& window);
    void detach(Window& window) noexcept;
    // Window's consumer-scoped operations reach the registry directly.
    [[nodiscard]] ImageRegistry& registry() noexcept { return images_; }

    std::thread::id ui_thread_;
    ImageRegistry images_;
    std::shared_ptr<const Theme> theme_;
    // Observing: each Window attaches in its constructor and detaches in its
    // destructor. Bounded by the windows the program creates on this thread.
    std::vector<Window*> windows_;
};

} // namespace gui_forms
