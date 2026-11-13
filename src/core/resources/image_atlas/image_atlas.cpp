#include "gui_forms/resources/image_atlas/image_atlas.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace gui_forms {
namespace {

constexpr std::uint32_t bytes_per_pixel = 4U;
constexpr std::uint32_t maximum_slot_side = 1024U;
constexpr std::uint32_t maximum_slot_count = 65536U;
constexpr std::uint32_t maximum_gutter = 8U;
constexpr std::uint32_t maximum_users_per_slot = 64U;

// The splitmix64 finalizer: spreads sequential keys over the index.
[[nodiscard]] std::uint64_t mixed_key(std::uint64_t key) noexcept {
    std::uint64_t value = key;
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31U;
    return value;
}

// The next generation of a slot, skipping 0 (no slot).
[[nodiscard]] std::uint32_t next_generation(std::uint32_t generation) noexcept {
    std::uint32_t next = generation + 1U;
    if (next == 0U) next = 1U;
    return next;
}

// Whether two weak references name the same control.
[[nodiscard]] bool same_owner(const std::weak_ptr<Control>& left, const std::weak_ptr<Control>& right) noexcept {
    const bool same = !left.owner_before(right) && !right.owner_before(left);
    return same;
}

void validate_layout(const ImageAtlasLayout& layout) {
    if (layout.slot_width == 0U || layout.slot_height == 0U || layout.slot_width > maximum_slot_side ||
        layout.slot_height > maximum_slot_side) {
        throw std::invalid_argument("ImageAtlas slot sides must be 1 to 1024 pixels");
    }
    if (layout.slot_count == 0U || layout.slot_count > maximum_slot_count) {
        throw std::invalid_argument("ImageAtlas slot count must be 1 to 65536");
    }
    if (layout.gutter > maximum_gutter) {
        throw std::invalid_argument("ImageAtlas gutter must be 0 to 8 pixels");
    }
    if (layout.users_per_slot == 0U || layout.users_per_slot > maximum_users_per_slot) {
        throw std::invalid_argument("ImageAtlas users per slot must be 1 to 64");
    }
    if (layout.replacement != ImageAtlasReplacement::least_recently_used &&
        layout.replacement != ImageAtlasReplacement::caller) {
        throw std::invalid_argument("ImageAtlas replacement policy is not one of its values");
    }
}

// The key index's capacity: the smallest power of two at least twice the
// slot count (at most 2^17 for 65536 slots).
[[nodiscard]] std::uint32_t key_capacity(std::uint32_t slot_count) noexcept {
    std::uint32_t capacity = 2U;
    while (capacity < slot_count * 2U) capacity *= 2U;
    return capacity;
}

} // namespace

ImageAtlas::ImageAtlas(const std::shared_ptr<ApplicationResources>& resources, ImageAtlasLayout layout)
    : resources_(resources), layout_(layout) {
    if (!resources) throw std::invalid_argument("ImageAtlas requires application resources");
    (*resources).require_access("ImageAtlas construction");
    validate_layout(layout_);

    // Every quantity below is bounded by the layout limits (cells at most
    // 1040 pixels, at most 65536 slots), so 64-bit products cannot overflow.
    cell_width_ = layout_.slot_width + 2U * layout_.gutter;
    cell_height_ = layout_.slot_height + 2U * layout_.gutter;
    const ImageRegistryLimits& limits = (*resources).images().limits();
    columns_ = std::min(layout_.slot_count, limits.maximum_width / cell_width_);
    if (columns_ == 0U) throw std::invalid_argument("ImageAtlas cell is wider than the registry's image limit");
    const std::uint32_t rows = (layout_.slot_count + columns_ - 1U) / columns_;
    const std::uint64_t width = static_cast<std::uint64_t>(columns_) * cell_width_;
    const std::uint64_t height = static_cast<std::uint64_t>(rows) * cell_height_;
    const std::uint64_t pixels = width * height;
    const std::uint64_t image_bytes = pixels * bytes_per_pixel;
    if (height > limits.maximum_height || pixels > limits.maximum_pixels ||
        image_bytes > limits.maximum_decoded_bytes_per_image ||
        image_bytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("ImageAtlas grid exceeds the registry's image limits");
    }
    image_width_ = static_cast<std::uint32_t>(width);
    image_height_ = static_cast<std::uint32_t>(height);

    // Every table the atlas will use, now; nothing below allocates again.
    slots_.resize(layout_.slot_count);
    users_.resize(static_cast<std::size_t>(layout_.slot_count) * layout_.users_per_slot);
    keys_.resize(key_capacity(layout_.slot_count));
    free_.reserve(layout_.slot_count);
    for (std::uint32_t slot = layout_.slot_count; slot > 0U; --slot) {
        free_.push_back(slot - 1U);
    }
    const std::size_t cell_bytes = static_cast<std::size_t>(cell_width_) * cell_height_ * bytes_per_pixel;
    staging_.resize(cell_bytes);
    const std::uint32_t pending_capacity = std::min(layout_.pending_fills, layout_.slot_count);
    if (pending_capacity != 0U) {
        // The pending cells are at most slot_count of the grid's cells, so
        // their bytes are at most image_bytes, checked above to fit a size.
        if (cell_bytes == 0U || pending_capacity > image_bytes / cell_bytes) {
            throw std::logic_error("ImageAtlas pending cells exceed the grid");
        }
        pending_pixels_.resize(cell_bytes * pending_capacity);
        pending_free_.reserve(pending_capacity);
        for (std::uint32_t cell = pending_capacity; cell > 0U; --cell) {
            pending_free_.push_back(cell - 1U);
        }
        pending_order_.reserve(pending_capacity);
    }

    // The transparent image, loaded from a zeroed buffer that lives for this
    // block only (counted under image_atlas, then released).
    const std::size_t row_bytes = static_cast<std::size_t>(image_width_) * bytes_per_pixel;
    Table<std::byte> blank(static_cast<std::size_t>(image_bytes), std::byte{0});
    const ImageLoadResult loaded = (*resources).load_bgra32_premultiplied(
        image_width_, image_height_, row_bytes, std::span<const std::byte>(blank.data(), blank.size()));
    if (!loaded) {
        std::string message = "ImageAtlas backing image refused: ";
        message += image_resource_error_name(loaded.error);
        throw std::runtime_error(message);
    }
    image_ = loaded.image;
    // Registered last, so that a refusal above leaves nothing behind; a
    // failed registration removes the image again.
    try {
        link_ = std::make_shared<detail::ImageAtlasLink>();
        (*link_).atlas = this;
        (*resources).register_image_atlas(link_);
    } catch (...) {
        static_cast<void>((*resources).remove_image(image_));
        throw;
    }
}

ImageAtlas::~ImageAtlas() {
    // Removing the image repaints every window sharing the resources, so no
    // user need be damaged here. Gone resources, or another thread, leave the
    // image to the registry's own teardown.
    // The link dies with the atlas, so the resources skip it from now on;
    // pending fills are dropped with the atlas.
    if (link_ != nullptr) (*link_).atlas = nullptr;
    const std::shared_ptr<ApplicationResources> resources = resources_.lock();
    if (resources && (*resources).check_access()) {
        static_cast<void>((*resources).remove_image(image_));
    }
}

std::shared_ptr<ApplicationResources> ImageAtlas::require_access(const char* operation) const {
    std::shared_ptr<ApplicationResources> resources = resources_.lock();
    if (!resources) {
        std::string message = "GUI.Forms ";
        message += operation;
        message += ": the atlas's application resources are gone";
        throw std::logic_error(message);
    }
    (*resources).require_access(operation);
    return resources;
}

bool ImageAtlas::current(ImageAtlasSlot slot) const noexcept {
    if (slot.generation == 0U || slot.index >= slots_.size()) return false;
    const SlotRecord& record = slots_[slot.index];
    const bool live = record.state != SlotState::free && record.generation == slot.generation;
    return live;
}

std::uint32_t ImageAtlas::key_position(std::uint64_t key) const noexcept {
    const std::uint64_t mask = static_cast<std::uint64_t>(keys_.size()) - 1U;
    const std::uint32_t position = static_cast<std::uint32_t>(mixed_key(key) & mask);
    return position;
}

std::uint32_t ImageAtlas::find_slot_index(std::uint64_t key) const noexcept {
    const std::uint32_t mask = static_cast<std::uint32_t>(keys_.size()) - 1U;
    std::uint32_t position = key_position(key);
    // The index is at most half full, so an empty entry ends every probe.
    while (keys_[position].slot_plus_one != 0U) {
        if (keys_[position].key == key) return keys_[position].slot_plus_one - 1U;
        position = (position + 1U) & mask;
    }
    return no_slot;
}

void ImageAtlas::insert_key(std::uint64_t key, std::uint32_t slot) noexcept {
    const std::uint32_t mask = static_cast<std::uint32_t>(keys_.size()) - 1U;
    std::uint32_t position = key_position(key);
    while (keys_[position].slot_plus_one != 0U) {
        position = (position + 1U) & mask;
    }
    keys_[position].key = key;
    keys_[position].slot_plus_one = slot + 1U;
}

void ImageAtlas::erase_key(std::uint64_t key) noexcept {
    const std::uint32_t mask = static_cast<std::uint32_t>(keys_.size()) - 1U;
    std::uint32_t hole = key_position(key);
    while (keys_[hole].slot_plus_one != 0U && keys_[hole].key != key) {
        hole = (hole + 1U) & mask;
    }
    if (keys_[hole].slot_plus_one == 0U) return;
    keys_[hole] = KeyEntry{};
    // Backward shift: each later entry of the run whose home is not
    // cyclically within (hole, next] moves into the hole, so every probe
    // still reaches its entry before an empty one.
    std::uint32_t next = hole;
    for (;;) {
        next = (next + 1U) & mask;
        if (keys_[next].slot_plus_one == 0U) return;
        const std::uint32_t home = key_position(keys_[next].key);
        bool stays = false;
        if (hole <= next) {
            stays = hole < home && home <= next;
        } else {
            stays = hole < home || home <= next;
        }
        if (stays) continue;
        keys_[hole] = keys_[next];
        keys_[next] = KeyEntry{};
        hole = next;
    }
}

void ImageAtlas::link_front(std::uint32_t slot) noexcept {
    SlotRecord& record = slots_[slot];
    record.newer = no_slot;
    record.older = newest_;
    if (newest_ != no_slot) slots_[newest_].newer = slot;
    newest_ = slot;
    if (oldest_ == no_slot) oldest_ = slot;
}

void ImageAtlas::unlink(std::uint32_t slot) noexcept {
    SlotRecord& record = slots_[slot];
    if (record.newer != no_slot) {
        slots_[record.newer].older = record.older;
    } else {
        newest_ = record.older;
    }
    if (record.older != no_slot) {
        slots_[record.older].newer = record.newer;
    } else {
        oldest_ = record.newer;
    }
    record.newer = no_slot;
    record.older = no_slot;
}

void ImageAtlas::touch(std::uint32_t slot) noexcept {
    if (newest_ == slot) return;
    unlink(slot);
    link_front(slot);
}

void ImageAtlas::assign(std::uint32_t slot, std::uint64_t key) noexcept {
    SlotRecord& record = slots_[slot];
    record.key = key;
    record.state = SlotState::assigned;
    insert_key(key, slot);
    link_front(slot);
    ++statistics_.slots_in_use;
}

void ImageAtlas::retire(std::uint32_t slot) noexcept {
    drop_pending(slot);
    SlotRecord& record = slots_[slot];
    erase_key(record.key);
    unlink(slot);
    record.generation = next_generation(record.generation);
    record.state = SlotState::free;
    record.key = 0U;
    --statistics_.slots_in_use;
}

void ImageAtlas::damage_users(std::uint32_t slot, bool whole) {
    SlotRecord& record = slots_[slot];
    const std::size_t first = static_cast<std::size_t>(slot) * layout_.users_per_slot;
    // Each entry is forgotten before its control is asked, so a throw from
    // invalidate (the window's damage list growing) leaves the remaining
    // entries recorded and the forgotten ones expired; remember_user reuses
    // expired entries and this loop skips them.
    for (std::uint32_t index = 0U; index < record.user_count; ++index) {
        UserRecord& entry = users_[first + index];
        const std::shared_ptr<Control> control = entry.control.lock();
        const Rect area = entry.area;
        entry.control.reset();
        entry.area = Rect{};
        if (!control || !(*control).is_alive()) continue;
        if (whole) {
            (*control).invalidate(Dirty::paint);
        } else {
            (*control).invalidate(area);
        }
        ++statistics_.users_damaged;
    }
    record.user_count = 0U;
}

bool ImageAtlas::remember_user(std::uint32_t slot, Control& user, Rect destination) noexcept {
    SlotRecord& record = slots_[slot];
    const std::size_t first = static_cast<std::size_t>(slot) * layout_.users_per_slot;
    const std::weak_ptr<Control> wanted = user.weak_from_this();
    std::uint32_t reusable = no_slot;
    for (std::uint32_t index = 0U; index < record.user_count; ++index) {
        UserRecord& entry = users_[first + index];
        if (same_owner(entry.control, wanted)) {
            entry.area = Rect::united(entry.area, destination);
            return true;
        }
        if (reusable == no_slot && entry.control.expired()) reusable = index;
    }
    if (reusable == no_slot) {
        if (record.user_count >= layout_.users_per_slot) return false;
        reusable = record.user_count;
        ++record.user_count;
    }
    UserRecord& entry = users_[first + reusable];
    entry.control = wanted;
    entry.area = destination;
    return true;
}

std::uint32_t ImageAtlas::cell_x(std::uint32_t slot) const noexcept {
    const std::uint32_t x = (slot % columns_) * cell_width_;
    return x;
}

std::uint32_t ImageAtlas::cell_y(std::uint32_t slot) const noexcept {
    const std::uint32_t y = (slot / columns_) * cell_height_;
    return y;
}

void ImageAtlas::stage(std::byte* cell, std::uint64_t source_row_bytes, std::span<const std::byte> pixels) noexcept {
    const std::size_t gutter = layout_.gutter;
    const std::size_t slot_width = layout_.slot_width;
    const std::size_t slot_height = layout_.slot_height;
    const std::size_t cell_row_bytes = static_cast<std::size_t>(cell_width_) * bytes_per_pixel;
    const std::size_t slot_row_bytes = slot_width * bytes_per_pixel;
    // The slot's rows, each with its first and last pixel repeated sideways.
    for (std::size_t row = 0U; row < slot_height; ++row) {
        std::byte* const line = cell + (row + gutter) * cell_row_bytes;
        const std::byte* const source = pixels.data() + row * static_cast<std::size_t>(source_row_bytes);
        std::copy_n(source, slot_row_bytes, line + gutter * bytes_per_pixel);
        const std::byte* const left = line + gutter * bytes_per_pixel;
        const std::byte* const right = line + (gutter + slot_width - 1U) * bytes_per_pixel;
        for (std::size_t column = 0U; column < gutter; ++column) {
            std::copy_n(left, bytes_per_pixel, line + column * bytes_per_pixel);
            std::copy_n(right, bytes_per_pixel, line + (gutter + slot_width + column) * bytes_per_pixel);
        }
    }
    // The first and last of those rows repeated up and down.
    const std::byte* const top = cell + gutter * cell_row_bytes;
    const std::byte* const bottom = cell + (gutter + slot_height - 1U) * cell_row_bytes;
    for (std::size_t row = 0U; row < gutter; ++row) {
        std::copy_n(top, cell_row_bytes, cell + row * cell_row_bytes);
        std::copy_n(bottom, cell_row_bytes, cell + (gutter + slot_height + row) * cell_row_bytes);
    }
}

std::byte* ImageAtlas::pending_cell_pixels(std::uint32_t cell) noexcept {
    const std::size_t cell_bytes = staging_.size();
    std::byte* const pixels = pending_pixels_.data() + static_cast<std::size_t>(cell) * cell_bytes;
    return pixels;
}

bool ImageAtlas::write_cell(ApplicationResources& resources, std::uint32_t slot, const std::byte* cell) noexcept {
    const std::uint64_t cell_row_bytes = static_cast<std::uint64_t>(cell_width_) * bytes_per_pixel;
    const ImageLoadResult written = resources.registry().patch_bgra32_premultiplied_in_place(
        image_, cell_x(slot), cell_y(slot), cell_width_, cell_height_, cell_row_bytes,
        std::span<const std::byte>(cell, staging_.size()));
    const bool accepted = static_cast<bool>(written);
    return accepted;
}

bool ImageAtlas::defer(std::uint32_t slot, std::uint64_t source_row_bytes,
                       std::span<const std::byte> pixels) noexcept {
    SlotRecord& record = slots_[slot];
    if (record.pending_cell != no_slot) {
        stage(pending_cell_pixels(record.pending_cell), source_row_bytes, pixels);
        ++statistics_.deferred_replacements;
        return true;
    }
    if (pending_free_.empty()) return false;
    record.pending_cell = pending_free_.back();
    pending_free_.pop_back();
    // Reserved for the pending capacity, which bounds the slots waiting:
    // never allocates.
    pending_order_.push_back(slot);
    stage(pending_cell_pixels(record.pending_cell), source_row_bytes, pixels);
    ++statistics_.pending_now;
    return true;
}

void ImageAtlas::drop_pending(std::uint32_t slot) noexcept {
    SlotRecord& record = slots_[slot];
    if (record.pending_cell == no_slot) return;
    pending_free_.push_back(record.pending_cell);
    record.pending_cell = no_slot;
    Table<std::uint32_t>::iterator found = std::find(pending_order_.begin(), pending_order_.end(), slot);
    if (found != pending_order_.end()) pending_order_.erase(found);
    --statistics_.pending_now;
    ++statistics_.deferred_drops;
}

void ImageAtlas::apply_pending_fills() noexcept {
    if (pending_order_.empty()) return;
    const std::shared_ptr<ApplicationResources> resources = resources_.lock();
    if (!resources || (*resources).painting()) return;
    // Taken in deferral order; each slot leaves the order before its users
    // are damaged, so the order is consistent whatever a damage request does.
    std::size_t next = 0U;
    while (next < pending_order_.size()) {
        const std::uint32_t slot = pending_order_[next];
        ++next;
        SlotRecord& record = slots_[slot];
        const std::uint32_t cell = record.pending_cell;
        record.pending_cell = no_slot;
        pending_free_.push_back(cell);
        --statistics_.pending_now;
        if (!write_cell(*resources, slot, pending_cell_pixels(cell))) {
            ++statistics_.fill_refusals;
            continue;
        }
        record.state = SlotState::filled;
        ++statistics_.fills;
        try {
            damage_users(slot, false);
        } catch (...) {
            ++statistics_.damage_failures;
        }
    }
    pending_order_.clear();
}

void ImageAtlas::settle_pending(const ApplicationResources& resources) noexcept {
    if (pending_order_.empty() || resources.painting()) return;
    apply_pending_fills();
}

ImageAtlasAcquire ImageAtlas::acquire(std::uint64_t key) {
    const std::shared_ptr<ApplicationResources> resources = require_access("ImageAtlas acquire");
    settle_pending(*resources);
    ImageAtlasAcquire result{};
    const std::uint32_t found = find_slot_index(key);
    if (found != no_slot) {
        touch(found);
        ++statistics_.hits;
        result.slot = ImageAtlasSlot{found, slots_[found].generation};
        result.outcome = ImageAtlasAcquireOutcome::hit;
        return result;
    }
    if (!free_.empty()) {
        const std::uint32_t slot = free_.back();
        free_.pop_back();
        assign(slot, key);
        ++statistics_.assignments;
        result.slot = ImageAtlasSlot{slot, slots_[slot].generation};
        result.outcome = ImageAtlasAcquireOutcome::assigned;
        return result;
    }
    if (layout_.replacement == ImageAtlasReplacement::caller || oldest_ == no_slot) {
        ++statistics_.full_refusals;
        result.outcome = ImageAtlasAcquireOutcome::full;
        return result;
    }
    const std::uint32_t victim = oldest_;
    result.replaced_key = slots_[victim].key;
    retire(victim);
    assign(victim, key);
    ++statistics_.replacements;
    result.slot = ImageAtlasSlot{victim, slots_[victim].generation};
    result.outcome = ImageAtlasAcquireOutcome::replaced;
    // The atlas is consistent; the previous key's users repaint whole and
    // look their key up again.
    damage_users(victim, true);
    return result;
}

ImageAtlasSlot ImageAtlas::find(std::uint64_t key) const noexcept {
    const std::uint32_t found = find_slot_index(key);
    if (found == no_slot) return ImageAtlasSlot{};
    const ImageAtlasSlot slot{found, slots_[found].generation};
    return slot;
}

bool ImageAtlas::filled(ImageAtlasSlot slot) const noexcept {
    const bool ready = current(slot) && slots_[slot.index].state == SlotState::filled;
    return ready;
}

ImageAtlasError ImageAtlas::fill(ImageAtlasSlot slot, std::uint64_t source_row_bytes,
                                 std::span<const std::byte> pixels) {
    const std::shared_ptr<ApplicationResources> resources = require_access("ImageAtlas fill");
    settle_pending(*resources);
    if (!current(slot)) {
        ++statistics_.stale_rejections;
        ++statistics_.fill_refusals;
        return ImageAtlasError::stale_slot;
    }
    // The last row needs only its pixels; check the span before multiplying
    // the caller's stride.
    const std::uint64_t slot_row_bytes = static_cast<std::uint64_t>(layout_.slot_width) * bytes_per_pixel;
    if (source_row_bytes < slot_row_bytes || slot_row_bytes > pixels.size() ||
        static_cast<std::uint64_t>(layout_.slot_height - 1U) >
            (static_cast<std::uint64_t>(pixels.size()) - slot_row_bytes) / source_row_bytes) {
        ++statistics_.fill_refusals;
        return ImageAtlasError::invalid_pixels;
    }
    if ((*resources).painting()) {
        if (!defer(slot.index, source_row_bytes, pixels)) {
            ++statistics_.fill_refusals;
            return ImageAtlasError::painting;
        }
        ++statistics_.deferred_fills;
        return ImageAtlasError::deferred;
    }
    stage(staging_.data(), source_row_bytes, pixels);
    if (!write_cell(*resources, slot.index, staging_.data())) {
        ++statistics_.fill_refusals;
        return ImageAtlasError::registry_refused;
    }
    slots_[slot.index].state = SlotState::filled;
    ++statistics_.fills;
    damage_users(slot.index, false);
    return ImageAtlasError::none;
}

bool ImageAtlas::release(ImageAtlasSlot slot) {
    const std::shared_ptr<ApplicationResources> resources = require_access("ImageAtlas release");
    settle_pending(*resources);
    if (!current(slot)) {
        ++statistics_.stale_rejections;
        return false;
    }
    retire(slot.index);
    // free_ was reserved for every slot and holds fewer, so this never
    // allocates.
    free_.push_back(slot.index);
    ++statistics_.releases;
    damage_users(slot.index, true);
    return true;
}

void ImageAtlas::clear() {
    const std::shared_ptr<ApplicationResources> resources = require_access("ImageAtlas clear");
    settle_pending(*resources);
    for (std::uint32_t index = 0U; index < slots_.size(); ++index) {
        if (slots_[index].state == SlotState::free) continue;
        const ImageAtlasSlot slot{index, slots_[index].generation};
        static_cast<void>(release(slot));
    }
}

bool ImageAtlas::draw(Painter& painter, Control& user, ImageAtlasSlot slot, Rect destination, double opacity) {
    if (!current(slot)) {
        ++statistics_.stale_rejections;
        ++statistics_.draw_refusals;
        return false;
    }
    if (resources_.expired() || !destination.finite()) {
        ++statistics_.draw_refusals;
        return false;
    }
    if (!remember_user(slot.index, user, destination)) {
        ++statistics_.user_overflows;
        ++statistics_.draw_refusals;
        return false;
    }
    // An unfilled slot (given, its fill pending or not yet made) draws
    // nothing, but the user is remembered: the fill, immediate or deferred,
    // then repaints it where it would have drawn (A13).
    if (slots_[slot.index].state != SlotState::filled) {
        ++statistics_.unfilled_draws;
        ++statistics_.draw_refusals;
        return false;
    }
    const bool drawn = draw_unrecorded(painter, slot, destination, opacity);
    return drawn;
}

bool ImageAtlas::draw_unrecorded(Painter& painter, ImageAtlasSlot slot, Rect destination, double opacity) {
    if (!current(slot)) {
        ++statistics_.stale_rejections;
        ++statistics_.draw_refusals;
        return false;
    }
    if (slots_[slot.index].state != SlotState::filled || resources_.expired() || !destination.finite()) {
        ++statistics_.draw_refusals;
        return false;
    }
    touch(slot.index);
    const Rect source{static_cast<double>(cell_x(slot.index) + layout_.gutter),
                      static_cast<double>(cell_y(slot.index) + layout_.gutter),
                      static_cast<double>(layout_.slot_width), static_cast<double>(layout_.slot_height)};
    painter.draw_image_region(image_, source, destination, opacity);
    ++statistics_.draws;
    return true;
}

std::optional<Rect> ImageAtlas::source_rectangle(ImageAtlasSlot slot) const noexcept {
    if (!current(slot)) return std::nullopt;
    const Rect source{static_cast<double>(cell_x(slot.index) + layout_.gutter),
                      static_cast<double>(cell_y(slot.index) + layout_.gutter),
                      static_cast<double>(layout_.slot_width), static_cast<double>(layout_.slot_height)};
    return source;
}

ImageAtlasStatistics ImageAtlas::statistics() const noexcept {
    ImageAtlasStatistics result = statistics_;
    result.image_bytes = static_cast<std::uint64_t>(image_width_) * image_height_ * bytes_per_pixel;
    result.table_bytes = static_cast<std::uint64_t>(slots_.capacity()) * sizeof(SlotRecord) +
                         static_cast<std::uint64_t>(users_.capacity()) * sizeof(UserRecord) +
                         static_cast<std::uint64_t>(keys_.capacity()) * sizeof(KeyEntry) +
                         static_cast<std::uint64_t>(free_.capacity()) * sizeof(std::uint32_t) +
                         static_cast<std::uint64_t>(staging_.capacity()) +
                         static_cast<std::uint64_t>(pending_pixels_.capacity()) +
                         static_cast<std::uint64_t>(pending_free_.capacity()) * sizeof(std::uint32_t) +
                         static_cast<std::uint64_t>(pending_order_.capacity()) * sizeof(std::uint32_t);
    return result;
}

} // namespace gui_forms
