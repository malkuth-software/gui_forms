#include "gui_forms/components/business_card/business_card.hpp"

#include "card_turn.hpp"

#include "gui_forms/controls/modal_layer.hpp"
#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

// -- the process identity and the remark source ---------------------------------

// The identity and the remark source: written and read on the UI thread
// (the header's contract), so they need no lock.
ProgramIdentity& identity_storage() noexcept {
    static ProgramIdentity identity{};
    return identity;
}

BusinessCardRemarkSource& remark_source_storage() noexcept {
    static BusinessCardRemarkSource source{};
    return source;
}

constexpr std::string_view https_prefix = "https://";

void require_utf8(std::string_view text, std::string_view field) {
    const Utf8ValidationResult checked = validate_utf8(text);
    if (checked.valid()) return;
    std::string message = "ProgramIdentity ";
    message += field;
    message += " is not valid UTF-8";
    throw std::invalid_argument(message);
}

// The leading bytes of `text` that fit in `capacity` and end on a whole
// UTF-8 sequence (a continuation byte is 10xxxxxx).
std::size_t whole_prefix(std::string_view text, std::size_t capacity) noexcept {
    if (text.size() <= capacity) return text.size();
    std::size_t length = capacity;
    while (length > 0U && (static_cast<unsigned char>(text[length]) & 0xC0U) == 0x80U) --length;
    return length;
}

// -- the card's geometry ------------------------------------------------------------

// The card's stock in logical units at text scale 1: a business card's
// 3.5 by 2 proportion, smaller on the page than in the hand.
constexpr Size page_card_size{252.0, 144.0};
constexpr Size hand_card_size{420.0, 240.0};
// The margin the control keeps around the stock for the shadow and the
// tilt, as a share of the card's height.
constexpr double margin_share = 0.07;
// The tilt's reach as a share of the card's height: the face leans this
// far toward the pointer at the card's edge (hand); on the page the card
// rises by the page share under the pointer.
constexpr double hand_tilt_share = 0.02;
constexpr double page_rise_share = 0.012;
// The shadow's resting drop and its lean against the tilt, as shares.
constexpr double shadow_drop_share = 0.02;
constexpr double shadow_lean = 0.5;

// The turn (hand). Angles in radians; the springs on the window's motion
// clock respond in turn_response, the turn overshooting its rest a
// little. A turn within turn_rest_tolerance of a half turn, and level, is
// at rest. The eye stands eye_distance_share card widths from the card.
constexpr double pi = 3.14159265358979323846;
constexpr FrameInterval turn_response = std::chrono::milliseconds(260);
constexpr double turn_overshoot = 0.04;
constexpr double turn_rest_tolerance = 1e-4;
constexpr double eye_distance_share = 6.0;
// How far (logical units) the pointer moves before a press becomes a
// drag, and how long the drag's spin carries the card on when let go.
constexpr double drag_threshold = 3.0;
constexpr double fling_carry_seconds = 0.25;
// The heading band rule clips only where the band reaches this far above
// the card's client, at least (logical units).
constexpr double heading_band_slack = 0.0;

// The remarks: the text size (logical units, at text scale 1); the window
// margin they keep; the room they keep from the card and from each other;
// the first one's delay after the pointer arrives, the gap between them
// (the shortest, and a random spread on it), and how long one stays
// (seconds, plus a little a byte, at most the longest); how long it rises
// and fades; the tick; and the placements tried before one is dropped.
constexpr double remark_font_size = 15.0;
constexpr double remark_margin = 12.0;
constexpr double remark_card_clearance = 6.0;
constexpr double remark_spacing = 8.0;
constexpr double first_remark_seconds = 0.45;
constexpr double remark_gap_seconds = 1.3;
constexpr double remark_gap_spread_seconds = 1.2;
constexpr double remark_life_seconds = 2.4;
constexpr double remark_life_per_byte_seconds = 0.045;
constexpr double remark_longest_life_seconds = 9.0;
constexpr double remark_rise_seconds = 0.3;
constexpr double remark_fade_seconds = 0.6;
constexpr FrameInterval remark_frame_interval = std::chrono::milliseconds(33);
constexpr std::uint32_t remark_placement_attempts = 60U;

constexpr std::array<PartField<BusinessCard::Parts>, 11> business_card_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::region},
    {.declaration = {.name = "shadow", .role = PartRole::shadow, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::shadow},
    {.declaration = {.name = "stock", .role = PartRole::face, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::stock},
    {.declaration = {.name = "vendor_name", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::vendor_name},
    {.declaration = {.name = "rule", .role = PartRole::divider, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::rule},
    {.declaration = {.name = "product", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::product},
    {.declaration = {.name = "website", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::website},
    {.declaration = {.name = "copyright", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::copyright},
    {.declaration = {.name = "remark", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::remark},
    {.declaration = {.name = "close_mark", .role = PartRole::glyph, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::close_mark},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::focus},
}};

constexpr std::array<PartDeclaration, 11> business_card_parts = part_declarations(business_card_part_fields);

constexpr std::array<AnatomyState, 7> business_card_states{
    AnatomyState::normal,  AnatomyState::hot,     AnatomyState::pressed,       AnatomyState::enabled,
    AnatomyState::disabled, AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ControlAnatomy business_card_anatomy{
    .control_id = "business_card",
    .parts = business_card_parts,
    .states = business_card_states,
    .custom_states = {},
    .item_systems = {},
};

Rect offset_rect(Rect rect, Point by) noexcept {
    const Rect moved{rect.x + by.x, rect.y + by.y, rect.width, rect.height};
    return moved;
}

Rect inflated(Rect rect, double by) noexcept {
    const Rect grown{rect.x - by, rect.y - by, rect.width + by * 2.0, rect.height + by * 2.0};
    return grown;
}

bool overlaps(Rect left, Rect right) noexcept {
    const bool apart = left.right() <= right.x || right.right() <= left.x || left.bottom() <= right.y ||
                       right.bottom() <= left.y;
    return !apart;
}

// The whole number of half turns nearest `turn`.
double nearest_rest(double turn) noexcept {
    const double rest = std::round(turn / pi) * pi;
    return rest;
}

// The card's pose for the turn kernel: `card` at rest, turned so.
detail::CardTurnPose turn_pose(Rect card, double turn, double pitch) noexcept {
    detail::CardTurnPose pose{};
    pose.yaw = turn;
    pose.pitch = std::clamp(pitch, -maximum_business_card_pitch, maximum_business_card_pitch);
    pose.card = card;
    pose.eye = std::max(card.width, card.height) * eye_distance_share;
    return pose;
}

FrameInterval seconds_interval(double seconds) noexcept {
    const std::chrono::duration<double> span{seconds};
    const FrameInterval interval = std::chrono::duration_cast<FrameInterval>(span);
    return interval;
}

// A xorshift step, and a value in [0, 1) from it.
std::uint32_t next_random(std::uint32_t& state) noexcept {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

double unit_random(std::uint32_t& state) noexcept {
    const std::uint32_t drawn = next_random(state);
    const double unit = static_cast<double>(drawn & 0xFFFFFFU) / 16777216.0;
    return unit;
}

double clamp_unit(double value) noexcept {
    if (!std::isfinite(value)) return 0.0;
    const double clamped = std::clamp(value, -1.0, 1.0);
    return clamped;
}

// "<product> <version>", either alone, or empty.
std::string product_line(const ProgramIdentity& identity) {
    std::string line = identity.product;
    if (!identity.version.empty()) {
        if (!line.empty()) line += ' ';
        line += identity.version;
    }
    return line;
}

// The website as printed: the address without its scheme.
std::string_view printed_website(const ProgramIdentity& identity) noexcept {
    std::string_view address = identity.website;
    if (address.substr(0U, https_prefix.size()) == https_prefix) address.remove_prefix(https_prefix.size());
    return address;
}

// -- the cards shown in the hand ------------------------------------------------------

// One window's card in the hand: the layer and the card, kept while the
// window lives so a card put away can be shown again. `thread` is the UI
// thread that made them; only it touches them. `window` identifies the
// window and is compared, never followed, once the layer is not shown (a
// destroyed window revokes its overlay, which closes the layer).
struct ShownCard final {
    const Window* window{nullptr};
    std::thread::id thread{};
    std::shared_ptr<ModalLayer> layer{};
    std::shared_ptr<BusinessCard> card{};
};

// The records and their lock. The lock guards the vector only; it is never
// held while a control runs (show, close or an event), since a handler may
// call back in.
struct ShownCards final {
    std::mutex lock{};
    std::vector<ShownCard> records{};
};

ShownCards& shown_cards() noexcept {
    static ShownCards cards{};
    return cards;
}

// The card handlers running on this thread (the card's own input and
// semantic handlers, and the emits of hide_business_card): while any runs,
// no record is released, so a card never outlives itself mid-call.
// Trivially destructible thread storage.
thread_local std::uint32_t card_handlers_running = 0U;

class CardHandlerScope final {
public:
    CardHandlerScope() noexcept { ++card_handlers_running; }
    ~CardHandlerScope() { --card_handlers_running; }
    CardHandlerScope(const CardHandlerScope&) = delete;
    CardHandlerScope& operator=(const CardHandlerScope&) = delete;
};

// The index of `window`'s record made on this thread, or records.size().
std::size_t find_record(const std::vector<ShownCard>& records, const Window* window) noexcept {
    const std::thread::id self = std::this_thread::get_id();
    for (std::size_t index = 0U; index < records.size(); ++index) {
        if (records[index].window == window && records[index].thread == self) return index;
    }
    return records.size();
}

// Moves this thread's records whose layer is not shown, other than
// `keep`'s, into `released`, which the caller destroys after the lock.
// Nothing moves while a card handler runs on this thread.
void sweep_records(std::vector<ShownCard>& records, const Window* keep, std::vector<ShownCard>& released) {
    if (card_handlers_running != 0U) return;
    const std::thread::id self = std::this_thread::get_id();
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < records.size(); ++index) {
        ShownCard& record = records[index];
        const bool mine = record.thread == self && record.window != keep;
        const bool idle = record.layer == nullptr || !(*record.layer).is_shown();
        if (mine && idle) {
            released.push_back(std::move(record));
            continue;
        }
        if (kept != index) records[kept] = std::move(record);
        ++kept;
    }
    records.erase(records.begin() + static_cast<std::ptrdiff_t>(kept), records.end());
}

// The window's record's layer and card (copies of the pointers, so the
// caller holds them while they run), or empty.
ShownCard copy_record(const Window* window) {
    ShownCards& cards = shown_cards();
    const std::lock_guard<std::mutex> held(cards.lock);
    const std::size_t index = find_record(cards.records, window);
    if (index == cards.records.size()) return ShownCard{};
    ShownCard copy{};
    copy.window = cards.records[index].window;
    copy.thread = cards.records[index].thread;
    copy.layer = cards.records[index].layer;
    copy.card = cards.records[index].card;
    return copy;
}

} // namespace

// -- identity, remark source and the overlay switch ---------------------------------

void set_program_identity(const ProgramIdentity& identity) {
    require_utf8(identity.product, "product");
    require_utf8(identity.version, "version");
    require_utf8(identity.vendor, "vendor");
    require_utf8(identity.website, "website");
    require_utf8(identity.copyright, "copyright");
    require_utf8(identity.credits, "credits");
    const std::string_view website = identity.website;
    const bool https = website.size() > https_prefix.size() && website.substr(0U, https_prefix.size()) == https_prefix;
    if (!website.empty() && !https) {
        throw std::invalid_argument("ProgramIdentity website must be empty or an https:// address");
    }
    ProgramIdentity copy = identity;
    identity_storage() = std::move(copy);
}

const ProgramIdentity& program_identity() noexcept {
    return identity_storage();
}

void set_business_card_remark_source(BusinessCardRemarkSource source) noexcept {
    remark_source_storage() = source;
}

BusinessCardRemarkSource business_card_remark_source() noexcept {
    return remark_source_storage();
}

void set_diagnostic_overlay(bool enabled) noexcept {
    set_invalidation_overlay_default(enabled);
}

bool diagnostic_overlay() noexcept {
    const bool on = invalidation_overlay_default();
    return on;
}

bool toggle_diagnostic_overlay() noexcept {
    const bool next = !invalidation_overlay_default();
    set_invalidation_overlay_default(next);
    return next;
}

// -- BusinessCard ----------------------------------------------------------------------

BusinessCard::BusinessCard(StableId stable_id, BusinessCardPresentation presentation)
    : Control(std::move(stable_id)), presentation_(presentation),
      turn_(*this, MotionCurve::spring(turn_response, turn_overshoot), 0.0),
      pitch_(*this, MotionCurve::spring(turn_response), 0.0) {
    const bool known =
        presentation == BusinessCardPresentation::page || presentation == BusinessCardPresentation::hand;
    if (!known) throw std::invalid_argument("BusinessCard presentation is outside BusinessCardPresentation");
    focusable = true;
    tab_stop = true;
    cursor = CursorKind::hand;
    // Radians, settled to within half a device pixel of arc at the card's
    // edge (half its width from the axis).
    static_cast<void>(turn_.set_extent(hand_card_size.width * 0.5));
    static_cast<void>(pitch_.set_extent(hand_card_size.height * 0.5));
    turn_.set_invalidation(Dirty::paint);
    pitch_.set_invalidation(Dirty::paint);
    remarks_.reserve(maximum_business_card_remarks);
}

BusinessCard::~BusinessCard() = default;

Size BusinessCard::card_size() const noexcept {
    const Size base = presentation_ == BusinessCardPresentation::hand ? hand_card_size : page_card_size;
    const double scale = effective_text_scale();
    const Size scaled{base.width * scale, base.height * scale};
    return scaled;
}

Size BusinessCard::measure(Size) {
    const Size card = card_size();
    const double margin = card.height * margin_share;
    const Size wanted{card.width + margin * 2.0, card.height + margin * 2.0};
    return wanted;
}

// The stock centred in the client, at its size or the client's less the
// margin, keeping the card's proportion; the lines are shares of its
// height so that every size prints the same card.
BusinessCard::Layout BusinessCard::layout() const noexcept {
    Layout placed{};
    const Rect client = client_rectangle();
    const Size wanted = card_size();
    if (client.empty() || wanted.width <= 0.0 || wanted.height <= 0.0) return placed;
    const double margin = wanted.height * margin_share;
    const double room_width = std::max(0.0, client.width - margin * 2.0);
    const double room_height = std::max(0.0, client.height - margin * 2.0);
    const double fit = std::min({1.0, room_width / wanted.width, room_height / wanted.height});
    const double width = wanted.width * fit;
    const double height = wanted.height * fit;
    placed.card = Rect{client.x + std::floor((client.width - width) * 0.5),
                       client.y + std::floor((client.height - height) * 0.5), width, height};
    const Rect card = placed.card;
    const double inset = width * 0.08;
    placed.vendor_name = Rect{card.x + inset, card.y + height * 0.20, width - inset * 2.0, height * 0.24};
    const double rule_inset = width * 0.2;
    placed.rule = Rect{card.x + rule_inset, placed.vendor_name.bottom() + height * 0.04, width - rule_inset * 2.0,
                       std::max(1.0, height * 0.008)};
    placed.product = Rect{card.x + inset, placed.rule.bottom() + height * 0.05, width - inset * 2.0, height * 0.11};
    const ProgramIdentity& identity = program_identity();
    if (!identity.website.empty()) {
        placed.website =
            Rect{card.x + inset, placed.product.bottom() + height * 0.02, width - inset * 2.0, height * 0.10};
    }
    if (presentation_ == BusinessCardPresentation::hand) {
        if (!identity.copyright.empty()) {
            placed.copyright =
                Rect{card.x + inset, card.bottom() - height * 0.17, width - inset * 2.0, height * 0.11};
        }
        const double side = height * 0.09;
        const double corner = height * 0.05;
        placed.close_mark = Rect{card.right() - corner - side, card.y + corner, side, side};
    }
    return placed;
}

Rect BusinessCard::card_bounds() const noexcept {
    return layout().card;
}

Rect BusinessCard::vendor_name_band() const noexcept {
    return layout().vendor_name;
}

Rect BusinessCard::website_band() const noexcept {
    return layout().website;
}

Rect BusinessCard::close_mark_bounds() const noexcept {
    return layout().close_mark;
}

bool BusinessCard::reduced_motion() const noexcept {
    const SurfaceRoot* const root = attached_surface_root();
    const bool reduced = root != nullptr && (*root).presentation_settings().reduced_motion;
    return reduced;
}

// -- the turn ------------------------------------------------------------------------

double BusinessCard::turn() const noexcept {
    return turn_.current();
}

double BusinessCard::pitch() const noexcept {
    return pitch_.current();
}

bool BusinessCard::face_up() const noexcept {
    const bool up = detail::card_front_faces_eye(turn_.current(), pitch_.current());
    return up;
}

bool BusinessCard::turn_available() const {
    const bool available = presentation_ == BusinessCardPresentation::hand &&
                           attached_surface_root() != nullptr && !faces_refused_;
    return available;
}

// Whether the card is off its rest: being dragged, moving, tipped, or at a
// turn that is not a whole number of half turns.
bool BusinessCard::turning() const noexcept {
    if (presentation_ != BusinessCardPresentation::hand) return false;
    if (dragging_ || !turn_.settled() || !pitch_.settled()) return true;
    const bool tipped = std::abs(pitch_.current()) > turn_rest_tolerance;
    const bool between = std::abs(turn_.current() - nearest_rest(turn_.current())) > turn_rest_tolerance;
    return tipped || between;
}

bool BusinessCard::faces_ready(double scale) const noexcept {
    const bool ready = !front_face_.pixels.empty() && !back_face_.pixels.empty() && front_face_.scale == scale &&
                       back_face_.scale == scale;
    return ready;
}

std::optional<Point> BusinessCard::face_point(Point local_point) const noexcept {
    const Layout placed = layout();
    const detail::CardTurnPose pose = turn_pose(placed.card, turn_.current(), pitch_.current());
    const detail::CardTurnHit hit = detail::turned_card_hit(pose, local_point);
    if (!hit.hit || !hit.front) return std::nullopt;
    const Point on_face{placed.card.x + hit.u * placed.card.width, placed.card.y + hit.v * placed.card.height};
    return on_face;
}

// One face painted offscreen through the host's renderer, at `scale`, and
// copied out tight. The framebuffer is the card's for the call alone.
bool BusinessCard::render_face(SurfaceRoot& root, const Layout& at_origin, Size logical, double scale, bool front,
                               FacePixels& face) const {
    const std::unique_ptr<PaintFramebuffer> framebuffer = root.create_framebuffer(logical, scale);
    if (!framebuffer) return false;
    const Rect whole{0.0, 0.0, logical.width, logical.height};
    if (!(*framebuffer).begin(root.image_resources(), whole)) return false;
    paint_face((*framebuffer).painter(), at_origin, Point{}, front, false);
    (*framebuffer).end();
    const std::uint32_t width = (*framebuffer).width();
    const std::uint32_t height = (*framebuffer).height();
    const std::size_t source_row = (*framebuffer).row_bytes();
    const std::size_t row = static_cast<std::size_t>(width) * 4U;
    const std::span<std::byte> pixels = (*framebuffer).pixels();
    if (width == 0U || height == 0U || source_row < row || pixels.size() < source_row * height) return false;
    face.pixels.resize(row * height);
    for (std::uint32_t y = 0U; y < height; ++y) {
        const std::byte* const from = pixels.data() + static_cast<std::size_t>(y) * source_row;
        std::memcpy(face.pixels.data() + static_cast<std::size_t>(y) * row, from, row);
    }
    face.width = width;
    face.height = height;
    face.scale = scale;
    return true;
}

bool BusinessCard::render_faces() {
    SurfaceRoot* const root = attached_surface_root();
    const Layout placed = layout();
    if (root == nullptr || placed.card.empty()) return false;
    const double scale = (*root).scale();
    const Point back_to_origin{-placed.card.x, -placed.card.y};
    Layout at_origin = placed;
    at_origin.card = offset_rect(placed.card, back_to_origin);
    at_origin.vendor_name = offset_rect(placed.vendor_name, back_to_origin);
    at_origin.rule = offset_rect(placed.rule, back_to_origin);
    at_origin.product = offset_rect(placed.product, back_to_origin);
    at_origin.website = offset_rect(placed.website, back_to_origin);
    at_origin.copyright = offset_rect(placed.copyright, back_to_origin);
    at_origin.close_mark = offset_rect(placed.close_mark, back_to_origin);
    const Size logical{placed.card.width, placed.card.height};
    FacePixels front{};
    FacePixels back{};
    const bool painted = render_face(*root, at_origin, logical, scale, true, front) &&
                         render_face(*root, at_origin, logical, scale, false, back);
    faces_refused_ = !painted;
    if (!painted) return false;
    front_face_ = std::move(front);
    back_face_ = std::move(back);
    return true;
}

// Before a turn starts: the faces painted afresh (the identity, the theme
// or the scale may have changed since the last turn).
void BusinessCard::begin_turn_gesture() {
    if (presentation_ != BusinessCardPresentation::hand) return;
    static_cast<void>(render_faces());
}

// Let go: to the half turn nearer along the spin, and level.
void BusinessCard::settle_turn() {
    const double carried = turn_.current() + drag_speed_ * fling_carry_seconds;
    static_cast<void>(turn_.animate_to(nearest_rest(carried)));
    static_cast<void>(pitch_.animate_to(0.0));
    drag_speed_ = 0.0;
}

// Left (-1) or Right (+1): over by half a turn from the rest it is going to.
void BusinessCard::turn_over(double direction) {
    begin_turn_gesture();
    if (!turn_available()) return;
    const double base = nearest_rest(turn_.target());
    static_cast<void>(turn_.animate_to(base + direction * pi));
    static_cast<void>(pitch_.animate_to(0.0));
}

// -- painting --------------------------------------------------------------------------

void BusinessCard::paint_face(Painter& painter, const Layout& placed, Point offset, bool front, bool keyed) const {
    const ProgramIdentity& identity = program_identity();
    AnatomyStateSet states = anatomy_states();
    if (!keyed) {
        // An offscreen face is the card at rest: no hot, pressed or focus.
        states.erase(AnatomyState::hot);
        states.erase(AnatomyState::pressed);
        states.erase(AnatomyState::focus_visible);
    }
    const double height = placed.card.height;

    PartPaint stock{.role = PartRole::face, .bounds = offset_rect(placed.card, offset), .states = states,
                    .family = ControlVisualRole::card};
    stock.corner_radius = height * 0.03;
    if (keyed) stock.key = PartKey{"stock", {}, {}};
    paint_part(painter, *this, stock);
    // The back is the stock alone.
    if (!front) return;

    FontSpec vendor_font{};
    vendor_font.size = height * 0.13;
    vendor_font.weight = 700;
    AnatomyStateSet vendor_states = states;
    vendor_states.erase(AnatomyState::hot);
    if (keyed && vendor_hot_) vendor_states.insert(AnatomyState::hot);
    PartPaint vendor{.role = PartRole::label, .bounds = offset_rect(placed.vendor_name, offset),
                     .states = vendor_states, .family = ControlVisualRole::card};
    vendor.text = lend_paint_text(identity.vendor);
    vendor.font = vendor_font;
    vendor.alignment = PartTextAlignment::center;
    if (keyed) vendor.key = PartKey{"vendor_name", {}, {}};
    paint_part(painter, *this, vendor);

    PartPaint rule{.role = PartRole::divider, .bounds = offset_rect(placed.rule, offset), .states = states,
                   .family = ControlVisualRole::card};
    if (keyed) rule.key = PartKey{"rule", {}, {}};
    paint_part(painter, *this, rule);

    const std::string product = product_line(identity);
    FontSpec line_font{};
    line_font.size = height * 0.065;
    PartPaint product_part{.role = PartRole::label, .bounds = offset_rect(placed.product, offset),
                           .states = states, .family = ControlVisualRole::card};
    product_part.text = lend_paint_text(product);
    product_part.font = line_font;
    product_part.alignment = PartTextAlignment::center;
    if (keyed) product_part.key = PartKey{"product", {}, {}};
    paint_part(painter, *this, product_part);

    if (!placed.website.empty()) {
        AnatomyStateSet website_states = states;
        website_states.erase(AnatomyState::hot);
        if (keyed && website_hot_) website_states.insert(AnatomyState::hot);
        FontSpec website_font{};
        website_font.size = height * 0.06;
        PartPaint website{.role = PartRole::label, .bounds = offset_rect(placed.website, offset),
                          .states = website_states, .family = ControlVisualRole::card};
        website.text = lend_paint_text(printed_website(identity));
        website.font = website_font;
        website.alignment = PartTextAlignment::center;
        website.tone = PartTextTone::secondary;
        if (keyed) website.key = PartKey{"website", {}, {}};
        paint_part(painter, *this, website);
    }

    if (!placed.copyright.empty()) {
        FontSpec copyright_font{};
        copyright_font.size = height * 0.055;
        PartPaint copyright{.role = PartRole::label, .bounds = offset_rect(placed.copyright, offset),
                            .states = states, .family = ControlVisualRole::card};
        copyright.text = lend_paint_text(identity.copyright);
        copyright.font = copyright_font;
        copyright.alignment = PartTextAlignment::center;
        copyright.tone = PartTextTone::secondary;
        if (keyed) copyright.key = PartKey{"copyright", {}, {}};
        paint_part(painter, *this, copyright);
    }

    if (!placed.close_mark.empty()) {
        AnatomyStateSet close_states = states;
        close_states.erase(AnatomyState::hot);
        if (keyed && close_hot_) close_states.insert(AnatomyState::hot);
        PartPaint close{.role = PartRole::glyph, .bounds = offset_rect(placed.close_mark, offset),
                        .states = close_states, .family = ControlVisualRole::card};
        close.mark = PartMark::close;
        if (keyed) close.key = PartKey{"close_mark", {}, {}};
        paint_part(painter, *this, close);
    }
}

// The card turned: its shadow under the turned outline, then the faces
// drawn turned and lit into the kept picture, shown by the image role.
// Without faces at this scale, the flat card at its rest.
void BusinessCard::paint_turned(Painter& painter, const Layout& placed) {
    const SurfaceRoot* const root = attached_surface_root();
    const double scale = root != nullptr ? (*root).scale() : 1.0;
    const AnatomyStateSet states = anatomy_states();
    const double height = placed.card.height;
    const detail::CardTurnPose pose = turn_pose(placed.card, turn_.current(), pitch_.current());
    const Rect outline = detail::turned_card_bounds(pose);
    PartPaint shadow{.role = PartRole::shadow,
                     .bounds = offset_rect(outline.empty() ? placed.card : outline, Point{0.0, height * shadow_drop_share}),
                     .states = states,
                     .family = ControlVisualRole::card};
    shadow.corner_radius = height * 0.03;
    shadow.key = PartKey{"shadow", {}, {}};
    paint_part(painter, *this, shadow);
    if (!faces_ready(scale)) {
        paint_face(painter, placed, Point{}, face_up(), true);
        return;
    }
    const Rect client = client_rectangle();
    const DeviceExtent extent{static_cast<std::uint32_t>(std::ceil(client.width * scale)),
                              static_cast<std::uint32_t>(std::ceil(client.height * scale))};
    // The kept picture is written again only while nothing else (a
    // recording of an earlier paint) still holds it.
    const bool reusable = picture_ && picture_.use_count() == 1 && (*picture_).extent() == extent &&
                          (*picture_).scale() == DeviceScale{scale};
    std::shared_ptr<PartRaster> target = reusable ? picture_ : PartRaster::create(extent, DeviceScale{scale});
    if (!target) {
        paint_face(painter, placed, Point{}, face_up(), true);
        return;
    }
    detail::CardTurnPose in_picture = pose;
    in_picture.card = offset_rect(placed.card, Point{-client.x, -client.y});
    const detail::CardFaceView front{front_face_.pixels, front_face_.width, front_face_.height,
                                     static_cast<std::size_t>(front_face_.width) * 4U};
    const detail::CardFaceView back{back_face_.pixels, back_face_.width, back_face_.height,
                                    static_cast<std::size_t>(back_face_.width) * 4U};
    const detail::CardTurnLight light{};
    const bool drawn = detail::draw_turned_card(front, back, in_picture, light, scale, (*target).view());
    (*target).record_opacity();
    if (!drawn) {
        paint_face(painter, placed, Point{}, face_up(), true);
        return;
    }
    picture_ = std::move(target);
    PartPaint picture{.role = PartRole::image, .bounds = client, .states = states,
                      .family = ControlVisualRole::card};
    picture.raster = picture_;
    paint_part(painter, *this, picture);
}

void BusinessCard::on_paint(Painter& painter, Rect) {
    // The remark timer waits for this paint after each damaging tick.
    remark_frames_.note_painted();
    const Layout placed = layout();
    if (placed.card.empty()) return;
    const AnatomyStateSet states = anatomy_states();
    const double height = placed.card.height;
    // The heading band rule: nothing above the band's lower edge.
    const std::optional<double> band = heading_band_bottom();
    const Rect client = client_rectangle();
    const bool clipped = band.has_value() && *band > client.y - heading_band_slack;
    if (clipped) {
        painter.save();
        const double reach = height;
        painter.clip_rect(Rect{client.x - reach, *band, client.width + reach * 2.0,
                               std::max(0.0, client.bottom() + reach - *band)});
    }
    if (turning()) {
        paint_turned(painter, placed);
    } else {
        const Point face_offset = tilt_;
        const Point shadow_offset{-tilt_.x * shadow_lean, height * shadow_drop_share - tilt_.y * shadow_lean};
        PartPaint shadow{.role = PartRole::shadow, .bounds = offset_rect(placed.card, shadow_offset),
                         .states = states, .family = ControlVisualRole::card};
        shadow.corner_radius = height * 0.03;
        shadow.key = PartKey{"shadow", {}, {}};
        paint_part(painter, *this, shadow);
        paint_face(painter, placed, face_offset, face_up(), true);
        if (states.contains(AnatomyState::focus_visible)) {
            PartPaint ring{.role = PartRole::focus, .bounds = offset_rect(placed.card, face_offset),
                           .states = states, .family = ControlVisualRole::card};
            ring.corner_radius = height * 0.03;
            ring.key = PartKey{"focus", {}, {}};
            paint_part(painter, *this, ring);
        }
    }
    if (clipped) painter.restore();
    paint_remarks(painter);
}

// -- the remarks -----------------------------------------------------------------------

// The window's motion clock time (a test may set its source), or the
// steady clock's while detached.
FrameTime BusinessCard::card_now() const noexcept {
    const SurfaceRoot* const root = attached_surface_root();
    if (root == nullptr) return FrameClock::now();
    return (*root).motion_clock().now();
}

const std::string& BusinessCard::remark() const& noexcept {
    static const std::string none{};
    if (remarks_.empty()) return none;
    return remarks_.back().text;
}

// The window's client in this control's coordinates, less a margin: where
// a remark may lie.
Rect BusinessCard::remark_room() const noexcept {
    const SurfaceRoot* const root = attached_surface_root();
    if (root == nullptr) return client_rectangle();
    const Size client = (*root).client_size();
    const Rect absolute = absolute_bounds();
    const Rect room{remark_margin - absolute.x, remark_margin - absolute.y,
                    std::max(0.0, client.width - remark_margin * 2.0), std::max(0.0, client.height - remark_margin * 2.0)};
    return room;
}

Insets BusinessCard::visual_outsets() const noexcept {
    const SurfaceRoot* const root = attached_surface_root();
    if (presentation_ != BusinessCardPresentation::hand || root == nullptr) return Control::visual_outsets();
    const Size client = (*root).client_size();
    const Rect absolute = absolute_bounds();
    const Insets outsets{std::max(0.0, absolute.x), std::max(0.0, absolute.y),
                         std::max(0.0, client.width - absolute.right()),
                         std::max(0.0, client.height - absolute.bottom())};
    return outsets;
}

double BusinessCard::remark_opacity(const SaidRemark& remark, FrameTime now) const noexcept {
    if (reduced_motion()) return 1.0;
    const double age = std::chrono::duration<double>(now - remark.born).count();
    const double rising = std::clamp(age / remark_rise_seconds, 0.0, 1.0);
    const double fading = std::clamp((remark.life - age) / remark_fade_seconds, 0.0, 1.0);
    const double opacity = std::min(rising, fading);
    return opacity;
}

void BusinessCard::paint_remarks(Painter& painter) {
    if (remarks_.empty()) return;
    const FrameTime now = card_now();
    const AnatomyStateSet states = anatomy_states();
    FontSpec remark_font{};
    remark_font.size = remark_font_size * effective_text_scale();
    remark_font.italic = true;
    for (const SaidRemark& said : remarks_) {
        const double opacity = remark_opacity(said, now);
        if (!(opacity > 0.0)) continue;
        const bool fading = opacity < 1.0;
        if (fading) painter.begin_group(PaintGroup{.bounds = said.box, .opacity = opacity});
        PartPaint remark{.role = PartRole::label, .bounds = said.box, .states = states,
                         .family = ControlVisualRole::card};
        remark.text = lend_paint_text(said.text);
        remark.font = remark_font;
        remark.alignment = PartTextAlignment::center;
        paint_part(painter, *this, remark);
        if (fading) painter.end_group();
    }
}

AnimationTickResult BusinessCard::RemarkTick::operator()(FrameTime now) const {
    const std::shared_ptr<BusinessCard> live = card.lock();
    const AnimationTickResult result = live ? (*live).on_remark_tick(now) : AnimationTickResult::unchanged;
    return result;
}

// The pointer came over the card in the hand: the first remark a moment
// later, on the remark timer.
void BusinessCard::start_remarks() {
    if (presentation_ != BusinessCardPresentation::hand) return;
    if (business_card_remark_source().write_remark == nullptr) return;
    next_remark_ = card_now() + seconds_interval(first_remark_seconds);
    if (remark_frames_.started()) return;
    const std::shared_ptr<BusinessCard> self = std::static_pointer_cast<BusinessCard>(shared_from_this());
    remark_frames_.start(*this, remark_frame_interval, RemarkTick{self});
}

// One tick: the remarks past their life leave, a new one is said when due
// while the pointer stays over the card, and the timer stops once nothing
// is shown and the pointer is away.
AnimationTickResult BusinessCard::on_remark_tick(FrameTime now) {
    const CardHandlerScope running;
    bool changed = false;
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < remarks_.size(); ++index) {
        const double age = std::chrono::duration<double>(now - remarks_[index].born).count();
        if (age > remarks_[index].life) {
            changed = true;
            continue;
        }
        if (kept != index) remarks_[kept] = std::move(remarks_[index]);
        ++kept;
    }
    remarks_.erase(remarks_.begin() + static_cast<std::ptrdiff_t>(kept), remarks_.end());
    const bool due = pointer_over_ && now >= next_remark_ && remarks_.size() < maximum_business_card_remarks;
    if (due) {
        say_remark(now);
        const double gap = remark_gap_seconds + remark_gap_spread_seconds * unit_random(remark_seed_);
        next_remark_ = now + seconds_interval(gap);
        changed = true;
    }
    // A shown remark rises or fades every tick.
    if (!remarks_.empty()) changed = true;
    if (remarks_.empty() && !pointer_over_) remark_frames_.stop();
    if (!changed) return AnimationTickResult::unchanged;
    invalidate(Dirty::paint);
    return AnimationTickResult::damaged;
}

// Asks the source for a remark and places it at random around the card:
// clear of the card and of the other remarks, near enough to be about it,
// inside the window. A remark with no room is not shown.
void BusinessCard::say_remark(FrameTime now) {
    const BusinessCardRemarkSource source = business_card_remark_source();
    if (source.write_remark == nullptr) return;
    std::string text{};
    source.write_remark(source.context, text);
    if (!is_alive()) return;
    text.resize(whole_prefix(text, maximum_business_card_remark));
    if (text.empty() || !validate_utf8(text).valid()) return;
    const Layout placed = layout();
    if (placed.card.empty()) return;
    const double font = remark_font_size * effective_text_scale();
    const Rect room = remark_room();
    // An estimate of the line's width (half an em a byte), as wide as the
    // room at most; the label centres the text in it.
    const double width = std::min(room.width, font * 0.5 * static_cast<double>(text.size()) + font);
    const double height = font * 1.7;
    const Rect card = inflated(placed.card, remark_card_clearance);
    const double centre_x = placed.card.x + placed.card.width * 0.5;
    const double centre_y = placed.card.y + placed.card.height * 0.5;
    for (std::uint32_t attempt = 0U; attempt < remark_placement_attempts; ++attempt) {
        const double angle = 2.0 * pi * unit_random(remark_seed_);
        const double reach = placed.card.width * (0.58 + 0.12 * unit_random(remark_seed_));
        const Rect box{centre_x + std::cos(angle) * reach - width * 0.5,
                       centre_y + std::sin(angle) * reach * 0.62 - height * 0.5, width, height};
        const bool inside = box.x >= room.x && box.y >= room.y && box.right() <= room.right() &&
                            box.bottom() <= room.bottom();
        if (!inside || overlaps(box, card)) continue;
        bool crowded = false;
        for (const SaidRemark& other : remarks_) {
            if (overlaps(inflated(box, remark_spacing), other.box)) crowded = true;
        }
        if (crowded) continue;
        const double life = std::min(remark_longest_life_seconds,
                                     remark_life_seconds + remark_life_per_byte_seconds * static_cast<double>(text.size()));
        remarks_.push_back(SaidRemark{std::move(text), box, now, life});
        return;
    }
}

// -- hit testing and input -------------------------------------------------------------

// Only the stock takes the pointer: in the hand, a press beside the card
// reaches the layer, which counts it as a press off the card. Above a
// heading band, nothing does.
bool BusinessCard::hit_test_local(Point local_point) const {
    const std::optional<double> band = heading_band_bottom();
    if (band.has_value() && local_point.y < *band) return false;
    if (turning()) {
        const detail::CardTurnPose pose = turn_pose(layout().card, turn_.current(), pitch_.current());
        const bool over = detail::turned_card_hit(pose, local_point).hit;
        return over;
    }
    const bool inside = layout().card.contains(local_point);
    return inside;
}

// What a press at `local_point` is on. Turned, the point is carried to the
// face as it lies at rest; on the back, only the card.
BusinessCard::PressTarget BusinessCard::target_at(Point local_point) const noexcept {
    const Layout placed = layout();
    Point point = local_point;
    if (turning()) {
        const std::optional<Point> on_face = face_point(local_point);
        if (!on_face.has_value()) {
            const detail::CardTurnPose pose = turn_pose(placed.card, turn_.current(), pitch_.current());
            const bool over = detail::turned_card_hit(pose, local_point).hit;
            return over ? PressTarget::card : PressTarget::none;
        }
        point = *on_face;
    } else if (!face_up()) {
        return placed.card.contains(point) ? PressTarget::card : PressTarget::none;
    }
    if (placed.close_mark.contains(point)) return PressTarget::close_mark;
    if (presentation_ == BusinessCardPresentation::hand && placed.website.contains(point)) {
        return PressTarget::website;
    }
    if (placed.card.contains(point)) return PressTarget::card;
    return PressTarget::none;
}

// The tilt and the hot parts for the pointer at `local_point`: the face
// leans toward it in proportion to its distance from the card's centre
// (hand), or rises while it is over the card (page). A pointer arriving
// over the card in the hand starts the remarks.
void BusinessCard::follow_pointer(Point local_point) {
    const Layout placed = layout();
    const bool over = hit_test_local(local_point);
    if (presentation_ == BusinessCardPresentation::hand) {
        const bool arrived = over && !pointer_over_;
        pointer_over_ = over;
        if (arrived) start_remarks();
    }
    const bool flat = !turning() && face_up();
    Point tilt{};
    if (over && flat && !reduced_motion()) {
        const double height = placed.card.height;
        if (presentation_ == BusinessCardPresentation::hand) {
            const double across = clamp_unit((local_point.x - (placed.card.x + placed.card.width * 0.5)) /
                                             (placed.card.width * 0.5));
            const double down = clamp_unit((local_point.y - (placed.card.y + placed.card.height * 0.5)) /
                                           (placed.card.height * 0.5));
            tilt = Point{across * height * hand_tilt_share, down * height * hand_tilt_share};
        } else {
            tilt = Point{0.0, -height * page_rise_share};
        }
    }
    const bool vendor = flat && placed.vendor_name.contains(local_point);
    const bool website =
        flat && presentation_ == BusinessCardPresentation::hand && placed.website.contains(local_point);
    const bool close = flat && placed.close_mark.contains(local_point);
    const bool changed = tilt.x != tilt_.x || tilt.y != tilt_.y || vendor != vendor_hot_ ||
                         website != website_hot_ || close != close_hot_;
    tilt_ = tilt;
    vendor_hot_ = vendor;
    website_hot_ = website;
    close_hot_ = close;
    if (changed) invalidate(Dirty::paint);
}

void BusinessCard::on_mouse_move(MouseEventArgs& arguments) {
    const CardHandlerScope running;
    const bool hand = presentation_ == BusinessCardPresentation::hand;
    if (hand && press_target_ != PressTarget::none) {
        const Point at = arguments.location;
        const double moved = std::hypot(at.x - drag_origin_.x, at.y - drag_origin_.y);
        if (!dragging_ && moved > drag_threshold) {
            begin_turn_gesture();
            if (turn_available()) {
                dragging_ = true;
                drag_turn_ = turn_.current();
                drag_pitch_ = pitch_.current();
                drag_last_ = at;
                drag_last_time_ = card_now();
                drag_speed_ = 0.0;
            }
        }
        if (dragging_) {
            // A card's width dragged across turns it half over.
            const double width = std::max(1.0, layout().card.width);
            const double per_unit = pi / width;
            const double turned = drag_turn_ + (at.x - drag_origin_.x) * per_unit;
            const double tipped = std::clamp(drag_pitch_ - (at.y - drag_origin_.y) * per_unit * 0.5,
                                             -maximum_business_card_pitch, maximum_business_card_pitch);
            const FrameTime now = card_now();
            const double elapsed = std::chrono::duration<double>(now - drag_last_time_).count();
            if (elapsed > 0.0) {
                const double sample = (at.x - drag_last_.x) * per_unit / elapsed;
                drag_speed_ = 0.5 * drag_speed_ + 0.5 * sample;
            }
            drag_last_ = at;
            drag_last_time_ = now;
            turn_.jump_to(turned);
            pitch_.jump_to(tipped);
            invalidate(Dirty::paint);
            arguments.handled = true;
        }
    }
    follow_pointer(arguments.location);
    Control::on_mouse_move(arguments);
}

void BusinessCard::on_mouse_leave() {
    const bool changed = tilt_.x != 0.0 || tilt_.y != 0.0 || vendor_hot_ || website_hot_ || close_hot_;
    tilt_ = Point{};
    vendor_hot_ = false;
    website_hot_ = false;
    close_hot_ = false;
    pointer_over_ = false;
    if (!dragging_) press_target_ = PressTarget::none;
    if (changed) invalidate(Dirty::paint);
    Control::on_mouse_leave();
}

// The gesture: D held and a secondary press on the vendor name toggles the
// diagnostic overlay. The card repaints, so the window presents again and
// shows the overlay's new state.
void BusinessCard::on_mouse_down(MouseEventArgs& arguments) {
    const CardHandlerScope running;
    const bool on_vendor = !turning() && face_up() && layout().vendor_name.contains(arguments.location);
    if (arguments.button == PointerButton::secondary && on_vendor) {
        const SurfaceRoot* const root = attached_surface_root();
        if (root != nullptr && (*root).is_key_down(PhysicalKey::d)) {
            static_cast<void>(toggle_diagnostic_overlay());
            invalidate(Dirty::paint);
            arguments.handled = true;
        }
    } else if (arguments.button == PointerButton::primary) {
        press_target_ = target_at(arguments.location);
        if (press_target_ != PressTarget::none) {
            drag_origin_ = arguments.location;
            dragging_ = false;
            invalidate(Dirty::paint);
            arguments.handled = true;
        }
    }
    Control::on_mouse_down(arguments);
}

void BusinessCard::on_mouse_up(MouseEventArgs& arguments) {
    const CardHandlerScope running;
    if (arguments.button == PointerButton::primary && dragging_) {
        dragging_ = false;
        press_target_ = PressTarget::none;
        settle_turn();
        invalidate(Dirty::paint);
        arguments.handled = true;
    } else if (arguments.button == PointerButton::primary && press_target_ != PressTarget::none) {
        const PressTarget began = press_target_;
        press_target_ = PressTarget::none;
        invalidate(Dirty::paint);
        arguments.handled = true;
        const bool same = target_at(arguments.location) == began;
        if (same) {
            switch (began) {
            case PressTarget::close_mark: put_self_away(); break;
            case PressTarget::website: ask_for_website(); break;
            case PressTarget::card:
                if (presentation_ == BusinessCardPresentation::page) ask_to_open();
                break;
            case PressTarget::none:
            default: break;
            }
        }
    }
    Control::on_mouse_up(arguments);
}

void BusinessCard::on_key_bubble(KeyEvent& event) {
    const CardHandlerScope running;
    if (event.action != KeyAction::down || event.handled) return;
    const bool hand = presentation_ == BusinessCardPresentation::hand;
    if (hand && event.physical_key == PhysicalKey::escape) {
        event.handled = true;
        put_self_away();
        return;
    }
    if (hand && (event.physical_key == PhysicalKey::left || event.physical_key == PhysicalKey::right)) {
        event.handled = true;
        turn_over(event.physical_key == PhysicalKey::left ? -1.0 : 1.0);
        return;
    }
    const bool activation = event.physical_key == PhysicalKey::enter || event.physical_key == PhysicalKey::space;
    if (!activation) return;
    event.handled = true;
    if (!hand) {
        ask_to_open();
    } else {
        ask_for_website();
    }
}

void BusinessCard::ask_to_open() {
    BusinessCardRequest request{};
    open_requested_.emit(request);
    if (request.handled || !is_alive()) return;
    Window* const window = attached_window();
    if (window != nullptr) static_cast<void>(show_business_card(*window));
}

// Unhandled, the address goes to the window's host (its open-URL
// capability); the outcome is kept for the program and for tests.
void BusinessCard::ask_for_website() {
    const ProgramIdentity& identity = program_identity();
    if (identity.website.empty()) return;
    BusinessCardRequest request{};
    website_requested_.emit(request);
    if (!is_alive()) return;
    if (request.handled) {
        last_website_request_ = BusinessCardLinkOutcome::handled;
        return;
    }
    const SurfaceRoot* const root = attached_surface_root();
    HostServices* const services = root != nullptr ? (*root).host_services() : nullptr;
    const bool capable = services != nullptr && (*services).snapshot().capabilities.supports(HostCapability::open_url);
    if (!capable) {
        last_website_request_ = BusinessCardLinkOutcome::unavailable;
        return;
    }
    // The identity's own copy: program_identity() may be replaced by a
    // handler, never during this call.
    const std::string address = identity.website;
    const HostServiceStatus status = (*services).open_url(address);
    last_website_request_ = status.accepted() ? BusinessCardLinkOutcome::opened : BusinessCardLinkOutcome::refused;
}

void BusinessCard::put_self_away() {
    Window* const window = attached_window();
    if (window != nullptr) hide_business_card(*window);
}

// A press beside the card in the hand (the layer's outside_pressed).
void BusinessCard::outside_pressed() {
    const CardHandlerScope running;
    put_self_away();
}

void BusinessCard::announce_put_away() {
    const CardHandlerScope running;
    tilt_ = Point{};
    vendor_hot_ = false;
    website_hot_ = false;
    close_hot_ = false;
    press_target_ = PressTarget::none;
    dragging_ = false;
    pointer_over_ = false;
    remarks_.clear();
    remark_frames_.stop();
    // The card comes out face up next time.
    turn_.jump_to(0.0);
    pitch_.jump_to(0.0);
    put_away_.emit();
}

// -- the heading band rule ---------------------------------------------------------

void BusinessCard::set_heading_band(const std::shared_ptr<const Control>& band) {
    heading_band_ = band;
    invalidate(Dirty::paint);
}

std::optional<double> BusinessCard::heading_band_bottom() const noexcept {
    if (presentation_ != BusinessCardPresentation::page) return std::nullopt;
    const std::shared_ptr<const Control> band = heading_band_.lock();
    if (!band) return std::nullopt;
    const SurfaceRoot* const mine = attached_surface_root();
    const SurfaceRoot* const theirs = (*band).attached_surface_root();
    if (mine == nullptr || mine != theirs) return std::nullopt;
    const double bottom = (*band).absolute_bounds().bottom() - absolute_bounds().y;
    return bottom;
}

SemanticDescriptor BusinessCard::semantic_descriptor() const {
    const ProgramIdentity& identity = program_identity();
    SemanticDescriptor descriptor;
    const bool hand = presentation_ == BusinessCardPresentation::hand;
    descriptor.role = hand && !identity.website.empty() ? SemanticRole::link : SemanticRole::button;
    if (!accessible_name.read().empty()) {
        descriptor.name = accessible_name.read();
    } else {
        descriptor.name = identity.vendor.empty() ? std::string("Business card") : identity.vendor + " business card";
    }
    descriptor.value = identity.website;
    std::string description = product_line(identity);
    if (!identity.credits.empty()) {
        if (!description.empty()) description += ". ";
        description += identity.credits;
    }
    descriptor.description = accessible_description.read().empty() ? description : accessible_description.read();
    if (!hand || !identity.website.empty()) descriptor.actions.push_back(SemanticAction::press);
    if (hand) descriptor.actions.push_back(SemanticAction::close);
    descriptor.exposed = true;
    return descriptor;
}

bool BusinessCard::on_semantic_action(SemanticAction action, std::string_view value) {
    const CardHandlerScope running;
    const bool hand = presentation_ == BusinessCardPresentation::hand;
    if (action == SemanticAction::press) {
        if (!hand) {
            ask_to_open();
            return true;
        }
        if (program_identity().website.empty()) return false;
        ask_for_website();
        return true;
    }
    if (action == SemanticAction::close && hand) {
        put_self_away();
        return true;
    }
    const bool handled = Control::on_semantic_action(action, value);
    return handled;
}

const ControlAnatomy& BusinessCard::anatomy() const noexcept {
    return business_card_anatomy;
}

AnatomyStateSet BusinessCard::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (press_target_ != PressTarget::none) states.insert(AnatomyState::pressed);
    return states;
}

void BusinessCard::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Layout placed = layout();
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    if (!placed.card.empty()) {
        const double height = placed.card.height;
        const Point shadow_offset{-tilt_.x * shadow_lean, height * shadow_drop_share - tilt_.y * shadow_lean};
        record.shadow = PartGeometry::from_bounds(offset_rect(placed.card, shadow_offset));
        record.stock = PartGeometry::from_bounds(offset_rect(placed.card, tilt_));
        record.vendor_name = PartGeometry::from_bounds(offset_rect(placed.vendor_name, tilt_));
        record.rule = PartGeometry::from_bounds(offset_rect(placed.rule, tilt_));
        record.product = PartGeometry::from_bounds(offset_rect(placed.product, tilt_));
        if (!placed.website.empty()) record.website = PartGeometry::from_bounds(offset_rect(placed.website, tilt_));
        if (!placed.copyright.empty()) {
            record.copyright = PartGeometry::from_bounds(offset_rect(placed.copyright, tilt_));
        }
        // The newest remark around the card.
        if (!remarks_.empty()) record.remark = PartGeometry::from_bounds(remarks_.back().box);
        if (!placed.close_mark.empty()) {
            record.close_mark = PartGeometry::from_bounds(offset_rect(placed.close_mark, tilt_));
        }
        record.focus = PartGeometry::from_bounds(offset_rect(placed.card, tilt_));
    }
    write_parts(anatomy(), business_card_part_fields, record, geometry);
}

BusinessCard::Parts BusinessCard::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), business_card_part_fields, published.parts(), record);
    return record;
}

namespace {

// The events BusinessCard adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 3> business_card_interactions{{
    {.name = "open_requested", .payload = "BusinessCardRequest", .mutable_payload = true},
    {.name = "put_away", .payload = "", .mutable_payload = false},
    {.name = "website_requested", .payload = "BusinessCardRequest", .mutable_payload = true},
}};

} // namespace

void BusinessCard::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), business_card_interactions.begin(), business_card_interactions.end());
}

// -- showing the card in the hand -------------------------------------------------------

// The card's pieces show_business_card and hide_business_card use (the
// card's friend).
class BusinessCardShowing final {
public:
    static void prepare(ModalLayer& layer, BusinessCard& card) {
        layer.close_on_escape = false;
        layer.accessible_name = std::string("Business card");
        on(layer.outside_pressed(), card, &BusinessCard::outside_pressed);
    }
    static void announce_put_away(BusinessCard& card) { card.announce_put_away(); }
};

bool show_business_card(Window& window) {
    ShownCards& cards = shown_cards();
    std::vector<ShownCard> released{};
    ShownCard record{};
    {
        const std::lock_guard<std::mutex> held(cards.lock);
        sweep_records(cards.records, &window, released);
        const std::size_t index = find_record(cards.records, &window);
        if (index != cards.records.size()) {
            ShownCard& found = cards.records[index];
            if (found.layer != nullptr && (*found.layer).is_shown()) return false;
            record.window = found.window;
            record.thread = found.thread;
            record.layer = found.layer;
            record.card = found.card;
        }
    }
    released.clear();
    const bool made = record.card == nullptr;
    if (made) {
        record.window = &window;
        record.thread = std::this_thread::get_id();
        record.layer = make_control<ModalLayer>(StableId("gui-forms.business-card.layer"));
        record.card = make_control<BusinessCard>(StableId("gui-forms.business-card"), BusinessCardPresentation::hand);
        BusinessCardShowing::prepare(*record.layer, *record.card);
    }
    const Control::Ptr owner = window.root();
    const bool shown = (*record.layer).show(owner, record.card);
    if (!shown) return false;
    if (made) {
        const std::lock_guard<std::mutex> held(cards.lock);
        cards.records.push_back(std::move(record));
    }
    return true;
}

void hide_business_card(Window& window) {
    const ShownCard record = copy_record(&window);
    if (record.layer == nullptr || !(*record.layer).is_shown()) return;
    const CardHandlerScope running;
    const bool closed = (*record.layer).close(DialogResult::cancel);
    if (closed) BusinessCardShowing::announce_put_away(*record.card);
}

bool business_card_shown(const Window& window) noexcept {
    const ShownCard record = copy_record(&window);
    const bool shown = record.layer != nullptr && (*record.layer).is_shown();
    return shown;
}

std::shared_ptr<BusinessCard> shown_business_card(const Window& window) noexcept {
    const ShownCard record = copy_record(&window);
    if (record.layer == nullptr || !(*record.layer).is_shown()) return nullptr;
    return record.card;
}

} // namespace gui_forms
