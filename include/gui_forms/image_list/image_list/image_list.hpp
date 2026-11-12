#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/resources.hpp"
#include "gui_forms/scheduler.hpp"

#include <any>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class Window;

// Visual-state variants are optional. Resolution falls back through a bounded,
// deterministic chain and never manufactures a second raster in a control.
enum class ImageVisualState : std::uint8_t {
    normal,
    hot,
    pressed,
    selected,
    disabled,
};

enum class ImageListChangeKind : std::uint8_t {
    added,
    replaced,
    removed,
    renamed,
    layout,
    reset,
};

struct GUI_FORMS_API ImageListChange final {
    ImageListChangeKind kind{ImageListChangeKind::reset};
    std::size_t index{};
    std::string key;
    std::uint64_t revision{};
};

struct GUI_FORMS_API ImageListResolution final {
    ImageId image{};
    Size source_size{};
    ImageVisualState requested_state{ImageVisualState::normal};
    ImageVisualState resolved_state{ImageVisualState::normal};
    double requested_scale{1.0};
    double resolved_scale{1.0};

    [[nodiscard]] explicit operator bool() const noexcept {
        return image.value != 0U;
    }
};

// A retained, renderer-neutral counterpart to the useful ImageList behavior:
// ordered index lookup, ASCII-case-insensitive keys, generational Window resources,
// density/state variants, atomic mutation, and deterministic disposal. PNG is
// the only encoded import format admitted by the GUI.Forms resource boundary.
class GUI_FORMS_API ImageList final : public Component {
public:
    explicit ImageList(Window& window, Size image_size = {16.0, 16.0});
    ~ImageList() override;

    // The window the list was created for, while it lives.
    [[nodiscard]] Window* bound_window() const noexcept;
    // True for every window sharing the creating window's resources
    // (ApplicationResources): the list's images are valid in all of them.
    // The list keeps working after the creating window closes, as long as
    // the resources live.
    [[nodiscard]] bool belongs_to(const Window& window) const noexcept;
    [[nodiscard]] Size image_size() const noexcept { return image_size_; }
    void set_image_size(Size image_size);
    [[nodiscard]] std::size_t count() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    [[nodiscard]] ImageLoadResult add_png(
        std::string key, std::span<const std::byte> encoded,
        double density_scale = 1.0);
    [[nodiscard]] ImageLoadResult set_variant_png(
        std::string_view key, ImageVisualState state, double density_scale,
        std::span<const std::byte> encoded);
    std::size_t add_image(std::string key, ImageId image,
                          double density_scale = 1.0);
    void set_variant_image(std::string_view key, ImageVisualState state,
                           double density_scale, ImageId image);

    [[nodiscard]] std::string_view key_at(std::size_t index) const;
    [[nodiscard]] std::optional<std::size_t> index_of_key(
        std::string_view key) const noexcept;
    [[nodiscard]] bool contains_key(std::string_view key) const noexcept {
        return index_of_key(key).has_value();
    }
    void set_key_name(std::size_t index, std::string key);
    bool remove_at(std::size_t index);
    bool remove_by_key(std::string_view key);
    void clear();

    [[nodiscard]] ImageListResolution resolve(
        std::size_t index, ImageVisualState state = ImageVisualState::normal,
        double density_scale = 1.0) const noexcept;
    [[nodiscard]] ImageListResolution resolve(
        std::string_view key,
        ImageVisualState state = ImageVisualState::normal,
        double density_scale = 1.0) const noexcept;

    [[nodiscard]] const std::any& tag() const noexcept { return tag_; }
    void set_tag(std::any tag);
    void clear_tag();
    [[nodiscard]] Event<const ImageListChange&>& changed() noexcept {
        return changed_;
    }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void verify_dispose_thread() override;
    void on_dispose() noexcept override;

private:
    struct Variant final {
        ImageVisualState state{ImageVisualState::normal};
        double density_scale{1.0};
        ImageId image{};
        Size source_size{};
        bool owned{};
    };
    struct Entry final {
        std::string key;
        std::vector<Variant> variants;
    };

    void require_access(std::string_view operation) const;
    [[nodiscard]] static bool valid_state(ImageVisualState state) noexcept;
    static void validate_key(std::string_view key);
    static void validate_density(double density_scale);
    static void validate_image_size(Size image_size);
    [[nodiscard]] std::size_t require_index(std::string_view key) const;
    [[nodiscard]] std::optional<std::size_t> variant_index(
        const Entry& entry, ImageVisualState state,
        double density_scale) const noexcept;
    [[nodiscard]] Size source_size(ImageId image) const;
    void release_entry(Entry& entry) noexcept;
    void release_all() noexcept;
    void emit_change(ImageListChangeKind kind, std::size_t index,
                     std::string_view key);

    std::weak_ptr<detail::WindowLifetime> window_lifetime_;
    // The registry the list's images live in (observing: the windows own it).
    std::weak_ptr<ApplicationResources> resources_;
    Size image_size_{16.0, 16.0};
    std::vector<Entry> entries_;
    std::any tag_;
    Event<const ImageListChange&> changed_;
    std::uint64_t revision_{1U};
};

inline constexpr std::size_t maximum_image_list_entries = 1024U;
inline constexpr std::size_t maximum_image_list_variants_per_entry = 32U;

} // namespace gui_forms
