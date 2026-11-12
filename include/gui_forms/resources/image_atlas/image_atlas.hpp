#pragma once

// An image atlas (demonstrator gap A6): many small images of one size kept
// in fixed-size slots of one backing image, as avatars, thumbnails or icons
// generated at run time are, with generational slot handles, a stated
// replacement policy, a draw call and damage limited to the controls that
// drew a changed slot.
//
// Layout: the slots are fixed-size cells in a grid, filled row by row; each
// cell is the slot grown by `gutter` pixels on every side, and a fill repeats
// the slot's edge pixels into its gutter, so a slot drawn scaled with linear
// filtering samples only its own colours. There is no shelf packing: every
// slot has the layout's size.
//
// Ownership: the atlas owns its backing image in the ApplicationResources it
// was made with (so the image is valid in every window sharing them) and
// removes it when destroyed. It observes the resources (a weak reference):
// after they are gone, mutations throw std::logic_error, draws draw nothing
// and the destructor leaves nothing to remove. It observes the controls that
// drew a slot (weak references) and never keeps one alive.
//
// Memory: every table, the staging cell and the key index are allocated by
// the constructor, under AllocationSubsystem::image_atlas
// (allocation_statistics.hpp); acquire, fill, release, find and draw
// allocate nothing of the atlas's own. A fill writes the backing image in
// place (ImageRegistry::patch_bgra32_premultiplied_in_place), so the
// image's ImageId never changes and display lists that recorded it stay
// valid; damaging a user may grow the window's damage list (Control::
// invalidate).
//
// Threads: one, the resources' UI thread (ApplicationResources::
// check_access). Mutations check it and throw std::logic_error elsewhere;
// the const reads and draw do not check and must not be called elsewhere.
//
// Order and reentrancy: fill is refused (painting) while any window sharing
// the resources is painting, as a paint reads the image; fill a slot during
// layout or input handling, as a row is measured, and draw it during paint.
// Damage is requested through Control::invalidate on each user, in the order
// they drew; no callback runs.

#include "gui_forms/allocation/counted_allocator/counted_allocator.hpp"
#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace gui_forms {

class ApplicationResources;
class Control;
class Painter;

// A slot's handle: its index and the generation the slot had when the
// handle was given. The slot's generation advances when it is released or
// replaced, so an older handle is stale and every call rejects it.
// Generation 0 is no slot. A generation is 32 bits: a handle kept across
// 2^32 reuses of its slot would be taken for current.
struct GUI_FORMS_API ImageAtlasSlot final {
    std::uint32_t index{};
    std::uint32_t generation{};

    [[nodiscard]] explicit operator bool() const noexcept { return generation != 0U; }
    friend constexpr bool operator==(const ImageAtlasSlot& left, const ImageAtlasSlot& right) noexcept {
        const bool same = left.index == right.index && left.generation == right.generation;
        return same;
    }
};

// Who chooses the slot a new key replaces when every slot is taken.
enum class ImageAtlasReplacement : std::uint8_t {
    // The atlas: acquire replaces the least recently used key, by the
    // atlas's own record of use (acquire hits and draws). The record is the
    // atlas's; the caller reads none of it.
    least_recently_used,
    // The caller: acquire answers full and replaces nothing; the caller
    // releases a slot by its own policy and acquires again.
    caller,
};

struct GUI_FORMS_API ImageAtlasLayout final {
    // A slot's size in image pixels, each 1 to 1024.
    std::uint32_t slot_width{};
    std::uint32_t slot_height{};
    // Slots in the atlas, 1 to 65536; the grid must fit the registry's
    // image limits.
    std::uint32_t slot_count{};
    // Pixels of edge repeated around each slot, 0 to 8.
    std::uint32_t gutter{1U};
    // Controls remembered per slot for damage, 1 to 64. A further control
    // drawing the slot is refused (draw returns false and counts
    // user_overflows), so a later change never leaves its pixels stale.
    std::uint32_t users_per_slot{4U};
    ImageAtlasReplacement replacement{ImageAtlasReplacement::least_recently_used};
};

enum class ImageAtlasAcquireOutcome : std::uint8_t {
    // The key has a slot; its pixels are as last filled (or unfilled).
    hit,
    // A free slot was given to the key; fill it before it draws.
    assigned,
    // The least recently used key's slot was given to the key; that key's
    // handles are stale and its users repaint. Fill it before it draws.
    replaced,
    // Every slot is taken and the policy is caller: nothing changed.
    full,
};

struct GUI_FORMS_API ImageAtlasAcquire final {
    ImageAtlasSlot slot{};
    ImageAtlasAcquireOutcome outcome{ImageAtlasAcquireOutcome::full};
    // For replaced: the key whose slot was taken.
    std::uint64_t replaced_key{};
};

enum class ImageAtlasError : std::uint8_t {
    none,
    // The handle is not the slot's current one.
    stale_slot,
    // A window sharing the resources is painting.
    painting,
    // The pixels are not slot_width x slot_height premultiplied BGRA rows
    // `source_row_bytes` apart.
    invalid_pixels,
    // The registry refused the write (the image was removed behind the
    // atlas's back).
    registry_refused,
};

struct GUI_FORMS_API ImageAtlasStatistics final {
    // acquire: keys found, keys given a free slot, keys given a replaced
    // slot, and refusals when full under the caller policy.
    std::uint64_t hits{};
    std::uint64_t assignments{};
    std::uint64_t replacements{};
    std::uint64_t full_refusals{};
    std::uint64_t releases{};
    std::uint64_t fills{};
    std::uint64_t fill_refusals{};
    // Calls refused for a stale handle (fill, release, draw).
    std::uint64_t stale_rejections{};
    std::uint64_t draws{};
    // Draws refused: a stale or unfilled slot, gone resources, or a user
    // over users_per_slot (also counted in user_overflows).
    std::uint64_t draw_refusals{};
    std::uint64_t user_overflows{};
    // Controls asked to repaint because a slot they drew changed.
    std::uint64_t users_damaged{};
    std::uint32_t slots_in_use{};
    // The backing image's pixel bytes (held by the registry) and the atlas's
    // own tables and staging cell.
    std::uint64_t image_bytes{};
    std::uint64_t table_bytes{};
};

class GUI_FORMS_API ImageAtlas final {
public:
    // Makes the backing image in `resources`, transparent. On the resources'
    // UI thread. std::invalid_argument for null resources or a layout out of
    // range or too large for the registry's limits, std::logic_error on
    // another thread, std::runtime_error when the registry refuses the image
    // (its quota); nothing is left behind then.
    ImageAtlas(const std::shared_ptr<ApplicationResources>& resources, ImageAtlasLayout layout);
    ~ImageAtlas();
    ImageAtlas(const ImageAtlas&) = delete;
    ImageAtlas& operator=(const ImageAtlas&) = delete;
    ImageAtlas(ImageAtlas&&) = delete;
    ImageAtlas& operator=(ImageAtlas&&) = delete;

    [[nodiscard]] const ImageAtlasLayout& layout() const noexcept { return layout_; }
    // The backing image; the same ImageId for the atlas's whole life.
    [[nodiscard]] ImageId image() const noexcept { return image_; }
    [[nodiscard]] std::uint32_t image_width() const noexcept { return image_width_; }
    [[nodiscard]] std::uint32_t image_height() const noexcept { return image_height_; }

    // The slot of `key`: found (hit), a free one (assigned), the least
    // recently used one (replaced, under least_recently_used) or none
    // (full, under caller). A hit counts as a use. A replaced slot's
    // previous key loses it: its handles go stale and the controls that
    // drew it are asked to repaint whole (Control::invalidate(Dirty::paint)),
    // so they look their key up again.
    [[nodiscard]] ImageAtlasAcquire acquire(std::uint64_t key);
    // The current handle of `key`'s slot, or no slot. Not a use.
    [[nodiscard]] ImageAtlasSlot find(std::uint64_t key) const noexcept;
    // Whether `slot` is current and has been filled since it was given.
    [[nodiscard]] bool filled(ImageAtlasSlot slot) const noexcept;
    // Writes `pixels` (slot_width x slot_height premultiplied BGRA, rows
    // `source_row_bytes` apart; the last row needs only its pixels) into
    // `slot` and its gutter. The controls that drew the slot are asked to
    // repaint where they drew it (Control::invalidate(Rect)) and forgotten
    // until they draw it again; no other control repaints. A refusal
    // changes nothing. std::logic_error on another thread or after the
    // resources are gone.
    ImageAtlasError fill(ImageAtlasSlot slot, std::uint64_t source_row_bytes, std::span<const std::byte> pixels);
    // Frees `slot` for another key: its handles go stale and its users
    // repaint whole. False, changing nothing, for a stale handle.
    // std::logic_error on another thread or after the resources are gone.
    bool release(ImageAtlasSlot slot);
    // Releases every slot.
    void clear();

    // Draws `slot` over `destination` (the user's client coordinates, as
    // during its on_paint) and remembers `user` and the destination, so that
    // a fill repaints that part of `user` and a release or replacement
    // repaints `user` whole. A use of the slot. False, drawing nothing, for
    // a stale or unfilled slot, gone resources, or a user that would exceed
    // users_per_slot: the caller draws its own fallback then. The
    // destination is remembered as the union of where `user` drew the slot
    // since the slot last changed.
    bool draw(Painter& painter, Control& user, ImageAtlasSlot slot, Rect destination, double opacity = 1.0);
    // As draw, remembering no user: a change to the slot repaints nothing
    // drawn this way. For a painter that is not a control's paint (a
    // capture); the caller repaints.
    bool draw_unrecorded(Painter& painter, ImageAtlasSlot slot, Rect destination, double opacity = 1.0);
    // The slot's rectangle in the backing image, inside its gutter; none for
    // a stale handle.
    [[nodiscard]] std::optional<Rect> source_rectangle(ImageAtlasSlot slot) const noexcept;

    [[nodiscard]] ImageAtlasStatistics statistics() const noexcept;

private:
    static constexpr std::uint32_t no_slot = 0xffffffffU;

    enum class SlotState : std::uint8_t { free, assigned, filled };

    struct SlotRecord final {
        std::uint64_t key{};
        std::uint32_t generation{1U};
        // Neighbours in the use order (most recent first); no_slot at the
        // ends and while the slot is free.
        std::uint32_t newer{no_slot};
        std::uint32_t older{no_slot};
        std::uint32_t user_count{};
        SlotState state{SlotState::free};
    };

    // One remembered user of a slot: users_per_slot entries per slot,
    // the first user_count of them live.
    struct UserRecord final {
        std::weak_ptr<Control> control{};
        Rect area{};
    };

    // The key index: open addressing with linear probing, a power-of-two
    // capacity at least twice the slot count; slot_plus_one 0 is empty.
    struct KeyEntry final {
        std::uint64_t key{};
        std::uint32_t slot_plus_one{};
    };

    template <typename Value>
    using Table = std::vector<Value, detail::CountedAllocator<Value, AllocationSubsystem::image_atlas>>;

    // Throws std::logic_error unless the resources live and this is their
    // thread; returns them.
    [[nodiscard]] std::shared_ptr<ApplicationResources> require_access(const char* operation) const;
    [[nodiscard]] bool current(ImageAtlasSlot slot) const noexcept;
    [[nodiscard]] std::uint32_t key_position(std::uint64_t key) const noexcept;
    [[nodiscard]] std::uint32_t find_slot_index(std::uint64_t key) const noexcept;
    // Never allocates: the index holds at most slot_count of its capacity.
    void insert_key(std::uint64_t key, std::uint32_t slot) noexcept;
    void erase_key(std::uint64_t key) noexcept;
    // The use order: touch moves a keyed slot to the front, link_front puts
    // an unlinked one there, unlink takes one out.
    void touch(std::uint32_t slot) noexcept;
    void link_front(std::uint32_t slot) noexcept;
    void unlink(std::uint32_t slot) noexcept;
    // Gives the free (or just retired) `slot` to `key`: indexed, at the front
    // of the use order, assigned and unfilled.
    void assign(std::uint32_t slot, std::uint64_t key) noexcept;
    // Retires the slot's key: removes it from the index and the use order,
    // advances its generation and marks it free. Its users are not damaged
    // here; the caller damages them once the atlas is consistent.
    void retire(std::uint32_t slot) noexcept;
    // Asks the slot's users to repaint, whole or at their areas, and forgets
    // them.
    void damage_users(std::uint32_t slot, bool whole);
    [[nodiscard]] bool remember_user(std::uint32_t slot, Control& user, Rect destination) noexcept;
    // The slot's cell's top-left pixel in the backing image.
    [[nodiscard]] std::uint32_t cell_x(std::uint32_t slot) const noexcept;
    [[nodiscard]] std::uint32_t cell_y(std::uint32_t slot) const noexcept;
    // Writes the caller's pixels into staging_'s centre and repeats the edges
    // into its gutter.
    void stage(std::uint64_t source_row_bytes, std::span<const std::byte> pixels) noexcept;

    std::weak_ptr<ApplicationResources> resources_{};
    ImageAtlasLayout layout_{};
    ImageId image_{};
    std::uint32_t image_width_{};
    std::uint32_t image_height_{};
    std::uint32_t columns_{};
    std::uint32_t cell_width_{};
    std::uint32_t cell_height_{};
    // Most and least recently used slots holding a key; no_slot when none.
    std::uint32_t newest_{no_slot};
    std::uint32_t oldest_{no_slot};
    Table<SlotRecord> slots_{};
    // slot_count * users_per_slot records.
    Table<UserRecord> users_{};
    Table<KeyEntry> keys_{};
    // Free slots, popped from the back; capacity slot_count.
    Table<std::uint32_t> free_{};
    // One cell of premultiplied BGRA, cell_width * cell_height * 4 bytes.
    Table<std::byte> staging_{};
    ImageAtlasStatistics statistics_{};
};

} // namespace gui_forms
