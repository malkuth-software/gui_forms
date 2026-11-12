// ImageAtlas (demonstrator gap A6): fixed-size slots of one backing image
// with generational handles, least-recently-used or caller replacement,
// edge-repeating gutters, an image whose ImageId survives every fill, damage
// limited to the controls that drew a changed slot, fills deferred during a
// paint and written when it ends, and no counted allocation after
// construction.
#include "gui_forms/allocation/statistics/allocation_statistics.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/resources/image_atlas/image_atlas.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

void expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "image atlas failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

// One draw_image_region call.
struct RegionDraw final {
    ImageId image{};
    Rect source{};
    Rect destination{};
};

// Records the image regions drawn; every other operation is ignored.
class RegionPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect, double) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_image_region(ImageId image, Rect source, Rect destination, double) override {
        regions.push_back(RegionDraw{image, source, destination});
    }
    std::vector<RegionDraw> regions{};
};

// Premultiplied BGRA pixels of one colour, with the top-left pixel `corner`
// and the bottom-right pixel `far_corner`, rows tightly packed.
std::vector<std::byte> slot_pixels(std::uint32_t width, std::uint32_t height, std::uint8_t fill, std::uint8_t corner,
                                   std::uint8_t far_corner) {
    std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4U, std::byte{fill});
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        pixels[channel] = std::byte{corner};
        pixels[pixels.size() - 4U + channel] = std::byte{far_corner};
    }
    return pixels;
}

ImageAtlasLayout layout_of(std::uint32_t side, std::uint32_t count, ImageAtlasReplacement replacement) {
    ImageAtlasLayout layout{};
    layout.slot_width = side;
    layout.slot_height = side;
    layout.slot_count = count;
    layout.gutter = 1U;
    layout.users_per_slot = 4U;
    layout.replacement = replacement;
    return layout;
}

ImageAtlasError fill_solid(ImageAtlas& atlas, ImageAtlasSlot slot, std::uint8_t value) {
    const std::uint32_t side = atlas.layout().slot_width;
    const std::vector<std::byte> pixels = slot_pixels(side, side, value, value, value);
    const ImageAtlasError error =
        atlas.fill(slot, static_cast<std::uint64_t>(side) * 4U, std::span<const std::byte>(pixels));
    return error;
}

// The byte at channel 0 of pixel (x, y) of the backing image.
std::uint8_t image_byte(const ApplicationResources& resources, const ImageAtlas& atlas, std::uint32_t x,
                        std::uint32_t y) {
    const std::optional<ImageResourceView> view = resources.images().find(atlas.image());
    expect(view.has_value(), "the backing image is in the registry");
    const std::size_t offset = static_cast<std::size_t>(y) * static_cast<std::size_t>((*view).row_bytes) +
                               static_cast<std::size_t>(x) * 4U;
    const std::uint8_t value = static_cast<std::uint8_t>((*view).encoded[offset]);
    return value;
}

// A slot is given, refused until filled, filled with its gutter repeating
// its edges, drawn from inside the gutter, and found again; the backing
// image keeps its ImageId through the fill.
void test_slot_lifecycle() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("atlas.lifecycle"));
    Window window(root, Size{200.0, 100.0});
    const std::shared_ptr<ApplicationResources> resources = window.resources();
    ImageAtlas atlas(resources, layout_of(4U, 3U, ImageAtlasReplacement::least_recently_used));
    expect(atlas.image_width() == 18U && atlas.image_height() == 6U, "three 6-pixel cells in one row");
    const ImageId image = atlas.image();

    const ImageAtlasAcquire first = atlas.acquire(10U);
    expect(first.outcome == ImageAtlasAcquireOutcome::assigned && first.slot.index == 0U &&
               first.slot.generation == 1U,
           "the first key is given slot 0 at generation 1");
    expect(!atlas.filled(first.slot), "a given slot is unfilled");
    RegionPainter painter;
    expect(!atlas.draw_unrecorded(painter, first.slot, Rect{0.0, 0.0, 8.0, 8.0}) && painter.regions.empty(),
           "an unfilled slot draws nothing");

    const std::vector<std::byte> pixels = slot_pixels(4U, 4U, 50U, 10U, 90U);
    expect(atlas.fill(first.slot, 16U, std::span<const std::byte>(pixels)) == ImageAtlasError::none,
           "the fill is accepted");
    expect(atlas.filled(first.slot), "the slot is filled");
    expect(atlas.image() == image && (*resources).images().find(image).has_value(),
           "the backing image keeps its ImageId through a fill");
    expect(image_byte(*resources, atlas, 0U, 0U) == 10U && image_byte(*resources, atlas, 1U, 1U) == 10U,
           "the gutter's corner repeats the slot's top-left pixel");
    expect(image_byte(*resources, atlas, 5U, 5U) == 90U && image_byte(*resources, atlas, 5U, 3U) == 50U,
           "the gutter repeats the slot's bottom-right pixel and right edge");

    expect(atlas.draw_unrecorded(painter, first.slot, Rect{2.0, 3.0, 8.0, 8.0}), "a filled slot draws");
    expect(painter.regions.size() == 1U && painter.regions[0].image == image &&
               painter.regions[0].source == (Rect{1.0, 1.0, 4.0, 4.0}) &&
               painter.regions[0].destination == (Rect{2.0, 3.0, 8.0, 8.0}),
           "the draw takes the slot inside its gutter");
    const std::optional<Rect> source = atlas.source_rectangle(first.slot);
    expect(source.has_value() && *source == (Rect{1.0, 1.0, 4.0, 4.0}), "the source rectangle is inside the gutter");

    const ImageAtlasAcquire again = atlas.acquire(10U);
    expect(again.outcome == ImageAtlasAcquireOutcome::hit && again.slot == first.slot, "the key is found");
    expect(atlas.find(10U) == first.slot && !atlas.find(11U), "find names the key's slot and no other");
    const ImageAtlasStatistics statistics = atlas.statistics();
    expect(statistics.assignments == 1U && statistics.hits == 1U && statistics.fills == 1U &&
               statistics.draws == 1U && statistics.draw_refusals == 1U && statistics.slots_in_use == 1U,
           "the statistics count the lifecycle");
    expect(statistics.image_bytes == 18U * 6U * 4U && statistics.table_bytes > 0U,
           "the statistics report the image and the tables");

    const std::vector<std::byte> short_pixels(15U, std::byte{0});
    expect(atlas.fill(first.slot, 16U, std::span<const std::byte>(short_pixels)) == ImageAtlasError::invalid_pixels,
           "too few pixels are refused");
    expect(atlas.fill(first.slot, 12U, std::span<const std::byte>(pixels)) == ImageAtlasError::invalid_pixels,
           "a stride shorter than a row is refused");
}

// Under least_recently_used a new key takes the least recently used slot;
// the previous key's handle is stale everywhere and a released slot comes
// back at a new generation.
void test_replacement_and_stale_handles() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("atlas.lru"));
    Window window(root, Size{200.0, 100.0});
    ImageAtlas atlas(window.resources(), layout_of(2U, 3U, ImageAtlasReplacement::least_recently_used));
    const ImageAtlasSlot one = atlas.acquire(1U).slot;
    const ImageAtlasSlot two = atlas.acquire(2U).slot;
    const ImageAtlasSlot three = atlas.acquire(3U).slot;
    expect(fill_solid(atlas, one, 1U) == ImageAtlasError::none && fill_solid(atlas, two, 2U) == ImageAtlasError::none &&
               fill_solid(atlas, three, 3U) == ImageAtlasError::none,
           "three keys fill three slots");
    RegionPainter painter;
    expect(atlas.draw_unrecorded(painter, one, Rect{0.0, 0.0, 2.0, 2.0}), "drawing key 1 uses it");

    const ImageAtlasAcquire four = atlas.acquire(4U);
    expect(four.outcome == ImageAtlasAcquireOutcome::replaced && four.replaced_key == 2U &&
               four.slot.index == two.index && four.slot.generation == two.generation + 1U,
           "key 4 takes key 2's slot, the least recently used, at the next generation");
    expect(!atlas.find(2U) && atlas.find(4U) == four.slot, "key 2 has no slot and key 4 has it");
    expect(fill_solid(atlas, two, 9U) == ImageAtlasError::stale_slot, "the stale handle cannot fill");
    expect(!atlas.draw_unrecorded(painter, two, Rect{0.0, 0.0, 2.0, 2.0}), "the stale handle cannot draw");
    expect(!atlas.release(two), "the stale handle cannot release");
    expect(!atlas.source_rectangle(two).has_value() && !atlas.filled(two), "the stale handle names nothing");
    expect(!atlas.filled(four.slot), "the replaced slot is unfilled for its new key");
    const ImageAtlasStatistics replaced = atlas.statistics();
    expect(replaced.replacements == 1U && replaced.assignments == 3U && replaced.stale_rejections == 3U &&
               replaced.slots_in_use == 3U,
           "the statistics count the replacement and the stale handles");

    expect(atlas.release(one), "a current handle releases");
    expect(!atlas.find(1U), "the released key has no slot");
    const ImageAtlasAcquire five = atlas.acquire(5U);
    expect(five.outcome == ImageAtlasAcquireOutcome::assigned && five.slot.index == one.index &&
               five.slot.generation == one.generation + 1U,
           "the released slot is given again at the next generation");
    expect(fill_solid(atlas, one, 7U) == ImageAtlasError::stale_slot, "the released handle stays stale");

    atlas.clear();
    expect(atlas.statistics().slots_in_use == 0U && !atlas.find(3U) && !atlas.find(4U) && !atlas.find(5U),
           "clear releases every slot");
}

// Under caller the atlas never replaces: a full atlas refuses, and the
// caller's release makes room.
void test_caller_replacement() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("atlas.caller"));
    Window window(root, Size{200.0, 100.0});
    ImageAtlas atlas(window.resources(), layout_of(2U, 2U, ImageAtlasReplacement::caller));
    const ImageAtlasSlot one = atlas.acquire(1U).slot;
    expect(static_cast<bool>(atlas.acquire(2U).slot), "two keys fill two slots");
    const ImageAtlasAcquire refused = atlas.acquire(3U);
    expect(refused.outcome == ImageAtlasAcquireOutcome::full && !refused.slot && atlas.find(1U) == one,
           "a full atlas refuses and keeps its keys");
    expect(atlas.statistics().full_refusals == 1U, "the refusal is counted");
    expect(atlas.release(one), "the caller releases a slot");
    const ImageAtlasAcquire three = atlas.acquire(3U);
    expect(three.outcome == ImageAtlasAcquireOutcome::assigned && three.slot.index == one.index,
           "the released slot is given to the waiting key");
}

// A fill repaints only where its users drew the slot, then forgets them; a
// release repaints its users whole; a user past users_per_slot is refused;
// an expired user is skipped.
void test_damage_scope() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("atlas.damage"));
    const std::shared_ptr<Panel> first = make_control<Panel>(StableId("atlas.damage.first"));
    const std::shared_ptr<Panel> second = make_control<Panel>(StableId("atlas.damage.second"));
    (*first).set_bounds(Rect{10.0, 20.0, 100.0, 50.0});
    (*second).set_bounds(Rect{10.0, 80.0, 100.0, 50.0});
    (*root).add_child(first);
    (*root).add_child(second);
    Window window(root, Size{200.0, 140.0});
    window.perform_layout();
    RegionPainter painter;
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 200.0, 140.0}));
    static_cast<void>(window.take_damage());

    ImageAtlas atlas(window.resources(), layout_of(8U, 2U, ImageAtlasReplacement::least_recently_used));
    const ImageAtlasSlot a = atlas.acquire(1U).slot;
    const ImageAtlasSlot b = atlas.acquire(2U).slot;
    expect(fill_solid(atlas, a, 1U) == ImageAtlasError::none && fill_solid(atlas, b, 2U) == ImageAtlasError::none,
           "both slots fill");
    expect(window.take_damage().empty(), "a fill with no users damages nothing");

    expect(atlas.draw(painter, *first, a, Rect{5.0, 5.0, 8.0, 8.0}), "the first panel draws slot a");
    expect(atlas.draw(painter, *second, b, Rect{5.0, 5.0, 8.0, 8.0}), "the second panel draws slot b");
    expect(window.take_damage().empty(), "drawing damages nothing");

    expect(fill_solid(atlas, a, 3U) == ImageAtlasError::none, "slot a is filled again");
    expect(has_dirty((*first).dirty(), Dirty::paint), "the user of slot a repaints");
    expect(!has_dirty((*second).dirty(), Dirty::paint), "the user of slot b does not");
    const DamageRegion filled = window.take_damage();
    expect(filled.bounds() == (Rect{15.0, 25.0, 8.0, 8.0}), "the damage is where the user drew the slot");
    expect(atlas.statistics().users_damaged == 1U, "one user was damaged");

    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 200.0, 140.0}));
    static_cast<void>(window.take_damage());
    expect(fill_solid(atlas, a, 4U) == ImageAtlasError::none && window.take_damage().empty(),
           "a user is forgotten after a change until it draws again");

    expect(atlas.release(b), "slot b is released");
    const DamageRegion released = window.take_damage();
    expect(released.bounds() == (Rect{10.0, 80.0, 100.0, 50.0}), "a release repaints its user whole");

    ImageAtlasLayout single = layout_of(4U, 1U, ImageAtlasReplacement::least_recently_used);
    single.users_per_slot = 1U;
    ImageAtlas narrow(window.resources(), single);
    const ImageAtlasSlot only = narrow.acquire(9U).slot;
    expect(fill_solid(narrow, only, 5U) == ImageAtlasError::none, "the narrow atlas fills");
    expect(narrow.draw(painter, *first, only, Rect{0.0, 0.0, 4.0, 4.0}), "the first user is remembered");
    expect(narrow.draw(painter, *first, only, Rect{20.0, 0.0, 4.0, 4.0}), "the same user drawing again is not new");
    expect(!narrow.draw(painter, *second, only, Rect{0.0, 0.0, 4.0, 4.0}), "a second user is refused");
    expect(narrow.statistics().user_overflows == 1U, "the overflow is counted");
    static_cast<void>(window.take_damage());
    expect(fill_solid(narrow, only, 6U) == ImageAtlasError::none, "the narrow slot is filled again");
    expect(window.take_damage().bounds() == (Rect{10.0, 20.0, 24.0, 4.0}),
           "the damage is the union of where the user drew the slot");

    std::shared_ptr<Panel> passing = make_control<Panel>(StableId("atlas.damage.passing"));
    expect(narrow.draw(painter, *passing, only, Rect{0.0, 0.0, 4.0, 4.0}), "a detached user is remembered");
    passing.reset();
    const std::uint64_t damaged = narrow.statistics().users_damaged;
    expect(fill_solid(narrow, only, 7U) == ImageAtlasError::none && narrow.statistics().users_damaged == damaged,
           "an expired user is skipped");
}

// What AtlasPanel does to its slot during its paint, after drawing it.
enum class PaintAction : std::uint8_t {
    // Fills it twice (values 8, then 9).
    fill_twice,
    // Fills it, then releases it.
    fill_then_release,
};

// A panel that draws a slot during its paint and fills it there.
class AtlasPanel final : public Panel {
public:
    explicit AtlasPanel(StableId id) : Panel(std::move(id)) {}

    void on_paint(Painter& painter, Rect local_damage) override {
        Panel::on_paint(painter, local_damage);
        if (atlas == nullptr) return;
        ImageAtlas& target = *atlas;
        atlas = nullptr;
        drawn = target.draw(painter, *this, slot, Rect{0.0, 0.0, 8.0, 8.0});
        first_fill = fill_solid(target, slot, 8U);
        if (action == PaintAction::fill_twice) {
            second_fill = fill_solid(target, slot, 9U);
        } else {
            released = target.release(slot);
        }
    }

    ImageAtlas* atlas{};
    ImageAtlasSlot slot{};
    PaintAction action{PaintAction::fill_twice};
    bool drawn{};
    bool released{};
    ImageAtlasError first_fill{ImageAtlasError::none};
    ImageAtlasError second_fill{ImageAtlasError::none};
};

// A fill during a paint is deferred, a second fill of the slot replaces the
// pending one, and the pending fill is written when the paint ends: the
// image holds the last pixels, and the panel, which drew the slot during
// the paint, is damaged where it drew it for the next frame.
void test_fill_deferred_during_paint() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("atlas.paint"));
    const std::shared_ptr<AtlasPanel> panel = std::make_shared<AtlasPanel>(StableId("atlas.paint.panel"));
    (*panel).set_bounds(Rect{30.0, 40.0, 50.0, 50.0});
    (*root).add_child(panel);
    Window window(root, Size{200.0, 140.0});
    const std::shared_ptr<ApplicationResources> resources = window.resources();
    ImageAtlas atlas(resources, layout_of(8U, 1U, ImageAtlasReplacement::least_recently_used));
    const ImageAtlasSlot slot = atlas.acquire(1U).slot;
    expect(fill_solid(atlas, slot, 1U) == ImageAtlasError::none, "the slot fills outside a paint");
    (*panel).atlas = &atlas;
    (*panel).slot = slot;
    window.perform_layout();
    RegionPainter painter;
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 200.0, 140.0}));
    expect((*panel).drawn, "the panel drew the slot during its paint");
    expect((*panel).first_fill == ImageAtlasError::deferred && (*panel).second_fill == ImageAtlasError::deferred,
           "fills during the paint are deferred");
    const ImageAtlasStatistics statistics = atlas.statistics();
    expect(statistics.deferred_fills == 2U && statistics.deferred_replacements == 1U && statistics.pending_now == 0U &&
               statistics.fills == 2U,
           "the second fill replaced the pending one, which was written when the paint ended");
    expect(image_byte(*resources, atlas, 4U, 4U) == 9U, "the image holds the last fill's pixels");
    expect(window.take_damage().bounds() == (Rect{30.0, 40.0, 8.0, 8.0}),
           "the panel is damaged where it drew the slot");
}

// Releasing a slot with a pending fill drops the fill; with no pending
// cells a fill during a paint is refused.
void test_pending_fill_dropped_or_refused() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("atlas.drop"));
    const std::shared_ptr<AtlasPanel> panel = std::make_shared<AtlasPanel>(StableId("atlas.drop.panel"));
    (*panel).set_bounds(Rect{0.0, 0.0, 50.0, 50.0});
    (*root).add_child(panel);
    Window window(root, Size{200.0, 140.0});
    const std::shared_ptr<ApplicationResources> resources = window.resources();
    ImageAtlas atlas(resources, layout_of(8U, 2U, ImageAtlasReplacement::least_recently_used));
    const ImageAtlasSlot slot = atlas.acquire(1U).slot;
    expect(fill_solid(atlas, slot, 1U) == ImageAtlasError::none, "the slot fills outside a paint");
    (*panel).atlas = &atlas;
    (*panel).slot = slot;
    (*panel).action = PaintAction::fill_then_release;
    window.perform_layout();
    RegionPainter painter;
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 200.0, 140.0}));
    expect((*panel).first_fill == ImageAtlasError::deferred && (*panel).released,
           "the fill was deferred and the slot released during the paint");
    const ImageAtlasStatistics dropped = atlas.statistics();
    expect(dropped.deferred_drops == 1U && dropped.pending_now == 0U && dropped.fills == 1U,
           "the release dropped the pending fill");
    expect(image_byte(*resources, atlas, 4U, 4U) == 1U, "the dropped fill never reached the image");

    ImageAtlasLayout none = layout_of(8U, 1U, ImageAtlasReplacement::least_recently_used);
    none.pending_fills = 0U;
    ImageAtlas refusing(resources, none);
    const ImageAtlasSlot only = refusing.acquire(2U).slot;
    (*panel).atlas = &refusing;
    (*panel).slot = only;
    (*panel).action = PaintAction::fill_twice;
    (*panel).invalidate(Dirty::paint);
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 200.0, 140.0}));
    expect((*panel).first_fill == ImageAtlasError::painting, "with no pending cells a fill during a paint is refused");
    expect(!refusing.filled(only), "the refused fill changed nothing");
}

// After construction, acquire, fill, draw and release allocate nothing on
// the counted paths; the constructor's allocations are counted under
// image_atlas and the backing image under image_resources.
void test_no_counted_allocation_after_construction() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("atlas.allocation"));
    Window window(root, Size{200.0, 100.0});
    const AllocationScope construction;
    ImageAtlas atlas(window.resources(), layout_of(4U, 8U, ImageAtlasReplacement::least_recently_used));
    const AllocationStatistics made = construction.counted();
    const AllocationCounts tables = made.of(AllocationSubsystem::image_atlas);
    expect(tables.allocations == 9U && tables.releases == 1U,
           "eight tables and the zeroed image buffer, which is released");
    const AllocationCounts image = made.of(AllocationSubsystem::image_resources);
    expect(image.allocations == 1U && image.allocated_bytes == atlas.statistics().image_bytes,
           "the registry holds the backing image's pixels once");

    RegionPainter painter;
    const AllocationScope steady;
    for (std::uint64_t key = 0U; key < 64U; ++key) {
        const ImageAtlasAcquire acquired = atlas.acquire(key);
        expect(fill_solid(atlas, acquired.slot, static_cast<std::uint8_t>(key)) == ImageAtlasError::none,
               "each key fills");
        expect(atlas.draw(painter, *root, acquired.slot, Rect{0.0, 0.0, 4.0, 4.0}), "each key draws");
        if (key % 3U == 0U) expect(atlas.release(acquired.slot), "every third key is released");
    }
    const AllocationStatistics after = steady.counted();
    expect(after.of(AllocationSubsystem::image_atlas).allocations == 0U &&
               after.of(AllocationSubsystem::image_resources).allocations == 0U,
           "the atlas and its image allocate nothing after construction (damage lists are the window's)");
}

// Runs acquire on another thread and records whether it was refused (a
// named function object for std::thread; the house style admits no
// lambdas). The thread is joined before `refused` is read.
struct ThreadProbe final {
    ImageAtlas* atlas{};
    bool* refused{};

    void operator()() const {
        try {
            static_cast<void>((*atlas).acquire(5U));
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

// Layouts out of range or too large for the registry are refused; an atlas
// outliving its resources refuses mutations and draws nothing; another
// thread is refused.
void test_refusals() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("atlas.refusals"));
    std::unique_ptr<ImageAtlas> orphan{};
    {
        Window window(root, Size{200.0, 100.0});
        const std::shared_ptr<ApplicationResources> resources = window.resources();
        ImageAtlasLayout zero = layout_of(0U, 1U, ImageAtlasReplacement::least_recently_used);
        bool refused = false;
        try {
            ImageAtlas atlas(resources, zero);
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        expect(refused, "a zero slot side is refused");
        ImageAtlasLayout huge = layout_of(1024U, 65536U, ImageAtlasReplacement::least_recently_used);
        refused = false;
        try {
            ImageAtlas atlas(resources, huge);
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        expect(refused, "a grid beyond the registry's limits is refused");
        ImageAtlasLayout wide_gutter = layout_of(4U, 1U, ImageAtlasReplacement::least_recently_used);
        wide_gutter.gutter = 9U;
        refused = false;
        try {
            ImageAtlas atlas(resources, wide_gutter);
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        expect(refused, "a gutter over 8 pixels is refused");

        ImageAtlas atlas(resources, layout_of(4U, 2U, ImageAtlasReplacement::least_recently_used));
        bool wrong_thread = false;
        std::thread other(ThreadProbe{&atlas, &wrong_thread});
        other.join();
        expect(wrong_thread, "another thread is refused");

        orphan = std::make_unique<ImageAtlas>(resources, layout_of(4U, 2U, ImageAtlasReplacement::least_recently_used));
        const ImageAtlasSlot slot = (*orphan).acquire(1U).slot;
        expect(fill_solid(*orphan, slot, 1U) == ImageAtlasError::none, "the orphan-to-be fills");
    }
    bool gone = false;
    try {
        static_cast<void>((*orphan).acquire(2U));
    } catch (const std::logic_error&) {
        gone = true;
    }
    expect(gone, "an atlas outliving its resources refuses mutations");
    RegionPainter painter;
    expect(!(*orphan).draw_unrecorded(painter, (*orphan).find(1U), Rect{0.0, 0.0, 4.0, 4.0}),
           "an atlas outliving its resources draws nothing");
    orphan.reset();
}

} // namespace

int main() {
    test_slot_lifecycle();
    test_replacement_and_stale_handles();
    test_caller_replacement();
    test_damage_scope();
    test_fill_deferred_during_paint();
    test_pending_fill_dropped_or_refused();
    test_no_counted_allocation_after_construction();
    test_refusals();
    std::cout << "image atlas tests passed\n";
    return EXIT_SUCCESS;
}
