#include "gui_forms/controls/wizard.hpp"

#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/step_indicator.hpp"
#include "gui_forms/controls/wizard_page.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

// Geometry in logical units: the padding around the header, page and
// button row, the gaps between them, the button row's height and a
// button's least width.
constexpr double wizard_padding = 16.0;
constexpr double header_gap = 4.0;
constexpr double section_gap = 12.0;
constexpr double button_height = 28.0;
constexpr double minimum_button_width = 80.0;
constexpr double button_gap = 8.0;
// Cancel stands apart from Back and Next.
constexpr double cancel_gap = 16.0;
constexpr double divider_thickness = 1.0;

constexpr std::array<PartField<Wizard::Parts>, 4> wizard_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Wizard::Parts::region},
    {.declaration = {.name = "caption", .role = PartRole::caption, .composed_from = {}},
     .member = &Wizard::Parts::caption},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &Wizard::Parts::label},
    {.declaration = {.name = "divider", .role = PartRole::divider, .composed_from = {}},
     .member = &Wizard::Parts::divider},
}};

constexpr std::array<PartDeclaration, 4> wizard_parts = part_declarations(wizard_part_fields);

constexpr std::array<AnatomyState, 3> wizard_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
};

constexpr ControlAnatomy wizard_anatomy{
    .control_id = "wizard",
    .parts = wizard_parts,
    .states = wizard_states,
    .custom_states = {},
};

} // namespace

Wizard::Wizard(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = false;
    const std::string prefix((*this).stable_id().value());
    indicator_ = make_control<StepIndicator>(StableId(prefix + ".steps"));
    back_ = make_control<Button>(StableId(prefix + ".back"), back_text.read());
    next_ = make_control<Button>(StableId(prefix + ".next"), next_text.read());
    (*next_).visual_style = ButtonVisualStyle::accent;
    cancel_ = make_control<Button>(StableId(prefix + ".cancel"), cancel_text.read());
    tokens_.push_back(
        (*back_).clicked().subscribe(*this, Delegate<ButtonBase&>::bind<Wizard, &Wizard::on_back>(*this)));
    tokens_.push_back(
        (*next_).clicked().subscribe(*this, Delegate<ButtonBase&>::bind<Wizard, &Wizard::on_next>(*this)));
    tokens_.push_back(
        (*cancel_).clicked().subscribe(*this, Delegate<ButtonBase&>::bind<Wizard, &Wizard::on_cancel>(*this)));
    tokens_.push_back((*indicator_).step_clicked().subscribe(
        *this, Delegate<std::size_t>::bind<Wizard, &Wizard::on_step_clicked>(*this)));
}

// add_child needs the shared owner, which make_control has made by now.
void Wizard::initialize_control_tree() {
    add_child(indicator_);
    add_child(back_);
    add_child(next_);
    add_child(cancel_);
    sync();
}

Wizard::~Wizard() {
    for (SubscriptionToken& token : tokens_) token.disconnect();
}

bool Wizard::internal_child(const Control& child) const noexcept {
    const bool internal = &child == indicator_.get() || &child == back_.get() || &child == next_.get() ||
                          &child == cancel_.get();
    return internal;
}

std::vector<std::shared_ptr<WizardPage>> Wizard::pages() const {
    std::vector<std::shared_ptr<WizardPage>> found;
    const std::vector<Control::Ptr> children = snapshot_layout_children();
    found.reserve(children.size());
    for (const Control::Ptr& child : children) {
        std::shared_ptr<WizardPage> page = std::dynamic_pointer_cast<WizardPage>(child);
        if (page) found.push_back(std::move(page));
    }
    return found;
}

std::shared_ptr<WizardPage> Wizard::page(std::size_t index) const {
    const std::vector<std::shared_ptr<WizardPage>> found = pages();
    if (index >= found.size()) return {};
    return found[index];
}

std::size_t Wizard::page_count() const {
    const std::size_t count = pages().size();
    return count;
}

bool Wizard::can_go_back() const noexcept {
    return current_page.read() > 0U;
}

bool Wizard::can_go_next() const {
    const std::shared_ptr<WizardPage> shown = page(current_page.read());
    const bool advance = shown && (*shown).can_advance.read();
    return advance;
}

bool Wizard::go_to(std::uint32_t index) {
    require_mutable();
    const std::size_t count = page_count();
    const std::uint32_t from = current_page.read();
    if (index >= count || index == from) return false;
    WizardPageChangingEvent request{from, index, false};
    page_changing_.emit(request);
    if (request.cancel || current_page.read() != from) return false;
    current_page = index;
    return true;
}

bool Wizard::go_next() {
    require_mutable();
    if (!can_go_next()) return false;
    const std::size_t count = page_count();
    if (static_cast<std::size_t>(current_page.read()) + 1U >= count) {
        finished_.emit();
        return true;
    }
    const bool moved = go_to(current_page.read() + 1U);
    return moved;
}

bool Wizard::go_back() {
    require_mutable();
    if (!can_go_back()) return false;
    const bool moved = go_to(current_page.read() - 1U);
    return moved;
}

void Wizard::cancel() {
    require_mutable();
    cancelled_.emit();
}

void Wizard::on_back(ButtonBase&) {
    static_cast<void>(go_back());
}

void Wizard::on_next(ButtonBase&) {
    static_cast<void>(go_next());
}

void Wizard::on_cancel(ButtonBase&) {
    cancel();
}

void Wizard::on_step_clicked(std::size_t step) {
    if (step < current_page.read()) static_cast<void>(go_to(static_cast<std::uint32_t>(step)));
}

void Wizard::on_page_updated(WizardPage&) {
    sync();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
}

void Wizard::sync() {
    const std::vector<std::shared_ptr<WizardPage>> found = pages();
    const std::uint32_t current = current_page.read();
    std::vector<std::string> names;
    names.reserve(found.size());
    for (std::size_t index = 0U; index < found.size(); ++index) {
        const WizardPage& shown = *found[index];
        names.push_back(shown.title.read());
        set_child_layout_collapsed(found[index], index != current);
    }
    if (names != (*indicator_).steps()) (*indicator_).set_steps(std::move(names));
    if ((*indicator_).current_step.read() != current && current < std::max<std::size_t>(1U, found.size())) {
        (*indicator_).current_step = current;
    }
    set_child_layout_collapsed(indicator_, !show_step_indicator.read());
    const bool last = static_cast<std::size_t>(current) + 1U >= found.size();
    const std::string& advance_text = last ? finish_text.read() : next_text.read();
    if ((*next_).text.read() != advance_text) (*next_).text = advance_text;
    if ((*back_).text.read() != back_text.read()) (*back_).text = back_text.read();
    if ((*cancel_).text.read() != cancel_text.read()) (*cancel_).text = cancel_text.read();
    const bool back_enabled = can_go_back();
    if ((*back_).enabled.read() != back_enabled) (*back_).enabled = back_enabled;
    const bool next_enabled = can_go_next();
    if ((*next_).enabled.read() != next_enabled) (*next_).enabled = next_enabled;
}

FontSpec Wizard::heading_font() const noexcept {
    const FontSpec font = text_font(effective_theme().structure().typography.heading);
    return font;
}

FontSpec Wizard::description_font() const noexcept {
    const FontSpec font = text_font(effective_theme().structure().typography.control);
    return font;
}

Wizard::Placement Wizard::placement(Size size) const {
    Placement placed;
    const double inner_width = std::max(0.0, size.width - wizard_padding * 2.0);
    double y = wizard_padding;
    const std::shared_ptr<WizardPage> shown = page(current_page.read());
    const double heading_height = std::ceil(measure_text_extent_utf8("Ag", heading_font()).logical_size.height);
    placed.caption = Rect{wizard_padding, y, inner_width, heading_height};
    y += heading_height;
    if (shown && !(*shown).description.read().empty()) {
        const double line = std::ceil(measure_text_extent_utf8("Ag", description_font()).logical_size.height);
        y += header_gap;
        placed.label = Rect{wizard_padding, y, inner_width, line};
        y += line;
    }
    y += section_gap;
    if (show_step_indicator.read()) {
        const double height = std::ceil((*indicator_).measure(Size{inner_width, size.height}).height);
        placed.indicator = Rect{wizard_padding, y, inner_width, height};
        y += height + section_gap;
    }
    // The button row from the bottom: Cancel at the trailing edge, then
    // Next and Back before it.
    const double row_y = std::max(y, size.height - wizard_padding - button_height);
    double right = size.width - wizard_padding;
    placed.cancel = Rect{right - cancel_size_.width, row_y, cancel_size_.width, button_height};
    right -= cancel_size_.width + cancel_gap;
    placed.next = Rect{right - next_size_.width, row_y, next_size_.width, button_height};
    right -= next_size_.width + button_gap;
    placed.back = Rect{right - back_size_.width, row_y, back_size_.width, button_height};
    placed.divider = Rect{0.0, std::max(y, row_y - section_gap - divider_thickness), size.width, divider_thickness};
    placed.page = Rect{wizard_padding, y, inner_width, std::max(0.0, placed.divider.y - section_gap - y)};
    return placed;
}

Size Wizard::measure(Size available) {
    const Rect requested = requested_bounds();
    const double width = requested.width > 0.0 ? requested.width : 520.0;
    const double height = requested.height > 0.0 ? requested.height : 380.0;
    const Size measured{std::min(available.width, width), std::min(available.height, height)};
    return measured;
}

void Wizard::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    sync();
    const Size unbounded{final_bounds.width, button_height};
    const Size back_wanted = (*back_).measure(unbounded);
    const Size next_wanted = (*next_).measure(unbounded);
    const Size cancel_wanted = (*cancel_).measure(unbounded);
    back_size_ = Size{std::max(minimum_button_width, std::ceil(back_wanted.width)), button_height};
    next_size_ = Size{std::max(minimum_button_width, std::ceil(next_wanted.width)), button_height};
    cancel_size_ = Size{std::max(minimum_button_width, std::ceil(cancel_wanted.width)), button_height};
    const Placement placed = placement(Size{final_bounds.width, final_bounds.height});
    if (show_step_indicator.read()) set_child_layout(indicator_, placed.indicator);
    set_child_layout(back_, placed.back);
    set_child_layout(next_, placed.next);
    set_child_layout(cancel_, placed.cancel);
    const std::shared_ptr<WizardPage> shown = page(current_page.read());
    if (shown) {
        static_cast<void>((*shown).measure(Size{placed.page.width, placed.page.height}));
        set_child_layout(shown, placed.page);
    }
}

void Wizard::on_paint(Painter& painter, Rect) {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    const AnatomyStateSet states = anatomy_states();
    const std::shared_ptr<WizardPage> shown = page(current_page.read());
    if (shown) {
        const std::string& heading = (*shown).title.read();
        PartPaint caption{.role = PartRole::caption, .bounds = placed.caption, .states = states,
                          .family = ControlVisualRole::panel};
        caption.text = lend_paint_text(heading);
        caption.font = heading_font();
        paint_part(painter, *this, caption);
        const std::string& line = (*shown).description.read();
        if (!line.empty()) {
            PartPaint label{.role = PartRole::label, .bounds = placed.label, .states = states,
                            .family = ControlVisualRole::panel};
            label.text = lend_paint_text(line);
            label.font = description_font();
            label.tone = PartTextTone::secondary;
            paint_part(painter, *this, label);
        }
    }
    const PartPaint divider{.role = PartRole::divider, .bounds = placed.divider, .states = states,
                            .family = ControlVisualRole::panel};
    paint_part(painter, *this, divider);
}

void Wizard::on_key_bubble(KeyEvent& event) {
    if (event.action != KeyAction::down || event.modifiers != Modifier::none) return;
    if (event.physical_key == PhysicalKey::enter) {
        event.handled = true;
        static_cast<void>(go_next());
    } else if (event.physical_key == PhysicalKey::escape) {
        event.handled = true;
        cancel();
    }
}

SemanticDescriptor Wizard::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::group;
    const std::shared_ptr<WizardPage> shown = page(current_page.read());
    descriptor.name = accessible_name.read();
    if (descriptor.name.empty() && shown) descriptor.name = (*shown).title.read();
    descriptor.description = accessible_description.read();
    if (descriptor.description.empty() && shown) descriptor.description = (*shown).description.read();
    descriptor.exposed = true;
    return descriptor;
}

const ControlAnatomy& Wizard::anatomy() const noexcept {
    return wizard_anatomy;
}

void Wizard::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect client = client_rectangle();
    const Placement placed = placement(Size{client.width, client.height});
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.caption = PartGeometry::from_bounds(placed.caption);
    record.label = PartGeometry::from_bounds(placed.label);
    record.divider = PartGeometry::from_bounds(placed.divider);
    write_parts(anatomy(), wizard_part_fields, record, geometry);
}

Wizard::Parts Wizard::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), wizard_part_fields, published.parts(), record);
    return record;
}

void Wizard::on_dispose() noexcept {
    for (SubscriptionToken& token : tokens_) token.disconnect();
    Control::on_dispose();
}

// -- property hooks --------------------------------------------------------------

void Wizard::prepare_write(const decltype(current_page)&, std::uint32_t& incoming) {
    require_mutable();
    if (incoming != 0U && incoming >= page_count()) {
        throw std::invalid_argument("Wizard current page is past the last page");
    }
}

void Wizard::written(const decltype(current_page)&) {
    sync();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
    page_changed_.emit(current_page.read());
}

void Wizard::prepare_write(const decltype(show_step_indicator)&, bool&) {
    require_mutable();
}

void Wizard::written(const decltype(show_step_indicator)&) {
    sync();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint);
}

void Wizard::prepare_write(const decltype(back_text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("Wizard back text must be valid UTF-8");
}

void Wizard::written(const decltype(back_text)&) {
    sync();
    invalidate(Dirty::arrange | Dirty::paint);
}

void Wizard::prepare_write(const decltype(next_text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("Wizard next text must be valid UTF-8");
}

void Wizard::written(const decltype(next_text)&) {
    sync();
    invalidate(Dirty::arrange | Dirty::paint);
}

void Wizard::prepare_write(const decltype(finish_text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("Wizard finish text must be valid UTF-8");
}

void Wizard::written(const decltype(finish_text)&) {
    sync();
    invalidate(Dirty::arrange | Dirty::paint);
}

void Wizard::prepare_write(const decltype(cancel_text)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("Wizard cancel text must be valid UTF-8");
}

void Wizard::written(const decltype(cancel_text)&) {
    sync();
    invalidate(Dirty::arrange | Dirty::paint);
}

// property_codemod begin Wizard (tools/property_families/wizard.json; generated, do not edit)

namespace {

PropertyDescriptor wizard_current_page_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "current_page";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Behavior";
    descriptor.description = "The page shown, counted from zero.";
    descriptor.default_value = BindingValue{std::uint64_t{0U}};
    descriptor.invalidation_effects = Dirty::arrange | Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_wizard_current_page() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        wizard_current_page_descriptor(), PropertyOperations{true, true, false, true});
    return descriptor;
}

PropertyDescriptor wizard_show_step_indicator_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "show_step_indicator";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the step indicator shows above the page.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_wizard_show_step_indicator() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        wizard_show_step_indicator_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor wizard_back_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "back_text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The Back button's text.";
    descriptor.default_value = BindingValue{std::string{"< Back"}};
    descriptor.invalidation_effects = Dirty::arrange | Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_wizard_back_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        wizard_back_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor wizard_next_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "next_text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The Next button's text.";
    descriptor.default_value = BindingValue{std::string{"Next >"}};
    descriptor.invalidation_effects = Dirty::arrange | Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_wizard_next_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        wizard_next_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor wizard_finish_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "finish_text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The Next button's text on the last page.";
    descriptor.default_value = BindingValue{std::string{"Finish"}};
    descriptor.invalidation_effects = Dirty::arrange | Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_wizard_finish_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        wizard_finish_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor wizard_cancel_text_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "cancel_text";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The Cancel button's text.";
    descriptor.default_value = BindingValue{std::string{"Cancel"}};
    descriptor.invalidation_effects = Dirty::arrange | Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_wizard_cancel_text() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        wizard_cancel_text_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, Wizard::wizard_property_count>& Wizard::wizard_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 6U> own{{
        {"current_page", &describe_wizard_current_page,
         &read_cell_entry<Wizard, decltype(&Wizard::current_page), &Wizard::current_page>,
         &write_cell_entry<Wizard, decltype(&Wizard::current_page), &Wizard::current_page>,
         nullptr, nullptr, nullptr,
         &connect_event_entry<Wizard, decltype(&Wizard::page_changed_), &Wizard::page_changed_>},
        {"show_step_indicator", &describe_wizard_show_step_indicator,
         &read_cell_entry<Wizard, decltype(&Wizard::show_step_indicator), &Wizard::show_step_indicator>,
         &write_cell_entry<Wizard, decltype(&Wizard::show_step_indicator), &Wizard::show_step_indicator>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"back_text", &describe_wizard_back_text,
         &read_cell_entry<Wizard, decltype(&Wizard::back_text), &Wizard::back_text>,
         &write_cell_entry<Wizard, decltype(&Wizard::back_text), &Wizard::back_text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"next_text", &describe_wizard_next_text,
         &read_cell_entry<Wizard, decltype(&Wizard::next_text), &Wizard::next_text>,
         &write_cell_entry<Wizard, decltype(&Wizard::next_text), &Wizard::next_text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"finish_text", &describe_wizard_finish_text,
         &read_cell_entry<Wizard, decltype(&Wizard::finish_text), &Wizard::finish_text>,
         &write_cell_entry<Wizard, decltype(&Wizard::finish_text), &Wizard::finish_text>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"cancel_text", &describe_wizard_cancel_text,
         &read_cell_entry<Wizard, decltype(&Wizard::cancel_text), &Wizard::cancel_text>,
         &write_cell_entry<Wizard, decltype(&Wizard::cancel_text), &Wizard::cancel_text>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "Wizard's property table names a property twice");
    static const std::array<PropertyEntry, wizard_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> Wizard::property_table() const noexcept {
    const std::array<PropertyEntry, wizard_property_count>& entries = wizard_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end Wizard

namespace {

// The events Wizard adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 4> wizard_interactions{{
    {.name = "page_changing", .payload = "WizardPageChangingEvent", .mutable_payload = true},
    {.name = "page_changed", .payload = "std::uint32_t", .mutable_payload = false},
    {.name = "finished", .payload = "", .mutable_payload = false},
    {.name = "cancelled", .payload = "", .mutable_payload = false},
}};

} // namespace

void Wizard::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), wizard_interactions.begin(), wizard_interactions.end());
}

} // namespace gui_forms
