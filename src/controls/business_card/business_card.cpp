#include "gui_forms/components/business_card/business_card.hpp"

#include "gui_forms/controls/modal_layer.hpp"
#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
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

constexpr std::array<PartField<BusinessCard::Parts>, 10> business_card_part_fields{{
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
    {.declaration = {.name = "remark", .role = PartRole::label, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::remark},
    {.declaration = {.name = "close_mark", .role = PartRole::glyph, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::close_mark},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}, .states = {}},
     .member = &BusinessCard::Parts::focus},
}};

constexpr std::array<PartDeclaration, 10> business_card_parts = part_declarations(business_card_part_fields);

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
    : Control(std::move(stable_id)), presentation_(presentation) {
    const bool known =
        presentation == BusinessCardPresentation::page || presentation == BusinessCardPresentation::hand;
    if (!known) throw std::invalid_argument("BusinessCard presentation is outside BusinessCardPresentation");
    focusable = true;
    tab_stop = true;
    cursor = CursorKind::hand;
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
        const bool remark_shown = !remark_.empty() || !identity.copyright.empty();
        if (remark_shown) {
            placed.remark = Rect{card.x + inset, card.bottom() - height * 0.17, width - inset * 2.0, height * 0.11};
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

void BusinessCard::on_paint(Painter& painter, Rect) {
    const Layout placed = layout();
    if (placed.card.empty()) return;
    const ProgramIdentity& identity = program_identity();
    const AnatomyStateSet states = anatomy_states();
    const double height = placed.card.height;
    const Point face_offset = tilt_;
    const Point shadow_offset{-tilt_.x * shadow_lean, height * shadow_drop_share - tilt_.y * shadow_lean};

    PartPaint shadow{.role = PartRole::shadow, .bounds = offset_rect(placed.card, shadow_offset), .states = states,
                     .family = ControlVisualRole::card};
    shadow.corner_radius = height * 0.03;
    shadow.key = PartKey{"shadow", {}, {}};
    paint_part(painter, *this, shadow);

    PartPaint stock{.role = PartRole::face, .bounds = offset_rect(placed.card, face_offset), .states = states,
                    .family = ControlVisualRole::card};
    stock.corner_radius = height * 0.03;
    stock.key = PartKey{"stock", {}, {}};
    paint_part(painter, *this, stock);

    FontSpec vendor_font{};
    vendor_font.size = height * 0.13;
    vendor_font.weight = 700;
    AnatomyStateSet vendor_states = states;
    vendor_states.erase(AnatomyState::hot);
    if (vendor_hot_) vendor_states.insert(AnatomyState::hot);
    PartPaint vendor{.role = PartRole::label, .bounds = offset_rect(placed.vendor_name, face_offset),
                     .states = vendor_states, .family = ControlVisualRole::card};
    vendor.text = lend_paint_text(identity.vendor);
    vendor.font = vendor_font;
    vendor.alignment = PartTextAlignment::center;
    vendor.key = PartKey{"vendor_name", {}, {}};
    paint_part(painter, *this, vendor);

    PartPaint rule{.role = PartRole::divider, .bounds = offset_rect(placed.rule, face_offset), .states = states,
                   .family = ControlVisualRole::card};
    rule.key = PartKey{"rule", {}, {}};
    paint_part(painter, *this, rule);

    const std::string product = product_line(identity);
    FontSpec line_font{};
    line_font.size = height * 0.065;
    PartPaint product_part{.role = PartRole::label, .bounds = offset_rect(placed.product, face_offset),
                           .states = states, .family = ControlVisualRole::card};
    product_part.text = lend_paint_text(product);
    product_part.font = line_font;
    product_part.alignment = PartTextAlignment::center;
    product_part.key = PartKey{"product", {}, {}};
    paint_part(painter, *this, product_part);

    if (!placed.website.empty()) {
        AnatomyStateSet website_states = states;
        website_states.erase(AnatomyState::hot);
        if (website_hot_) website_states.insert(AnatomyState::hot);
        FontSpec website_font{};
        website_font.size = height * 0.06;
        PartPaint website{.role = PartRole::label, .bounds = offset_rect(placed.website, face_offset),
                          .states = website_states, .family = ControlVisualRole::card};
        website.text = lend_paint_text(printed_website(identity));
        website.font = website_font;
        website.alignment = PartTextAlignment::center;
        website.tone = PartTextTone::secondary;
        website.key = PartKey{"website", {}, {}};
        paint_part(painter, *this, website);
    }

    if (!placed.remark.empty()) {
        const std::string_view remark_text = remark_.empty() ? std::string_view{identity.copyright}
                                                              : std::string_view{remark_};
        FontSpec remark_font{};
        remark_font.size = height * 0.055;
        remark_font.italic = !remark_.empty();
        PartPaint remark{.role = PartRole::label, .bounds = offset_rect(placed.remark, face_offset),
                         .states = states, .family = ControlVisualRole::card};
        remark.text = lend_paint_text(remark_text);
        remark.font = remark_font;
        remark.alignment = PartTextAlignment::center;
        remark.tone = PartTextTone::secondary;
        remark.key = PartKey{"remark", {}, {}};
        paint_part(painter, *this, remark);
    }

    if (!placed.close_mark.empty()) {
        AnatomyStateSet close_states = states;
        close_states.erase(AnatomyState::hot);
        if (close_hot_) close_states.insert(AnatomyState::hot);
        PartPaint close{.role = PartRole::glyph, .bounds = offset_rect(placed.close_mark, face_offset),
                        .states = close_states, .family = ControlVisualRole::card};
        close.mark = PartMark::close;
        close.key = PartKey{"close_mark", {}, {}};
        paint_part(painter, *this, close);
    }

    if (states.contains(AnatomyState::focus_visible)) {
        PartPaint ring{.role = PartRole::focus, .bounds = offset_rect(placed.card, face_offset), .states = states,
                       .family = ControlVisualRole::card};
        ring.corner_radius = height * 0.03;
        ring.key = PartKey{"focus", {}, {}};
        paint_part(painter, *this, ring);
    }
}

// Only the stock takes the pointer: in the hand, a press beside the card
// reaches the layer, which counts it as a press off the card.
bool BusinessCard::hit_test_local(Point local_point) const {
    const bool inside = layout().card.contains(local_point);
    return inside;
}

BusinessCard::PressTarget BusinessCard::target_at(Point local_point) const noexcept {
    const Layout placed = layout();
    if (placed.close_mark.contains(local_point)) return PressTarget::close_mark;
    if (presentation_ == BusinessCardPresentation::hand && placed.website.contains(local_point)) {
        return PressTarget::website;
    }
    if (placed.card.contains(local_point)) return PressTarget::card;
    return PressTarget::none;
}

// The tilt and the hot parts for the pointer at `local_point`: the face
// leans toward it in proportion to its distance from the card's centre
// (hand), or rises while it is over the card (page).
void BusinessCard::follow_pointer(Point local_point) {
    const Layout placed = layout();
    const bool over = placed.card.contains(local_point);
    Point tilt{};
    if (over && !reduced_motion()) {
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
    const bool vendor = placed.vendor_name.contains(local_point);
    const bool website = presentation_ == BusinessCardPresentation::hand && placed.website.contains(local_point);
    const bool close = placed.close_mark.contains(local_point);
    const bool changed = tilt.x != tilt_.x || tilt.y != tilt_.y || vendor != vendor_hot_ ||
                         website != website_hot_ || close != close_hot_;
    tilt_ = tilt;
    vendor_hot_ = vendor;
    website_hot_ = website;
    close_hot_ = close;
    if (changed) invalidate(Dirty::paint);
}

void BusinessCard::on_mouse_move(MouseEventArgs& arguments) {
    follow_pointer(arguments.location);
    Control::on_mouse_move(arguments);
}

void BusinessCard::on_mouse_leave() {
    const bool changed = tilt_.x != 0.0 || tilt_.y != 0.0 || vendor_hot_ || website_hot_ || close_hot_;
    tilt_ = Point{};
    vendor_hot_ = false;
    website_hot_ = false;
    close_hot_ = false;
    press_target_ = PressTarget::none;
    if (changed) invalidate(Dirty::paint);
    Control::on_mouse_leave();
}

// The gesture: D held and a secondary press on the vendor name toggles the
// diagnostic overlay. The card repaints, so the window presents again and
// shows the overlay's new state.
void BusinessCard::on_mouse_down(MouseEventArgs& arguments) {
    const CardHandlerScope running;
    if (arguments.button == PointerButton::secondary && layout().vendor_name.contains(arguments.location)) {
        const SurfaceRoot* const root = attached_surface_root();
        if (root != nullptr && (*root).is_key_down(PhysicalKey::d)) {
            static_cast<void>(toggle_diagnostic_overlay());
            invalidate(Dirty::paint);
            arguments.handled = true;
        }
    } else if (arguments.button == PointerButton::primary) {
        press_target_ = target_at(arguments.location);
        if (press_target_ != PressTarget::none) {
            invalidate(Dirty::paint);
            arguments.handled = true;
        }
    }
    Control::on_mouse_down(arguments);
}

void BusinessCard::on_mouse_up(MouseEventArgs& arguments) {
    const CardHandlerScope running;
    if (arguments.button == PointerButton::primary && press_target_ != PressTarget::none) {
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
    if (presentation_ == BusinessCardPresentation::hand && event.physical_key == PhysicalKey::escape) {
        event.handled = true;
        put_self_away();
        return;
    }
    const bool activation = event.physical_key == PhysicalKey::enter || event.physical_key == PhysicalKey::space;
    if (!activation) return;
    event.handled = true;
    if (presentation_ == BusinessCardPresentation::page) {
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

void BusinessCard::ask_for_website() {
    if (program_identity().website.empty()) return;
    BusinessCardRequest request{};
    website_requested_.emit(request);
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
    put_away_.emit();
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
        if (!placed.remark.empty()) record.remark = PartGeometry::from_bounds(offset_rect(placed.remark, tilt_));
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
    static void take_remark(BusinessCard& card, std::string remark) { card.remark_ = std::move(remark); }
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
    // The remark is chosen each time the card comes out.
    std::string remark{};
    const BusinessCardRemarkSource source = business_card_remark_source();
    if (source.write_remark != nullptr) source.write_remark(source.context, remark);
    remark.resize(whole_prefix(remark, maximum_business_card_remark));
    if (!validate_utf8(remark).valid()) remark.clear();
    BusinessCardShowing::take_remark(*record.card, std::move(remark));
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
