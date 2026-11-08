#include "gui_forms/controls/ribbon_font_control/ribbon_font_control.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {
namespace {

// Field geometry in logical units at text scale 1: one ribbon row.
constexpr double field_height = 22.0;
constexpr double field_gap = 2.0;
constexpr double minimum_field_width = 16.0;
constexpr double maximum_field_width = 512.0;

// A size as its list shows it: whole points without a fraction, others with
// one decimal ("10.5").
std::string size_text(double size) {
    std::ostringstream text;
    if (std::floor(size) == size) {
        text << static_cast<long long>(size);
    } else {
        text.setf(std::ios::fixed);
        text.precision(1);
        text << size;
    }
    return text.str();
}

// The size list's rows, one per size in `sizes`.
std::vector<std::string> size_texts(std::span<const double> sizes) {
    std::vector<std::string> texts;
    texts.reserve(sizes.size());
    for (const double size : sizes) texts.push_back(size_text(size));
    return texts;
}

} // namespace

RibbonFontControl::RibbonFontControl(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::control);
    sizes_ = {8.0, 9.0, 10.0, 10.5, 11.0, 12.0, 14.0, 16.0, 18.0, 20.0, 22.0, 24.0, 26.0, 28.0, 36.0, 48.0, 72.0};
}

void RibbonFontControl::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    family_ = make_control<ComboBox>(StableId(prefix + ".family"));
    size_ = make_control<ComboBox>(StableId(prefix + ".size"));
    (*family_).accessible_name = "Font";
    (*size_).accessible_name = "Font size";
    (*size_).items = size_texts(sizes_);
    add_child(family_);
    add_child(size_);
    const std::weak_ptr<RibbonFontControl> weak =
        std::static_pointer_cast<RibbonFontControl>(shared_from_this());
    family_changed_ = (*family_).selected_index_changed().subscribe(*this, FieldChanged{weak, Field::family});
    size_changed_ = (*size_).selected_index_changed().subscribe(*this, FieldChanged{weak, Field::size});
}

void RibbonFontControl::on_dispose() noexcept {
    command_value_.disconnect();
    command_state_.disconnect();
    family_changed_.disconnect();
    size_changed_.disconnect();
}

// -- model ---------------------------------------------------------------------

void RibbonFontControl::set_families(std::vector<std::string> families) {
    require_mutable();
    if (families.size() > maximum_families) {
        throw std::invalid_argument("RibbonFontControl has more families than maximum_families");
    }
    std::unordered_set<std::string> seen{};
    for (const std::string& family : families) {
        if (family.empty() || !validate_utf8(family).valid() || family.find('\n') != std::string::npos ||
            !seen.insert(family).second) {
            throw std::invalid_argument("RibbonFontControl families must be unique, single-line UTF-8 names");
        }
    }
    families_ = std::move(families);
    synchronizing_ = true;
    (*family_).items = families_;
    synchronizing_ = false;
    show(value.read());
}

void RibbonFontControl::set_sizes(std::vector<double> sizes) {
    require_mutable();
    if (sizes.size() > maximum_sizes) throw std::invalid_argument("RibbonFontControl has more sizes than maximum_sizes");
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        const double size = sizes[index];
        const bool in_range = std::isfinite(size) && size > 0.0 && size <= FontCommand::maximum_size;
        if (!in_range || (index > 0U && size <= sizes[index - 1U])) {
            throw std::invalid_argument("RibbonFontControl sizes must ascend within FontCommand's range");
        }
    }
    sizes_ = std::move(sizes);
    std::vector<std::string> texts = size_texts(sizes_);
    synchronizing_ = true;
    (*size_).items = std::move(texts);
    synchronizing_ = false;
    show(value.read());
}

void RibbonFontControl::set_field_widths(double family, double size) {
    require_mutable();
    const bool family_valid = std::isfinite(family) && family >= minimum_field_width && family <= maximum_field_width;
    const bool size_valid = std::isfinite(size) && size >= minimum_field_width && size <= maximum_field_width;
    if (!family_valid || !size_valid) {
        throw std::invalid_argument("RibbonFontControl field widths must lie within 16 to 512");
    }
    family_width_ = family;
    size_width_ = size;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test);
}

// A changed command first drops the old one's subscriptions.
void RibbonFontControl::prepare_write(const decltype(command)&, std::shared_ptr<FontCommand>& incoming) {
    require_mutable();
    if (command.read() == incoming) return;
    command_value_.disconnect();
    command_state_.disconnect();
}

void RibbonFontControl::written(const decltype(command)&) {
    const std::shared_ptr<FontCommand> bound = command.read();
    if (bound) {
        const std::weak_ptr<RibbonFontControl> weak =
            std::static_pointer_cast<RibbonFontControl>(shared_from_this());
        command_value_ = (*bound).value_changed().subscribe(*this, CommandValueChanged{weak});
        command_state_ = (*(*bound).command()).state_changed().subscribe(*this, CommandStateChanged{weak});
    }
    sync_enabled();
    show(value.read());
}

// value is a view: the command's value when there is one, else the
// control's own.
FontChoice RibbonFontControl::read_view(const decltype(value)&) const {
    const std::shared_ptr<FontCommand>& bound = command.read();
    if (bound) return (*bound).value();
    return own_value_;
}


void RibbonFontControl::write_view(const decltype(value)&, FontChoice incoming) {
    require_mutable();
    const std::shared_ptr<FontCommand> bound = command.read();
    if (bound) {
        (*bound).set_value(std::move(incoming));
        return;
    }
    if (!FontCommand::valid_value(incoming)) {
        throw std::invalid_argument("RibbonFontControl value is not a valid font choice");
    }
    if (own_value_ == incoming) return;
    own_value_ = std::move(incoming);
    show(own_value_);
    value_changed_.emit(own_value_);
}

// -- synchronization ---------------------------------------------------------------

void RibbonFontControl::FieldChanged::operator()(std::optional<std::size_t> index) const {
    const std::shared_ptr<RibbonFontControl> control = target.lock();
    if (control && (*control).is_alive()) (*control).field_changed(field, index);
}

void RibbonFontControl::CommandValueChanged::operator()(const FontChoice& value) const {
    const std::shared_ptr<RibbonFontControl> control = target.lock();
    if (!control || !(*control).is_alive()) return;
    (*control).show(value);
    (*control).value_changed_.emit(value);
}

void RibbonFontControl::CommandStateChanged::operator()(const CommandState&) const {
    const std::shared_ptr<RibbonFontControl> control = target.lock();
    if (control && (*control).is_alive()) (*control).sync_enabled();
}

// The user chose in a field: the other half of the value is kept, and the
// choice commits to the command (or becomes the control's own value).
void RibbonFontControl::field_changed(Field field, std::optional<std::size_t> index) {
    if (synchronizing_ || !index) return;
    FontChoice chosen = value.read();
    if (field == Field::family) {
        if (*index >= families_.size()) return;
        chosen.family = families_[*index];
    } else {
        if (*index >= sizes_.size()) return;
        chosen.size = sizes_[*index];
    }
    if (command.read()) {
        const Control::Ptr lifetime = shared_from_this();
        const bool committed = (*command.read()).commit(chosen, stable_id().value());
        // A refused commit leaves the fields showing the command's value.
        if (!committed && is_alive()) show(value.read());
        return;
    }
    if (own_value_ == chosen) return;
    own_value_ = std::move(chosen);
    value_changed_.emit(own_value_);
}

// Shows `value` in the fields; a half that is unknown or not listed shows
// empty.
void RibbonFontControl::show(const FontChoice& value) {
    std::optional<std::size_t> family{};
    for (std::size_t index = 0; index < families_.size(); ++index) {
        if (families_[index] == value.family) family = index;
    }
    std::optional<std::size_t> size{};
    for (std::size_t index = 0; index < sizes_.size(); ++index) {
        if (sizes_[index] == value.size) size = index;
    }
    synchronizing_ = true;
    if ((*family_).selected_index.read() != family) (*family_).selected_index = family;
    if ((*size_).selected_index.read() != size) (*size_).selected_index = size;
    synchronizing_ = false;
}

void RibbonFontControl::sync_enabled() {
    const bool enabled = !command.read() || (*command.read()).state().enabled;
    (*family_).enabled = enabled;
    (*size_).enabled = enabled;
}

// -- layout --------------------------------------------------------------------

RibbonFontControl::Fields RibbonFontControl::fields(Rect client) const {
    const double scale = effective_text_scale();
    const double height = std::min(client.height, field_height * scale);
    const double y = client.y + std::floor((client.height - height) * 0.5);
    const double family = family_width_ * scale;
    const double size = size_width_ * scale;
    Fields placed;
    placed.family = {client.x, y, std::min(family, client.width), height};
    const double size_x = client.x + family + field_gap * scale;
    placed.size = {size_x, y, std::max(0.0, std::min(size, client.x + client.width - size_x)), height};
    return placed;
}

Size RibbonFontControl::natural_size() const noexcept {
    const double scale = effective_text_scale();
    const Size size{(family_width_ + field_gap + size_width_) * scale, field_height * scale};
    return size;
}

Size RibbonFontControl::measure(Size available) {
    const Size natural = natural_size();
    const Size result{std::min(natural.width, available.width), std::min(natural.height, available.height)};
    return result;
}

void RibbonFontControl::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    // In the bounds as arranged, as publish_part_geometry places them.
    const Fields placed = fields(client_rectangle());
    if (family_ && is_current_layout_child(family_)) set_child_layout(family_, placed.family);
    if (size_ && is_current_layout_child(size_)) set_child_layout(size_, placed.size);
}

// -- semantics and anatomy -----------------------------------------------------

SemanticDescriptor RibbonFontControl::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read().empty() ? "Font" : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = true;
    return descriptor;
}

namespace {

constexpr std::array<PartField<RibbonFontControl::Parts>, 3> font_control_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &RibbonFontControl::Parts::region},
    {.declaration = {.name = "family_field", .role = PartRole::well, .composed_from = "combo_box"},
     .member = &RibbonFontControl::Parts::family_field},
    {.declaration = {.name = "size_field", .role = PartRole::well, .composed_from = "combo_box"},
     .member = &RibbonFontControl::Parts::size_field},
}};

constexpr std::array<PartDeclaration, 3> font_control_parts = part_declarations(font_control_part_fields);
constexpr std::array<AnatomyState, 5> font_control_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ControlAnatomy font_control_anatomy{
    .control_id = "ribbon_font_control",
    .parts = font_control_parts,
    .states = font_control_states,
    .custom_states = {},
    .item_systems = {},
};

// The events RibbonFontControl adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 1> font_control_interactions{{
    {.name = "value_changed", .payload = "FontChoice", .mutable_payload = false},
}};

} // namespace

const ControlAnatomy& RibbonFontControl::anatomy() const noexcept {
    return font_control_anatomy;
}

void RibbonFontControl::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Fields placed = fields(client_rectangle());
    Parts record;
    record.region = PartGeometry::from_bounds(client_rectangle());
    record.family_field = PartGeometry::from_bounds(placed.family);
    record.size_field = PartGeometry::from_bounds(placed.size);
    write_parts(anatomy(), font_control_part_fields, record, geometry);
}

RibbonFontControl::Parts RibbonFontControl::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), font_control_part_fields, published.parts(), record);
    return record;
}

void RibbonFontControl::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), font_control_interactions.begin(), font_control_interactions.end());
}

// property_codemod begin RibbonFontControl (tools/property_families/ribbon_font_control.json; generated, do not edit)

// property_codemod end RibbonFontControl

} // namespace gui_forms
