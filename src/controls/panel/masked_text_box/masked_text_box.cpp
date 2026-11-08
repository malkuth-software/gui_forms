#include "gui_forms/controls/panel/masked_text_box/masked_text_box.hpp"

#include "../input_control_utilities.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// The byte length of the UTF-8 scalar starting at `index` in text that
// validate_utf8 has accepted.
std::size_t scalar_length(std::string_view text, std::size_t index) noexcept {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    if (lead >= 0xF0U) return 4U;
    if (lead >= 0xE0U) return 3U;
    if (lead >= 0xC0U) return 2U;
    return 1U;
}

// Splits valid UTF-8 into one view per scalar; the views borrow `text`.
void split_scalars(std::string_view text, std::vector<std::string_view>& pieces) {
    pieces.clear();
    std::size_t index = 0U;
    while (index < text.size()) {
        const std::size_t length = scalar_length(text, index);
        pieces.push_back(text.substr(index, length));
        index += length;
    }
}

bool valid_format(MaskTextFormat format) noexcept {
    const bool result = format == MaskTextFormat::exclude_prompt_and_literals ||
                        format == MaskTextFormat::include_prompt ||
                        format == MaskTextFormat::include_literals ||
                        format == MaskTextFormat::include_prompt_and_literals;
    return result;
}

} // namespace

MaskedTextBox::MaskedTextBox(StableId stable_id, std::string initial_mask)
    : Panel(std::move(stable_id)), provider_(std::move(initial_mask)) {
    set_tool_tip_presentation(ToolTipPresentation::field_hint);
    border_style = BorderStyle::none;
    published_text_ = text();
}

MaskedTextBox::~MaskedTextBox() {
    // The editor may outlive this box (anyone may hold editor()); its filter
    // must not call into a destroyed box.
    if (editor_) (*editor_).set_edit_filter(Delegate<TextEdit&>{});
}

void MaskedTextBox::EditorChangeCallback::operator()(const std::string& text) const {
    const std::shared_ptr<MaskedTextBox> box = target.lock();
    if (box) (*box).editor_text_changed(text);
}

void MaskedTextBox::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    editor_ = make_control<TextBox>(StableId(prefix + ".editor"), provider_.display_text());
    (*editor_).set_edit_filter(
        Delegate<TextEdit&>::bind<MaskedTextBox, &MaskedTextBox::filter_edit>(*this));
    add_child(editor_);
    place_caret();
    const std::weak_ptr<MaskedTextBox> weak =
        std::static_pointer_cast<MaskedTextBox>(shared_from_this());
    editor_change_ = (*editor_).text_changed().subscribe(*this, EditorChangeCallback{weak});
    validating_ = validating().subscribe(
        *this, Delegate<ControlValidationEvent&>::bind<MaskedTextBox, &MaskedTextBox::on_validating>(*this));
}

// -- the edit filter -----------------------------------------------------------

void MaskedTextBox::filter_edit(TextEdit& edit) {
    pending_.reset();
    // An empty mask takes any text.
    if (provider_.length() == 0U) return;
    const MaskPosition start = provider_.display_position(edit.start);
    const MaskPosition end = provider_.display_position(edit.end);
    switch (edit.kind) {
    case TextEditKind::typing:
        filter_typing(edit, start, end);
        return;
    case TextEditKind::deletion:
        filter_deletion(edit, start, end);
        return;
    case TextEditKind::paste:
        filter_paste(edit, start, end);
        return;
    case TextEditKind::value:
        filter_value(edit);
        return;
    }
    edit.verdict = TextEditVerdict::refuse;
}

void MaskedTextBox::filter_typing(TextEdit& edit, MaskPosition start, MaskPosition end) {
    MaskedTextProvider candidate = provider_;
    MaskEditResult result;
    if (start != end) {
        result = candidate.replace_range(edit.replacement, start, end);
    } else if (overwrite_mode.read()) {
        result = candidate.replace_at(edit.replacement, start);
    } else {
        result = candidate.insert_at(edit.replacement, start);
    }
    if (!result.succeeded()) {
        edit.verdict = TextEditVerdict::refuse;
        reject(result, edit.replacement);
        return;
    }
    const MaskPosition caret = caret_after(candidate, result.position);
    propose(edit, std::move(candidate), caret);
}

void MaskedTextBox::filter_deletion(TextEdit& edit, MaskPosition start, MaskPosition end) {
    bool removes_edit_position = false;
    for (std::size_t index = start.value(); index < end.value(); ++index) {
        if (provider_.is_edit_position(MaskPosition(index))) {
            removes_edit_position = true;
            break;
        }
    }
    if (!removes_edit_position) {
        // A literal cannot be removed: Backspace (the caret was at the end of
        // the range) moves the caret back over it, Delete moves it past.
        const TextSelection selection = (*editor_).selection();
        const bool backward = selection.empty() && selection.caret == edit.end;
        const MaskPosition caret = backward ? start : end;
        propose(edit, provider_, caret);
        return;
    }
    MaskedTextProvider candidate = provider_;
    const MaskEditResult result = candidate.remove_range(start, end);
    if (!result.succeeded()) {
        edit.verdict = TextEditVerdict::refuse;
        reject(result, {});
        return;
    }
    propose(edit, std::move(candidate), start);
}

void MaskedTextBox::filter_paste(TextEdit& edit, MaskPosition start, MaskPosition end) {
    MaskedTextProvider candidate = provider_;
    if (reject_input_on_first_failure.read()) {
        const MaskEditResult result = candidate.replace_range(edit.replacement, start, end);
        if (!result.succeeded()) {
            edit.verdict = TextEditVerdict::refuse;
            reject(result, edit.replacement);
            return;
        }
        const MaskPosition caret = caret_after(candidate, result.position);
        propose(edit, std::move(candidate), caret);
        return;
    }
    const MaskEditResult removed = candidate.remove_range(start, end);
    if (!removed.succeeded()) {
        edit.verdict = TextEditVerdict::refuse;
        reject(removed, edit.replacement);
        return;
    }
    // Each character the mask takes is placed; each one it refuses is
    // reported and dropped, and the next character tries the same position.
    std::vector<std::string_view> pieces;
    split_scalars(edit.replacement, pieces);
    MaskPosition next = start;
    std::optional<MaskPosition> last{};
    for (const std::string_view piece : pieces) {
        MaskEditResult placed;
        if (overwrite_mode.read()) {
            placed = candidate.replace_at(piece, next);
        } else {
            placed = candidate.insert_at(piece, next);
        }
        if (!placed.succeeded()) {
            reject(placed, piece);
            continue;
        }
        last = placed.position;
        next = MaskPosition(placed.position.value() + 1U);
    }
    const MaskPosition caret = last ? caret_after(candidate, *last) : start;
    propose(edit, std::move(candidate), caret);
}

void MaskedTextBox::filter_value(TextEdit& edit) {
    MaskedTextProvider candidate = provider_;
    const MaskEditResult result = candidate.set_text(edit.replacement);
    if (!result.succeeded()) {
        edit.verdict = TextEditVerdict::refuse;
        reject(result, edit.replacement);
        return;
    }
    const MaskPosition caret(candidate.length());
    propose(edit, std::move(candidate), caret);
}

void MaskedTextBox::reject(MaskEditResult result, std::string_view text) {
    const MaskInputRejection rejection{result.position, result.hint, std::string(text)};
    mask_input_rejected_.emit(rejection);
}

void MaskedTextBox::propose(TextEdit& edit, MaskedTextProvider candidate, MaskPosition caret) {
    edit.verdict = TextEditVerdict::rewrite;
    edit.rewritten_text = candidate.display_text();
    edit.rewritten_caret = candidate.display_offset(caret);
    pending_ = std::move(candidate);
}

MaskPosition MaskedTextBox::caret_after(const MaskedTextProvider& candidate, MaskPosition last) const {
    const MaskPosition after(last.value() + 1U);
    const std::optional<MaskPosition> edit = candidate.find_edit_position(after, true);
    if (edit) return *edit;
    const MaskPosition end(candidate.length());
    return end;
}

// -- synchronizing the editor and the provider ---------------------------------

void MaskedTextBox::editor_text_changed(const std::string& display) {
    if (synchronizing_) return;
    if (pending_ && (*pending_).display_text() == display) {
        provider_ = std::move(*pending_);
    } else {
        adopt_display_text(display);
    }
    pending_.reset();
    publish_text_if_changed();
}

void MaskedTextBox::adopt_display_text(std::string_view display) {
    if (provider_.length() == 0U) return;
    std::vector<std::string_view> pieces;
    split_scalars(display, pieces);
    if (pieces.size() != provider_.length()) return;
    MaskedTextProvider candidate = provider_;
    candidate.clear();
    // A prompt shown in the field is an unassigned position.
    const std::string prompt = input_control_detail::utf8_scalar(provider_.prompt_character());
    for (std::size_t index = 0U; index < pieces.size(); ++index) {
        const MaskPosition position(index);
        if (!candidate.is_edit_position(position) || pieces[index] == prompt) continue;
        const MaskEditResult placed = candidate.replace_at(pieces[index], position);
        if (!placed.succeeded()) return;
    }
    provider_ = std::move(candidate);
}

void MaskedTextBox::show_provider() {
    // Under an empty mask the editor's text is the text; there is nothing to show.
    if (editor_ && provider_.length() != 0U) {
        synchronizing_ = true;
        (*editor_).text = provider_.display_text();
        synchronizing_ = false;
        place_caret();
    }
    pending_.reset();
    publish_text_if_changed();
}

void MaskedTextBox::place_caret() {
    if (!editor_ || provider_.length() == 0U) return;
    MaskPosition caret(provider_.length());
    for (std::size_t index = 0U; index < provider_.length(); ++index) {
        const MaskPosition position(index);
        if (provider_.is_edit_position(position) && !provider_.is_assigned(position)) {
            caret = position;
            break;
        }
    }
    const Utf8Offset offset = provider_.display_offset(caret);
    try {
        (*editor_).select(offset, offset);
    } catch (const std::out_of_range&) {
        // A combining character typed into a character position makes this
        // offset fall inside a grapheme; the caret then stays at the end.
    }
}

void MaskedTextBox::publish_text_if_changed() {
    std::string now = text();
    if (now == published_text_) return;
    published_text_ = std::move(now);
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(text_changed_, published_text_);
}

// -- properties ------------------------------------------------------------------

MaskedTextProvider MaskedTextBox::configured_provider(std::string next_mask, MaskCulture next_culture) const {
    MaskedTextProvider next(std::move(next_mask), std::move(next_culture));
    next.set_prompt_character(provider_.prompt_character());
    next.set_allow_prompt_as_input(provider_.allow_prompt_as_input());
    next.set_reset_on_prompt(provider_.reset_on_prompt());
    next.set_reset_on_space(provider_.reset_on_space());
    next.set_ascii_only(provider_.ascii_only());
    next.set_skip_literals(provider_.skip_literals());
    return next;
}

void MaskedTextBox::replace_provider(MaskedTextProvider next) {
    // The assigned characters, without prompts or literals, placed again;
    // under an empty mask, the plain text.
    std::string kept{};
    if (provider_.length() == 0U) {
        kept = text();
    } else {
        kept = provider_.to_string(MaskTextFormat::exclude_prompt_and_literals);
    }
    if (next.length() == 0U) {
        provider_ = std::move(next);
        if (editor_) {
            synchronizing_ = true;
            (*editor_).text = kept;
            synchronizing_ = false;
        }
        pending_.reset();
        publish_text_if_changed();
        return;
    }
    const MaskEditResult placed = next.set_text(kept);
    if (!placed.succeeded()) next.clear();
    provider_ = std::move(next);
    show_provider();
}

std::string MaskedTextBox::read_view(const decltype(mask)&) const {
    std::string current(provider_.mask());
    return current;
}

void MaskedTextBox::write_view(const decltype(mask)&, std::string incoming) {
    require_mutable();
    if (incoming == provider_.mask()) return;
    MaskedTextProvider next = configured_provider(std::move(incoming), provider_.culture());
    replace_provider(std::move(next));
    on_mask_changed();
}

MaskCulture MaskedTextBox::read_view(const decltype(culture)&) const {
    MaskCulture current = provider_.culture();
    return current;
}

void MaskedTextBox::write_view(const decltype(culture)&, MaskCulture incoming) {
    require_mutable();
    if (incoming == provider_.culture()) return;
    MaskedTextProvider next = configured_provider(std::string(provider_.mask()), std::move(incoming));
    replace_provider(std::move(next));
}

std::string MaskedTextBox::text() const {
    // Under an empty mask the editor's text is the text.
    if (provider_.length() == 0U) {
        const std::string plain = editor_ ? std::string((*editor_).text.read()) : std::string{};
        return plain;
    }
    return provider_.to_string(text_mask_format.read());
}

MaskEditResult MaskedTextBox::set_text(std::string_view text) {
    require_mutable();
    if (provider_.length() == 0U) {
        if (!validate_utf8(text).valid()) {
            const MaskEditResult invalid{MaskResultHint::invalid_input, MaskPosition(0U)};
            reject(invalid, {});
            return invalid;
        }
        if (editor_) {
            synchronizing_ = true;
            (*editor_).text = std::string(text);
            synchronizing_ = false;
        }
        publish_text_if_changed();
        const MaskEditResult accepted{MaskResultHint::success, MaskPosition(0U)};
        return accepted;
    }
    MaskedTextProvider candidate = provider_;
    const MaskEditResult result = candidate.set_text(text);
    if (!result.succeeded()) {
        reject(result, text);
        return result;
    }
    provider_ = std::move(candidate);
    show_provider();
    return result;
}

void MaskedTextBox::clear() {
    require_mutable();
    provider_.clear();
    show_provider();
}

void MaskedTextBox::prepare_write(const decltype(text_mask_format)&, MaskTextFormat& incoming) {
    require_mutable();
    if (!valid_format(incoming)) throw std::invalid_argument("MaskedTextBox text mask format is invalid");
}

void MaskedTextBox::written(const decltype(text_mask_format)&) {
    publish_text_if_changed();
}

char32_t MaskedTextBox::read_view(const decltype(prompt_character)&) const {
    const char32_t result = provider_.prompt_character();
    return result;
}

void MaskedTextBox::write_view(const decltype(prompt_character)&, char32_t incoming) {
    require_mutable();
    if (incoming == provider_.prompt_character()) return;
    provider_.set_prompt_character(incoming);
    show_provider();
}

// The provider's input settings: views; a change applies to the next input.
bool MaskedTextBox::read_view(const decltype(allow_prompt_as_input)&) const {
    const bool result = provider_.allow_prompt_as_input();
    return result;
}

void MaskedTextBox::write_view(const decltype(allow_prompt_as_input)&, bool incoming) {
    require_mutable();
    provider_.set_allow_prompt_as_input(incoming);
}

bool MaskedTextBox::read_view(const decltype(ascii_only)&) const {
    const bool result = provider_.ascii_only();
    return result;
}

void MaskedTextBox::write_view(const decltype(ascii_only)&, bool incoming) {
    require_mutable();
    provider_.set_ascii_only(incoming);
}

bool MaskedTextBox::read_view(const decltype(skip_literals)&) const {
    const bool result = provider_.skip_literals();
    return result;
}

void MaskedTextBox::write_view(const decltype(skip_literals)&, bool incoming) {
    require_mutable();
    provider_.set_skip_literals(incoming);
}

bool MaskedTextBox::read_view(const decltype(reset_on_prompt)&) const {
    const bool result = provider_.reset_on_prompt();
    return result;
}

void MaskedTextBox::write_view(const decltype(reset_on_prompt)&, bool incoming) {
    require_mutable();
    provider_.set_reset_on_prompt(incoming);
}

bool MaskedTextBox::read_view(const decltype(reset_on_space)&) const {
    const bool result = provider_.reset_on_space();
    return result;
}

void MaskedTextBox::write_view(const decltype(reset_on_space)&, bool incoming) {
    require_mutable();
    provider_.set_reset_on_space(incoming);
}

void MaskedTextBox::prepare_write(const decltype(overwrite_mode)&, bool&) {
    require_mutable();
}

void MaskedTextBox::written(const decltype(overwrite_mode)&) {
    publish_change(overwrite_mode_changed_, overwrite_mode.read());
}

void MaskedTextBox::prepare_write(const decltype(read_only)&, bool&) {
    require_mutable();
}

void MaskedTextBox::written(const decltype(read_only)&) {
    if (editor_) (*editor_).read_only = read_only.read();
    invalidate(Dirty::paint | Dirty::semantics);
    on_read_only_changed();
}

// -- validation ------------------------------------------------------------------

MaskValidation MaskedTextBox::run_validation() {
    MaskValidation validation;
    validation.text = text();
    validation.mask_completed = provider_.mask_completed();
    if (!validator_) return validation;
    validator_(validation);
    type_validation_completed_.emit(validation);
    return validation;
}

bool MaskedTextBox::validate_text() {
    require_mutable();
    const MaskValidation validation = run_validation();
    return validation.valid;
}

void MaskedTextBox::on_validating(ControlValidationEvent& event) {
    const MaskValidation validation = run_validation();
    if (!validation.valid && validation.cancel) event.cancel = true;
}

// -- layout, input and semantics ---------------------------------------------------

void MaskedTextBox::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    if (editor_) set_child_layout(editor_, {0.0, 0.0, final_bounds.width, final_bounds.height});
}

void MaskedTextBox::on_key_preview(KeyEvent& event) {
    if (!enabled.read() || event.action != KeyAction::down) return;
    if (event.physical_key == PhysicalKey::insert && event.modifiers == Modifier::none) {
        overwrite_mode = !overwrite_mode.read();
        event.handled = true;
    }
}

SemanticDescriptor MaskedTextBox::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::text_box;
    descriptor.name = accessible_name.read();
    // The value is what the field shows: prompts and literals in place.
    descriptor.value = provider_.length() == 0U ? text() : provider_.display_text();
    // The editor shows the value, so its selection and caret index it; they
    // are forwarded only while the two agree.
    if (editor_ && (*editor_).text.read() == descriptor.value) {
        const SemanticDescriptor editing = (*editor_).semantic_descriptor();
        descriptor.text_selection = editing.text_selection;
    }
    descriptor.description = accessible_description.read();
    descriptor.actions = {SemanticAction::focus};
    if (read_only.read()) {
        descriptor.states |= SemanticState::read_only;
    } else {
        descriptor.actions.push_back(SemanticAction::set_value);
    }
    descriptor.exposed = true;
    descriptor.include_descendants = false;
    return descriptor;
}

bool MaskedTextBox::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::set_value) {
        if (read_only.read()) return false;
        const MaskEditResult result = set_text(value);
        return result.succeeded();
    }
    Window* const host = window();
    if (action == SemanticAction::focus && editor_ && host != nullptr) {
        const bool focused = (*host).request_focus(editor_);
        return focused;
    }
    return Panel::on_semantic_action(action, value);
}

bool MaskedTextBox::editor_shows_published_value() const {
    if (!editor_) return false;
    const std::string published = provider_.length() == 0U ? text() : provider_.display_text();
    const bool same = (*editor_).text.read() == published;
    return same;
}

SemanticTextResult MaskedTextBox::on_semantic_text_layout(SemanticTextLayout& layout) {
    if (!editor_shows_published_value()) return SemanticTextResult::no_text;
    const SemanticTextResult result = (*editor_).on_semantic_text_layout(layout);
    return result;
}

SemanticTextResult MaskedTextBox::on_set_semantic_text_selection(SemanticTextSelection selection) {
    if (!editor_shows_published_value()) return SemanticTextResult::no_text;
    const SemanticTextResult result = (*editor_).on_set_semantic_text_selection(selection);
    return result;
}

// -- anatomy -----------------------------------------------------------------------

namespace {

constexpr std::array<PartField<MaskedTextBox::Parts>, 3> masked_text_box_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &MaskedTextBox::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &MaskedTextBox::Parts::edge},
    {.declaration = {.name = "field", .role = PartRole::well, .composed_from = "text_box"},
     .member = &MaskedTextBox::Parts::field},
}};

constexpr std::array<StateField<MaskedTextBox::States>, 10> masked_text_box_state_fields{{
    {.state = AnatomyState::normal, .member = &MaskedTextBox::States::normal},
    {.state = AnatomyState::pressed, .member = &MaskedTextBox::States::pressed},
    {.state = AnatomyState::captured, .member = &MaskedTextBox::States::captured},
    {.state = AnatomyState::enabled, .member = &MaskedTextBox::States::enabled},
    {.state = AnatomyState::disabled, .member = &MaskedTextBox::States::disabled},
    {.state = AnatomyState::focused, .member = &MaskedTextBox::States::focused},
    {.state = AnatomyState::focus_visible, .member = &MaskedTextBox::States::focus_visible},
    {.state = AnatomyState::read_only, .member = &MaskedTextBox::States::read_only},
    {.state = AnatomyState::invalid, .member = &MaskedTextBox::States::invalid},
    {.state = AnatomyState::pending, .member = &MaskedTextBox::States::pending},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 3> masked_text_box_parts =
    joined_declarations(scrollable_detail::parts, part_declarations(masked_text_box_extra_part_fields));
constexpr std::array<AnatomyState, 10> masked_text_box_states =
    state_declarations(masked_text_box_state_fields);

constexpr ControlAnatomy masked_text_box_anatomy{
    .control_id = "masked_text_box",
    .parts = masked_text_box_parts,
    .states = masked_text_box_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& MaskedTextBox::anatomy() const noexcept {
    return masked_text_box_anatomy;
}

MaskedTextBox::Parts MaskedTextBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(), record.scrollable);
    read_parts(anatomy(), masked_text_box_extra_part_fields, published.parts(), record);
    return record;
}

MaskedTextBox::States MaskedTextBox::states() const {
    States record;
    read_states(masked_text_box_state_fields, anatomy_states(), record);
    return record;
}

void MaskedTextBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Panel::Parts panel = panel_parts();
    Parts record;
    record.scrollable = panel.scrollable;
    record.face = panel.face;
    record.edge = panel.edge;
    // The field is the child text box, which draws the prompts and literals.
    if (editor_) record.field = PartGeometry::from_bounds((*editor_).committed_arranged_bounds());
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), masked_text_box_extra_part_fields, record, geometry);
}

AnatomyStateSet MaskedTextBox::anatomy_states() const {
    AnatomyStateSet states = ScrollableControl::anatomy_states();
    if (read_only.read()) states.insert(AnatomyState::read_only);
    const ControlVisualStatus status = visual_status();
    if (status == ControlVisualStatus::invalid) {
        states.insert(AnatomyState::invalid);
    } else if (status == ControlVisualStatus::pending) {
        states.insert(AnatomyState::pending);
    }
    return states;
}

namespace {

// The events MaskedTextBox adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 5> masked_text_box_interactions{{
    {.name = "mask_input_rejected", .payload = "MaskInputRejection", .mutable_payload = false},
    {.name = "text_changed", .payload = "std::string", .mutable_payload = false},
    {.name = "mask_changed", .payload = "std::string", .mutable_payload = false},
    {.name = "overwrite_mode_changed", .payload = "bool", .mutable_payload = false},
    {.name = "type_validation_completed", .payload = "MaskValidation", .mutable_payload = false},
}};

} // namespace

void MaskedTextBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), masked_text_box_interactions.begin(),
                        masked_text_box_interactions.end());
}

// property_codemod begin MaskedTextBox (tools/property_families/masked_text_box.json; generated, do not edit)

void MaskedTextBox::prepare_write(const decltype(reject_input_on_first_failure)&, bool&) {
    require_mutable();
}

void MaskedTextBox::written(const decltype(reject_input_on_first_failure)&) {
}

void MaskedTextBox::on_mask_changed() {
    publish_change(mask_changed_, mask.read());
}

void MaskedTextBox::on_read_only_changed() {}

// property_codemod end MaskedTextBox

} // namespace gui_forms
