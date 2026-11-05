// TokenBox: an editor whose accepted entries become removable tokens. The
// main sequence: the editor's events (committed, a delimiter in its text, an
// accepted completion) call accept_entry; layout_for places the tokens and
// the editor's field, and every reader of geometry goes through it;
// on_key_preview and on_pointer select and remove tokens.

#include "gui_forms/controls/panel/token_box/token_box.hpp"

#include "../input_control_utilities.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace input_control_detail;

namespace {

// Logical units, before the text scale: the box's inner padding, a token's
// height, the pitch of its lines, the gap between tokens, the label's
// leading padding, the close glyph's side and the gap before it, and the
// narrowest field the editor keeps on a line.
constexpr double box_padding = 3.0;
constexpr double token_height = 22.0;
constexpr double line_pitch = 26.0;
constexpr double token_gap = 4.0;
constexpr double label_padding = 7.0;
constexpr double close_side = 14.0;
constexpr double close_gap = 3.0;
constexpr double minimum_field_width = 60.0;

bool space(char byte) noexcept {
    const bool result = byte == ' ';
    return result;
}

// The text without its leading and trailing ASCII spaces.
std::string_view trimmed(std::string_view text) noexcept {
    std::size_t first = 0U;
    while (first < text.size() && space(text[first])) ++first;
    std::size_t last = text.size();
    while (last > first && space(text[last - 1U])) --last;
    const std::string_view result = text.substr(first, last - first);
    return result;
}

bool admitted_delimiter(char byte) noexcept {
    const unsigned char value = static_cast<unsigned char>(byte);
    const bool punctuation = value >= 0x21U && value <= 0x7EU &&
        !((value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') ||
          (value >= 'a' && value <= 'z'));
    const bool result = punctuation || byte == ' ';
    return result;
}

} // namespace

TokenBox::TokenBox(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::control);
    cursor = CursorKind::text;
}

TokenBox::~TokenBox() {
    editor_commit_.disconnect();
    editor_text_.disconnect();
    editor_completion_.disconnect();
    editor_focus_.disconnect();
}

void TokenBox::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    editor_ = make_control<TextBox>(StableId(prefix + ".editor"));
    (*editor_).set_border_style(BorderStyle::none);
    (*editor_).set_font(font_);
    (*editor_).set_auto_complete_anchor(shared_from_this());
    // Enter turns the typed text into a token (editor_committed); with
    // nothing typed it goes on to the window's accept button (W decision 6),
    // so the editor accepts Enter only while it holds an entry.
    (*editor_).set_accepts_enter(false);
    add_child(editor_);
    editor_commit_ = (*editor_).committed().subscribe(*this,
        Delegate<const std::string&>::bind<TokenBox, &TokenBox::editor_committed>(*this));
    editor_text_ = (*editor_).text_changed().subscribe(*this,
        Delegate<const std::string&>::bind<TokenBox, &TokenBox::editor_text_changed>(*this));
    editor_completion_ = (*editor_).auto_complete_accepted().subscribe(*this,
        Delegate<const std::string&>::bind<TokenBox, &TokenBox::editor_completion_accepted>(*this));
    editor_focus_ = (*editor_).focus_observed().subscribe(*this,
        Delegate<bool>::bind<TokenBox, &TokenBox::editor_focus_changed>(*this));
}

// -- tokens ------------------------------------------------------------------

TokenId TokenBox::add_token(std::string text) {
    require_mutable();
    if (!validate_utf8(text).valid()) {
        throw std::invalid_argument("TokenBox token text must be valid UTF-8");
    }
    const std::string_view kept = trimmed(text);
    if (kept.empty()) {
        throw std::invalid_argument("TokenBox token text may not be empty");
    }
    const TokenId id{next_id_};
    ++next_id_;
    Token token{.id = id, .text = std::string(kept)};
    tokens_.push_back(token);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    publish_change(token_added_, token);
    return id;
}

std::optional<std::size_t> TokenBox::token_index(TokenId id) const noexcept {
    for (std::size_t index = 0U; index < tokens_.size(); ++index) {
        if (tokens_[index].id == id) return index;
    }
    return std::nullopt;
}

bool TokenBox::remove_token(TokenId id) {
    require_mutable();
    const std::optional<std::size_t> index = token_index(id);
    if (!index) return false;
    remove_at(*index);
    return true;
}

void TokenBox::remove_at(std::size_t index) {
    const Token removed = tokens_[index];
    tokens_.erase(tokens_.begin() + static_cast<std::ptrdiff_t>(index));
    if (hot_ == removed.id) hot_.reset();
    if (pressed_close_ == removed.id) pressed_close_.reset();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    if (selected_ == removed.id) {
        set_selected(std::nullopt);
        if (!is_alive()) return;
    }
    publish_change(token_removed_, removed);
}

void TokenBox::clear_tokens() {
    require_mutable();
    while (!tokens_.empty() && is_alive()) remove_at(tokens_.size() - 1U);
}

void TokenBox::select_token(std::optional<TokenId> id) {
    require_mutable();
    if (id && !token_index(*id)) {
        throw std::invalid_argument("TokenBox can select only one of its own tokens");
    }
    set_selected(id);
}

void TokenBox::set_selected(std::optional<TokenId> id) {
    if (selected_ == id) return;
    selected_ = id;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(selected_token_changed_, selected_);
}

void TokenBox::set_delimiters(std::string delimiters) {
    require_mutable();
    for (const char byte : delimiters) {
        if (!admitted_delimiter(byte)) {
            throw std::invalid_argument("TokenBox delimiters must be ASCII punctuation or the space");
        }
    }
    delimiters_ = std::move(delimiters);
}

void TokenBox::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) {
        throw std::invalid_argument("TokenBox font specification is invalid");
    }
    set_font_from_code(true);
    if (font_ == font) return;
    font_ = font;
    if (editor_) (*editor_).set_font(font);
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

AutoCompleteMode TokenBox::auto_complete_mode() const noexcept {
    return (*editor_).auto_complete_mode();
}

void TokenBox::set_auto_complete_mode(AutoCompleteMode mode) {
    require_mutable();
    (*editor_).set_auto_complete_mode(mode);
}

AutoCompleteSource TokenBox::auto_complete_source() const noexcept {
    return (*editor_).auto_complete_source();
}

void TokenBox::set_auto_complete_source(AutoCompleteSource source) {
    require_mutable();
    (*editor_).set_auto_complete_source(source);
}

const std::shared_ptr<AutoCompleteStringCollection>& TokenBox::auto_complete_custom_source() const noexcept {
    return (*editor_).auto_complete_custom_source();
}

void TokenBox::set_auto_complete_custom_source(std::shared_ptr<AutoCompleteStringCollection> source) {
    require_mutable();
    (*editor_).set_auto_complete_custom_source(std::move(source));
}

// -- the editor's events -----------------------------------------------------

void TokenBox::accept_entry(std::string_view entry) {
    const std::string_view kept = trimmed(entry);
    if (kept.empty() || !validate_utf8(kept).valid()) return;
    const std::string text(kept);
    synchronizing_ = true;
    (*editor_).set_text(std::string{});
    synchronizing_ = false;
    static_cast<void>(add_token(text));
}

void TokenBox::split_delimited(const std::string& text) {
    // Every delimited piece but the last becomes a token; the last stays in
    // the editor, still being typed.
    std::vector<std::string> pieces;
    std::size_t start = 0U;
    for (std::size_t index = 0U; index < text.size(); ++index) {
        if (delimiters_.find(text[index]) == std::string::npos) continue;
        pieces.emplace_back(text, start, index - start);
        start = index + 1U;
    }
    std::string rest(text, start, std::string::npos);
    synchronizing_ = true;
    (*editor_).set_text(std::move(rest));
    synchronizing_ = false;
    for (const std::string& piece : pieces) {
        const std::string_view kept = trimmed(piece);
        if (kept.empty()) continue;
        static_cast<void>(add_token(std::string(kept)));
        if (!is_alive()) return;
    }
}

void TokenBox::editor_committed(const std::string& text) {
    accept_entry(text);
}

void TokenBox::editor_completion_accepted(const std::string& text) {
    accept_entry(text);
}

void TokenBox::editor_text_changed(const std::string& text) {
    // An entry of only spaces would make no token: Enter passes on then too.
    const bool entry = !trimmed(text).empty();
    if ((*editor_).accepts_enter() != entry) (*editor_).set_accepts_enter(entry);
    if (synchronizing_) return;
    // Typing goes back to the editor from a selected token.
    if (selected_) {
        set_selected(std::nullopt);
        if (!is_alive()) return;
    }
    // Any delimiter in the text (none without delimiters) splits it.
    const bool delimited = text.find_first_of(delimiters_) != std::string::npos;
    if (delimited) split_delimited(text);
}

void TokenBox::editor_focus_changed(bool focused) {
    editor_focused_ = focused;
    // A selected token is a keyboard position in the editor's focus.
    if (!focused && selected_) set_selected(std::nullopt);
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- layout ------------------------------------------------------------------

TokenBox::Layout TokenBox::layout_for(double width, double height) const {
    Layout placed;
    const double scale = effective_text_scale();
    const double padding = box_padding;
    const double face_height = token_height * scale;
    const double pitch = line_pitch * scale;
    const double inner_right = std::max(padding, width - padding);
    const FontSpec font = text_font(font_);
    double x = padding;
    double y = padding;
    placed.tokens.reserve(tokens_.size());
    for (std::size_t index = 0U; index < tokens_.size(); ++index) {
        const ResolvedTextLayout text = resolve_text_layout_utf8(tokens_[index].text, font);
        const double label_width = std::ceil(text.logical_size.width);
        const double face_width = label_padding + label_width + close_gap + close_side * scale + close_gap;
        // A token that does not fit the rest of the line starts the next
        // one, unless it is the first on its line.
        if (x + face_width > inner_right && x > padding) {
            x = padding;
            y += pitch;
        }
        TokenPlacement placement;
        placement.index = index;
        placement.face = Rect{x, y, std::min(face_width, inner_right - x), face_height};
        placement.label = Rect{x + label_padding, y, label_width, face_height};
        const double glyph = close_side * scale;
        placement.close = Rect{x + label_padding + label_width + close_gap,
                               y + (face_height - glyph) * 0.5, glyph, glyph};
        // Only tokens whose line fits the box are laid out.
        if (y + face_height <= height - padding + 0.5) placed.tokens.push_back(placement);
        x += face_width + token_gap;
    }
    if (inner_right - x < minimum_field_width && x > padding) {
        x = padding;
        y += pitch;
    }
    placed.field = Rect{x, y, std::max(0.0, inner_right - x), face_height};
    placed.height = y + face_height + padding;
    return placed;
}

TokenBox::Layout TokenBox::layout() const {
    const Rect bounds = client_rectangle();
    const Layout placed = layout_for(bounds.width, bounds.height);
    return placed;
}

Size TokenBox::measure(Size available) {
    const double width = std::isfinite(available.width) ? available.width : 240.0;
    const Layout placed = layout_for(width, std::numeric_limits<double>::infinity());
    const Size desired{width, placed.height};
    return desired;
}

void TokenBox::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    if (!editor_) return;
    const Layout placed = layout_for(final_bounds.width, final_bounds.height);
    set_child_layout(editor_, placed.field);
}

std::string TokenBox::token_stable_id(TokenId id) const {
    const std::string result = std::string(stable_id().value()) + ".token." + std::to_string(id.value);
    return result;
}

bool TokenBox::focus_visible() const noexcept {
    const Window* const host = window();
    const bool result = editor_focused_ && host != nullptr && (*host).focus_cue_visible();
    return result;
}

AnatomyStateSet TokenBox::token_states(std::size_t index) const {
    AnatomyStateSet states;
    const TokenId id = tokens_[index].id;
    if (selected_ == id) {
        states.insert(AnatomyState::selected);
        if (focus_visible()) states.insert(AnatomyState::focus_visible);
    }
    if (hot_ == id) states.insert(AnatomyState::hot);
    return states;
}

// -- paint -------------------------------------------------------------------

void TokenBox::on_paint(Painter& painter, Rect) {
    const Rect bounds = client_rectangle();
    AnatomyStateSet box_states;
    if (focus_visible()) box_states.insert(AnatomyState::focus_visible);
    PartPaint well{.role = PartRole::well, .bounds = bounds, .states = box_states,
                   .family = ControlVisualRole::editor};
    paint_part(painter, *this, well);
    const Layout placed = layout();
    const FontSpec font = text_font(font_);
    for (const TokenPlacement& placement : placed.tokens) {
        const Token& token = tokens_[placement.index];
        const AnatomyStateSet states = token_states(placement.index);
        // No vocabulary atom names a token: its plate is a chip of the
        // choice family, the family whose selected state marks a chosen
        // item (an owner vocabulary decision).
        PartPaint part{.role = PartRole::chip, .bounds = placement.face, .states = states,
                       .family = ControlVisualRole::choice};
        paint_part(painter, *this, part);
        part.role = PartRole::label;
        part.bounds = placement.label;
        part.text = token.text;
        part.font = font;
        paint_part(painter, *this, part);
        part.text = {};
        part.role = PartRole::glyph;
        part.bounds = placement.close;
        part.mark = PartMark::close;
        AnatomyStateSet close_states;
        if (hot_ == token.id && hot_close_) close_states.insert(AnatomyState::hot);
        if (pressed_close_ == token.id) close_states.insert(AnatomyState::pressed);
        part.states = close_states;
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
        part.role = PartRole::focus;
        part.bounds = placement.face;
        part.states = states;
        paint_part(painter, *this, part);
    }
    PartPaint focus{.role = PartRole::focus, .bounds = bounds, .states = box_states,
                    .family = ControlVisualRole::editor};
    paint_part(painter, *this, focus);
}

// -- input -------------------------------------------------------------------

TokenBox::TokenHit TokenBox::hit_token(Point window_point) const {
    TokenHit hit;
    const Rect absolute = absolute_bounds();
    const Point local{window_point.x - absolute.x, window_point.y - absolute.y};
    const Layout placed = layout();
    for (const TokenPlacement& placement : placed.tokens) {
        if (!placement.face.contains(local)) continue;
        hit.index = placement.index;
        hit.close = placement.close.contains(local);
        return hit;
    }
    return hit;
}

void TokenBox::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    const TokenHit hit = hit_token(event.position);
    if (event.action == PointerAction::move || event.action == PointerAction::leave) {
        std::optional<TokenId> hot{};
        if (event.action == PointerAction::move && hit.index) hot = tokens_[*hit.index].id;
        const bool close = event.action == PointerAction::move && hit.close;
        if (hot != hot_ || close != hot_close_) {
            hot_ = hot;
            hot_close_ = close;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        Window* const host = window();
        if (host != nullptr) static_cast<void>((*host).request_focus(editor_));
        if (hit.index && hit.close) {
            // The close glyph acts on release over it, as a button does.
            pressed_close_ = tokens_[*hit.index].id;
            set_pointer_capture(true);
            invalidate(Dirty::paint);
        } else if (hit.index) {
            set_selected(tokens_[*hit.index].id);
        } else {
            set_selected(std::nullopt);
        }
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::up && pressed_close_) {
        const TokenId pressed = *pressed_close_;
        pressed_close_.reset();
        set_pointer_capture(false);
        invalidate(Dirty::paint);
        if (hit.index && hit.close && tokens_[*hit.index].id == pressed) {
            static_cast<void>(remove_token(pressed));
        }
        event.handled = true;
    }
}

void TokenBox::on_key_preview(KeyEvent& event) {
    if (!editor_ || !enabled.type() || event.action != KeyAction::down) return;
    // The editor's suggestion list has the keys while it is open.
    if ((*editor_).auto_complete_open()) return;
    const std::uint32_t key = event.physical_key;
    if (event.modifiers != Modifier::none) return;
    if (selected_) {
        const std::optional<std::size_t> index = token_index(*selected_);
        if (!index) {
            set_selected(std::nullopt);
            return;
        }
        if (key == PhysicalKey::backspace || key == PhysicalKey::delete_forward) {
            remove_at(*index);
            event.handled = true;
        } else if (key == PhysicalKey::left) {
            const std::size_t previous = *index == 0U ? 0U : *index - 1U;
            set_selected(tokens_[previous].id);
            event.handled = true;
        } else if (key == PhysicalKey::right) {
            // Past the last token the editor has the caret again.
            if (*index + 1U < tokens_.size()) {
                set_selected(tokens_[*index + 1U].id);
            } else {
                set_selected(std::nullopt);
            }
            event.handled = true;
        } else if (key == PhysicalKey::home) {
            set_selected(tokens_.front().id);
            event.handled = true;
        } else if (key == PhysicalKey::end || key == PhysicalKey::escape) {
            set_selected(std::nullopt);
            event.handled = true;
        }
        return;
    }
    const TextSelection caret = (*editor_).selection();
    const bool at_start = caret.empty() && caret.caret == Utf8Offset(0U);
    if (at_start && !tokens_.empty() && (key == PhysicalKey::backspace || key == PhysicalKey::left)) {
        // Backspace or Left at the start selects the last token; a second
        // Backspace removes it.
        set_selected(tokens_.back().id);
        event.handled = true;
    }
}

void TokenBox::on_detached_from_window() noexcept {
    hot_.reset();
    hot_close_ = false;
    pressed_close_.reset();
    editor_focused_ = false;
}

// -- semantics ---------------------------------------------------------------

SemanticDescriptor TokenBox::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    // No semantic role names a token field: it is published as the list of
    // its tokens, with the editor as a child (an owner vocabulary decision).
    descriptor.role = SemanticRole::list;
    descriptor.name = accessible_name.type();
    descriptor.description = accessible_description.type();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> TokenBox::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes;
    const Rect absolute = absolute_bounds();
    const Layout placed = layout();
    nodes.reserve(placed.tokens.size());
    for (const TokenPlacement& placement : placed.tokens) {
        const Token& token = tokens_[placement.index];
        SemanticNode node;
        node.stable_id = token_stable_id(token.id);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::list_item;
        node.name = token.text;
        node.position_in_set = placement.index + 1U;
        node.set_size = tokens_.size();
        node.bounds = Rect{absolute.x + placement.face.x, absolute.y + placement.face.y,
                           placement.face.width, placement.face.height};
        node.states = SemanticState::visible;
        if (effectively_enabled()) node.states |= SemanticState::enabled;
        if (selected_ == token.id) node.states |= SemanticState::selected;
        node.actions = {SemanticAction::select};
        // The close glyph is a button of its own, so a token can be removed
        // without the keyboard.
        SemanticNode remove;
        remove.stable_id = node.stable_id + ".remove";
        remove.runtime_id = virtual_semantic_runtime_id(remove.stable_id);
        remove.role = SemanticRole::button;
        remove.name = "Remove " + token.text;
        remove.bounds = Rect{absolute.x + placement.close.x, absolute.y + placement.close.y,
                             placement.close.width, placement.close.height};
        remove.states = SemanticState::visible;
        if (effectively_enabled()) remove.states |= SemanticState::enabled;
        remove.actions = {SemanticAction::press};
        node.children.push_back(std::move(remove));
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool TokenBox::on_semantic_action(SemanticAction action, std::string_view value) {
    Window* const host = window();
    if (action == SemanticAction::focus && editor_ && host != nullptr) {
        const bool focused = (*host).request_focus(editor_);
        return focused;
    }
    const bool handled = Control::on_semantic_action(action, value);
    return handled;
}

bool TokenBox::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view) {
    for (const Token& token : tokens_) {
        const std::string node = token_stable_id(token.id);
        if (id == node && action == SemanticAction::select) {
            set_selected(token.id);
            return true;
        }
        if (id == node + ".remove" && action == SemanticAction::press) {
            const TokenId removed = token.id;
            static_cast<void>(remove_token(removed));
            return true;
        }
    }
    return false;
}

// -- anatomy -----------------------------------------------------------------

namespace {

constexpr std::array<AnatomyState, 2> close_part_states{{AnatomyState::hot, AnatomyState::pressed}};

constexpr std::array<PartField<TokenBox::Parts>, 4> token_box_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &TokenBox::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &TokenBox::Parts::well},
    {.declaration = {.name = "field", .role = PartRole::well, .composed_from = "text_box"},
     .member = &TokenBox::Parts::field},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &TokenBox::Parts::focus},
}};

constexpr std::array<PartField<TokenBox::ItemParts>, 4> token_item_part_fields{{
    {.declaration = {.name = "token_face", .role = PartRole::chip, .composed_from = {}},
     .member = &TokenBox::ItemParts::token_face},
    {.declaration = {.name = "token_label", .role = PartRole::label, .composed_from = {}},
     .member = &TokenBox::ItemParts::token_label},
    {.declaration = {.name = "token_close", .role = PartRole::glyph, .composed_from = {},
                     .states = close_part_states},
     .member = &TokenBox::ItemParts::token_close},
    {.declaration = {.name = "token_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &TokenBox::ItemParts::token_focus},
}};

constexpr std::array<StateField<TokenBox::ItemStates>, 3> token_item_state_fields{{
    {.state = AnatomyState::selected, .member = &TokenBox::ItemStates::selected},
    {.state = AnatomyState::hot, .member = &TokenBox::ItemStates::hot},
    {.state = AnatomyState::focus_visible, .member = &TokenBox::ItemStates::focus_visible},
}};

constexpr std::array<StateField<TokenBox::States>, 5> token_box_state_fields{{
    {.state = AnatomyState::normal, .member = &TokenBox::States::normal},
    {.state = AnatomyState::enabled, .member = &TokenBox::States::enabled},
    {.state = AnatomyState::disabled, .member = &TokenBox::States::disabled},
    {.state = AnatomyState::focused, .member = &TokenBox::States::focused},
    {.state = AnatomyState::focus_visible, .member = &TokenBox::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 4> token_box_parts = part_declarations(token_box_part_fields);
constexpr std::array<PartDeclaration, 4> token_item_parts = part_declarations(token_item_part_fields);
constexpr std::array<AnatomyState, 3> token_item_states = state_declarations(token_item_state_fields);
constexpr std::array<AnatomyState, 5> token_box_states = state_declarations(token_box_state_fields);

constexpr std::array<ItemSystemDeclaration, 1> token_box_item_systems{{
    {.name = "token", .parts = token_item_parts, .states = token_item_states, .custom_states = {}},
}};

constexpr ControlAnatomy token_box_anatomy{
    .control_id = "token_box",
    .parts = token_box_parts,
    .states = token_box_states,
    .custom_states = {},
    .item_systems = token_box_item_systems,
};

} // namespace

const ControlAnatomy& TokenBox::anatomy() const noexcept {
    return token_box_anatomy;
}

AnatomyStateSet TokenBox::anatomy_states() const {
    AnatomyStateSet states = Control::anatomy_states();
    // The editor's focus is the box's.
    if (editor_focused_) {
        states.insert(AnatomyState::focused);
        if (focus_visible()) states.insert(AnatomyState::focus_visible);
    }
    return states;
}

void TokenBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = client_rectangle();
    const Layout placed = layout();
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.well = PartGeometry::from_bounds(bounds);
    record.field = PartGeometry::from_bounds(placed.field);
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), token_box_part_fields, record, geometry);
}

TokenBox::Parts TokenBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), token_box_part_fields, published.parts(), record);
    return record;
}

TokenBox::States TokenBox::states() const {
    States record;
    read_states(token_box_state_fields, anatomy_states(), record);
    return record;
}

std::size_t TokenBox::realized_item_count() const {
    const Layout placed = layout();
    const std::size_t count = placed.tokens.size();
    return count;
}

std::string TokenBox::realized_item_stable_id(std::size_t item) const {
    const Layout placed = layout();
    if (item >= placed.tokens.size()) {
        throw std::out_of_range("TokenBox realized token is past the laid-out tokens");
    }
    const std::string result = token_stable_id(tokens_[placed.tokens[item].index].id);
    return result;
}

AnatomyStateSet TokenBox::realized_item_anatomy_states(std::size_t item) const {
    const Layout placed = layout();
    if (item >= placed.tokens.size()) {
        throw std::out_of_range("TokenBox realized token is past the laid-out tokens");
    }
    const AnatomyStateSet states = token_states(placed.tokens[item].index);
    return states;
}

void TokenBox::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const Layout placed = layout();
    const TokenPlacement& placement = placed.tokens[item];
    const Token& token = tokens_[placement.index];
    ItemParts record;
    record.token_face = PartGeometry::from_bounds(placement.face);
    record.token_label = PartGeometry::from_bounds(placement.label);
    record.token_close = PartGeometry::from_bounds(placement.close);
    if (hot_ == token.id && hot_close_) record.token_close.states.insert(AnatomyState::hot);
    if (pressed_close_ == token.id) record.token_close.states.insert(AnatomyState::pressed);
    record.token_focus = PartGeometry::from_bounds(placement.face);
    write_item_parts(anatomy().item_systems.front(), token_item_part_fields, record, geometry);
}

std::vector<TokenBox::Item> TokenBox::realized_items() const {
    const Layout placed = layout();
    std::vector<Item> records;
    records.reserve(placed.tokens.size());
    for (std::size_t item = 0U; item < placed.tokens.size(); ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        const std::size_t index = placed.tokens[item].index;
        Item record;
        record.id = tokens_[index].id;
        record.index = index;
        record.stable_id = token_stable_id(record.id);
        read_item_parts(anatomy().item_systems.front(), token_item_part_fields, published.parts(),
                        record.parts);
        read_states(token_item_state_fields, token_states(index), record.states);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events TokenBox adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 3> token_box_interactions{{
    {.name = "token_added", .payload = "Token", .mutable_payload = false},
    {.name = "token_removed", .payload = "Token", .mutable_payload = false},
    {.name = "selected_token_changed", .payload = "std::optional<TokenId>", .mutable_payload = false},
}};

} // namespace

void TokenBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), token_box_interactions.begin(), token_box_interactions.end());
}

} // namespace gui_forms
