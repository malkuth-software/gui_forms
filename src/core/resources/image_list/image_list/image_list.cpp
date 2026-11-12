#include "gui_forms/image_list.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "../../../surface_root/access/surface_lifetime_access.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

[[nodiscard]] bool same_key(std::string_view left,
                            std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        unsigned char lhs = static_cast<unsigned char>(left[index]);
        unsigned char rhs = static_cast<unsigned char>(right[index]);
        if (lhs >= 'A' && lhs <= 'Z') lhs = static_cast<unsigned char>(lhs + 32U);
        if (rhs >= 'A' && rhs <= 'Z') rhs = static_cast<unsigned char>(rhs + 32U);
        if (lhs != rhs) return false;
    }
    return true;
}

} // namespace

ImageList::ImageList(Window& window, Size image_size)
    : window_lifetime_(detail::SurfaceLifetimeAccess::observe(window)), resources_(window.resources()),
      image_size_(image_size) {
    window.verify_access("ImageList construction");
    validate_image_size(image_size_);
}

ImageList::~ImageList() {
    // Explicit Component disposal is the deterministic path. Preserve safety
    // if an ordinary shared_ptr dies first on the owner thread; a wrong-thread
    // destructor never races the Window registry and the Window still owns the
    // bounded resources until its own teardown.
    if (is_alive()) {
        const std::shared_ptr<ApplicationResources> resources = resources_.lock();
        if (resources && (*resources).check_access()) {
            release_all();
        }
    }
}

Window* ImageList::bound_window() const noexcept {
    const std::shared_ptr<gui_forms::detail::WindowLifetime> lifetime = window_lifetime_.lock();
    return lifetime ? (*lifetime).window : nullptr;
}

bool ImageList::belongs_to(const Window& window) const noexcept {
    const std::shared_ptr<ApplicationResources> resources = resources_.lock();
    const bool shared = resources && window.resources() == resources;
    return shared;
}

void ImageList::require_access(std::string_view operation) const {
    if (!is_alive()) {
        throw std::logic_error("GUI.Forms cannot mutate a disposed ImageList");
    }
    const std::shared_ptr<ApplicationResources> resources = resources_.lock();
    if (!resources) {
        throw std::logic_error("GUI.Forms cannot use an ImageList after Window shutdown");
    }
    if (!(*resources).check_access()) {
        std::string message = "GUI.Forms ";
        message += operation;
        message += " must run on the window's UI thread";
        throw std::logic_error(message);
    }
}

bool ImageList::valid_state(ImageVisualState state) noexcept {
    switch (state) {
    case ImageVisualState::normal:
    case ImageVisualState::hot:
    case ImageVisualState::pressed:
    case ImageVisualState::selected:
    case ImageVisualState::disabled: return true;
    }
    return false;
}

void ImageList::validate_key(std::string_view key) {
    if (key.empty() || key.size() > 256U || !validate_utf8(key).valid()) {
        throw std::invalid_argument(
            "ImageList keys must be valid nonempty UTF-8 up to 256 bytes");
    }
}

void ImageList::validate_density(double density_scale) {
    if (!std::isfinite(density_scale) || density_scale < 0.5 ||
        density_scale > 8.0) {
        throw std::invalid_argument(
            "ImageList density scale must be finite and between 0.5 and 8");
    }
}

void ImageList::validate_image_size(Size image_size) {
    if (!std::isfinite(image_size.width) ||
        !std::isfinite(image_size.height) || image_size.width < 1.0 ||
        image_size.height < 1.0 || image_size.width > 512.0 ||
        image_size.height > 512.0) {
        throw std::invalid_argument(
            "ImageList logical image size must be finite and between 1 and 512");
    }
}

void ImageList::set_image_size(Size image_size) {
    require_access("ImageList image-size mutation");
    validate_image_size(image_size);
    if (image_size_ == image_size) return;
    image_size_ = image_size;
    emit_change(ImageListChangeKind::layout, 0U, {});
}

std::optional<std::size_t> ImageList::index_of_key(
    std::string_view key) const noexcept {
    std::vector<Entry>::const_iterator found = entries_.begin();
    while (found != entries_.end() && !same_key((*found).key, key)) {
        ++found;
    }
    if (found == entries_.end()) return {};
    return static_cast<std::size_t>(std::distance(entries_.begin(), found));
}

std::size_t ImageList::require_index(std::string_view key) const {
    const std::optional<std::size_t> index = index_of_key(key);
    if (!index) throw std::out_of_range("ImageList key is not present");
    return *index;
}

std::optional<std::size_t> ImageList::variant_index(
    const Entry& entry, ImageVisualState state,
    double density_scale) const noexcept {
    for (std::size_t index = 0; index < entry.variants.size(); ++index) {
        const Variant& variant = entry.variants[index];
        if (variant.state == state && variant.density_scale == density_scale) {
            return index;
        }
    }
    return {};
}

Size ImageList::source_size(ImageId image) const {
    const std::shared_ptr<ApplicationResources> owner = resources_.lock();
    const std::optional<ImageResourceView> resource = owner ? (*owner).images().find(image)
                                : std::optional<ImageResourceView>{};
    if (!resource) throw std::invalid_argument("ImageList requires a live Window image ID");
    return {static_cast<double>((*resource).metadata.width),
            static_cast<double>((*resource).metadata.height)};
}

ImageLoadResult ImageList::add_png(std::string key,
                                   std::span<const std::byte> encoded,
                                   double density_scale) {
    require_access("ImageList PNG add");
    validate_key(key);
    validate_density(density_scale);
    const std::optional<std::size_t> existing = index_of_key(key);
    if (!existing && entries_.size() >= maximum_image_list_entries) {
        throw std::length_error("ImageList entry limit exceeded");
    }
    if (existing && variant_index(entries_[*existing], ImageVisualState::normal,
                                  density_scale)) {
        throw std::invalid_argument("ImageList already contains this key/density variant");
    }
    const std::shared_ptr<ApplicationResources> owner = resources_.lock();
    ImageLoadResult loaded = (*owner).load_png(encoded);
    if (!loaded) return loaded;
    std::size_t changed_index{};
    try {
        const Variant variant{ImageVisualState::normal, density_scale,
                              loaded.image, source_size(loaded.image), true};
        if (existing) {
            Entry& entry = entries_[*existing];
            if (entry.variants.size() >= maximum_image_list_variants_per_entry) {
                static_cast<void>((*owner).remove_image(loaded.image));
                throw std::length_error("ImageList variant limit exceeded");
            }
            entry.variants.push_back(variant);
            changed_index = *existing;
        } else {
            entries_.push_back({std::move(key), {variant}});
            changed_index = entries_.size() - 1U;
        }
    } catch (...) {
        if ((*owner).images().find(loaded.image)) {
            static_cast<void>((*owner).remove_image(loaded.image));
        }
        throw;
    }
    emit_change(ImageListChangeKind::added, changed_index,
                entries_[changed_index].key);
    return loaded;
}

ImageLoadResult ImageList::set_variant_png(
    std::string_view key, ImageVisualState state, double density_scale,
    std::span<const std::byte> encoded) {
    require_access("ImageList PNG variant mutation");
    if (!valid_state(state)) throw std::invalid_argument("invalid ImageList visual state");
    validate_density(density_scale);
    const std::size_t entry_index = require_index(key);
    Entry& entry = entries_[entry_index];
    const std::optional<std::size_t> existing = variant_index(entry, state, density_scale);
    const std::shared_ptr<ApplicationResources> owner = resources_.lock();
    if (existing && entry.variants[*existing].owned) {
        Variant& variant = entry.variants[*existing];
        ImageLoadResult replaced = (*owner).replace_png(variant.image, encoded);
        if (!replaced) return replaced;
        variant.image = replaced.image;
        variant.source_size = source_size(replaced.image);
        emit_change(ImageListChangeKind::replaced, entry_index, entry.key);
        return replaced;
    }

    ImageLoadResult loaded = (*owner).load_png(encoded);
    if (!loaded) return loaded;
    ImageListChangeKind change_kind = ImageListChangeKind::added;
    try {
        Variant next{state, density_scale, loaded.image,
                     source_size(loaded.image), true};
        if (existing) {
            entry.variants[*existing] = next;
            change_kind = ImageListChangeKind::replaced;
        } else {
            if (entry.variants.size() >= maximum_image_list_variants_per_entry) {
                static_cast<void>((*owner).remove_image(loaded.image));
                throw std::length_error("ImageList variant limit exceeded");
            }
            entry.variants.push_back(next);
        }
    } catch (...) {
        if ((*owner).images().find(loaded.image)) {
            static_cast<void>((*owner).remove_image(loaded.image));
        }
        throw;
    }
    emit_change(change_kind, entry_index, entry.key);
    return loaded;
}

std::size_t ImageList::add_image(std::string key, ImageId image,
                                 double density_scale) {
    require_access("ImageList image add");
    validate_key(key);
    validate_density(density_scale);
    const Size source = source_size(image);
    const std::optional<std::size_t> existing = index_of_key(key);
    if (!existing && entries_.size() >= maximum_image_list_entries) {
        throw std::length_error("ImageList entry limit exceeded");
    }
    if (existing && variant_index(entries_[*existing], ImageVisualState::normal,
                                  density_scale)) {
        throw std::invalid_argument("ImageList already contains this key/density variant");
    }
    if (existing) {
        Entry& entry = entries_[*existing];
        if (entry.variants.size() >= maximum_image_list_variants_per_entry) {
            throw std::length_error("ImageList variant limit exceeded");
        }
        entry.variants.push_back(
            {ImageVisualState::normal, density_scale, image, source, false});
        emit_change(ImageListChangeKind::added, *existing, entry.key);
        return *existing;
    }
    entries_.push_back({std::move(key),
                        {{ImageVisualState::normal, density_scale, image,
                          source, false}}});
    emit_change(ImageListChangeKind::added, entries_.size() - 1U,
                entries_.back().key);
    return entries_.size() - 1U;
}

void ImageList::set_variant_image(std::string_view key, ImageVisualState state,
                                  double density_scale, ImageId image) {
    require_access("ImageList image variant mutation");
    if (!valid_state(state)) throw std::invalid_argument("invalid ImageList visual state");
    validate_density(density_scale);
    const Size source = source_size(image);
    const std::size_t entry_index = require_index(key);
    Entry& entry = entries_[entry_index];
    const std::optional<std::size_t> existing = variant_index(entry, state, density_scale);
    if (existing) {
        Variant& previous = entry.variants[*existing];
        if (previous.image == image) return;
        if (previous.owned) {
            const std::shared_ptr<ApplicationResources> owner = resources_.lock();
            if (owner) static_cast<void>((*owner).remove_image(previous.image));
        }
        previous = {state, density_scale, image, source, false};
        emit_change(ImageListChangeKind::replaced, entry_index, entry.key);
        return;
    }
    if (entry.variants.size() >= maximum_image_list_variants_per_entry) {
        throw std::length_error("ImageList variant limit exceeded");
    }
    entry.variants.push_back({state, density_scale, image, source, false});
    emit_change(ImageListChangeKind::added, entry_index, entry.key);
}

std::string_view ImageList::key_at(std::size_t index) const {
    if (index >= entries_.size()) throw std::out_of_range("ImageList index is outside the collection");
    return entries_[index].key;
}

void ImageList::set_key_name(std::size_t index, std::string key) {
    require_access("ImageList key mutation");
    if (index >= entries_.size()) throw std::out_of_range("ImageList index is outside the collection");
    validate_key(key);
    if (const std::optional<std::size_t> duplicate = index_of_key(key); duplicate && *duplicate != index) {
        throw std::invalid_argument("ImageList keys are unique ignoring ASCII case");
    }
    Entry& entry = entries_[index];
    if (entry.key == key) return;
    entry.key = std::move(key);
    emit_change(ImageListChangeKind::renamed, index, entry.key);
}

void ImageList::release_entry(Entry& entry) noexcept {
    const std::shared_ptr<ApplicationResources> owner = resources_.lock();
    if (!owner) return;
    for (Variant& variant : entry.variants) {
        if (!variant.owned || variant.image.value == 0U) continue;
        try {
            static_cast<void>((*owner).remove_image(variant.image));
        } catch (...) {
            // Component disposal cannot throw. Window teardown remains the
            // final owner of any resource whose release could not complete.
        }
        variant.image = {};
    }
}

void ImageList::release_all() noexcept {
    for (Entry& entry : entries_) release_entry(entry);
    entries_.clear();
}

bool ImageList::remove_at(std::size_t index) {
    require_access("ImageList removal");
    if (index >= entries_.size()) return false;
    const std::string key = entries_[index].key;
    release_entry(entries_[index]);
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    emit_change(ImageListChangeKind::removed, index, key);
    return true;
}

bool ImageList::remove_by_key(std::string_view key) {
    require_access("ImageList key removal");
    const std::optional<std::size_t> index = index_of_key(key);
    return index ? remove_at(*index) : false;
}

void ImageList::clear() {
    require_access("ImageList clear");
    if (entries_.empty()) return;
    release_all();
    emit_change(ImageListChangeKind::reset, 0U, {});
}

ImageListResolution ImageList::resolve(std::string_view key,
                                       ImageVisualState state,
                                       double density_scale) const noexcept {
    const std::optional<std::size_t> index = index_of_key(key);
    if (index) return resolve(*index, state, density_scale);
    ImageListResolution result;
    result.requested_state = state;
    result.requested_scale = density_scale;
    return result;
}

ImageListResolution ImageList::resolve(std::size_t index,
                                       ImageVisualState state,
                                       double density_scale) const noexcept {
    ImageListResolution result;
    result.requested_state = state;
    result.requested_scale = density_scale;
    if (!is_alive() || index >= entries_.size() || !valid_state(state) ||
        !std::isfinite(density_scale) || density_scale <= 0.0) return result;
    const std::shared_ptr<ApplicationResources> owner = resources_.lock();
    if (!owner) return result;

    std::array<ImageVisualState, 3> states{state, ImageVisualState::normal,
                                          ImageVisualState::normal};
    std::size_t state_count = 2U;
    // Pressed and selected fall back through hot to normal, normal stands
    // alone, and every other state falls back to normal (above).
    if (state == ImageVisualState::pressed) {
        states = {ImageVisualState::pressed, ImageVisualState::hot,
                  ImageVisualState::normal};
        state_count = 3U;
    } else if (state == ImageVisualState::selected) {
        states = {ImageVisualState::selected, ImageVisualState::hot,
                  ImageVisualState::normal};
        state_count = 3U;
    } else if (state == ImageVisualState::normal) {
        states[0] = ImageVisualState::normal;
        state_count = 1U;
    }

    const Entry& entry = entries_[index];
    for (std::size_t state_index = 0; state_index < state_count; ++state_index) {
        const ImageVisualState candidate_state = states[state_index];
        const Variant* exact{};
        const Variant* larger{};
        const Variant* smaller{};
        for (const Variant& variant : entry.variants) {
            if (variant.state != candidate_state ||
                !(*owner).images().find(variant.image)) continue;
            if (variant.density_scale == density_scale) exact = &variant;
            else if (variant.density_scale > density_scale &&
                     (!larger || variant.density_scale < (*larger).density_scale)) {
                larger = &variant;
            } else if (variant.density_scale < density_scale &&
                       (!smaller || variant.density_scale > (*smaller).density_scale)) {
                smaller = &variant;
            }
        }
        const Variant* chosen = exact ? exact : (larger ? larger : smaller);
        if (!chosen) continue;
        result.image = (*chosen).image;
        result.source_size = (*chosen).source_size;
        result.resolved_state = candidate_state;
        result.resolved_scale = (*chosen).density_scale;
        return result;
    }
    return result;
}

void ImageList::set_tag(std::any tag) {
    require_access("ImageList tag mutation");
    tag_ = std::move(tag);
}

void ImageList::clear_tag() {
    require_access("ImageList tag clear");
    tag_.reset();
}

void ImageList::emit_change(ImageListChangeKind kind, std::size_t index,
                            std::string_view key) {
    ++revision_;
    changed_.emit({kind, index, std::string(key), revision_});
}

void ImageList::verify_dispose_thread() {
    const std::shared_ptr<ApplicationResources> owner = resources_.lock();
    if (owner && !(*owner).check_access()) {
        throw std::logic_error("GUI.Forms ImageList disposal must run on the window's UI thread");
    }
}

void ImageList::on_dispose() noexcept {
    release_all();
    tag_.reset();
    ++revision_;
    try {
        changed_.emit({ImageListChangeKind::reset, 0U, {}, revision_});
    } catch (...) {
        // Disposal is authoritative even if an observing application callback
        // faults; Component::dispose is deliberately non-throwing here.
    }
    window_lifetime_.reset();
    resources_.reset();
}

namespace {

// The events ImageList adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 1> image_list_interactions{{
    {.name = "changed", .payload = "ImageListChange", .mutable_payload = false},
}};

} // namespace

void ImageList::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), image_list_interactions.begin(), image_list_interactions.end());
}

} // namespace gui_forms
