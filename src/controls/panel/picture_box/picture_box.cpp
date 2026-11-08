#include "gui_forms/controls/panel/picture_box/picture_box.hpp"
#include "../../basic/basic_control_rendering.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

PictureBox::PictureBox(StableId stable_id) : Panel(std::move(stable_id)) {
    set_background(Color::rgba(255, 255, 255));

}

bool PictureBox::should_serialize_image() const noexcept {
    return image.read().value != 0U;
}

void PictureBox::clear_image() {
    image = ImageId{};
}

bool PictureBox::has_valid_image() const noexcept {
    return window() != nullptr && image.read().value != 0U &&
           (*window()).image_resources().find(image.read()).has_value();
}

Size PictureBox::image_size() const noexcept {
    if (window() == nullptr || image.read().value == 0U) {
        return {};
    }
    const std::optional<ImageResourceView> resource = (*window()).image_resources().find(image.read());
    if (!resource) {
        return {};
    }
    return {static_cast<double>((*resource).metadata.width),
            static_cast<double>((*resource).metadata.height)};
}

Rect PictureBox::content_bounds() const noexcept {
    const Rect bounds = local_bounds();
    const double inset = border_style.read() == BorderStyle::none ? 0.0 : 1.0;
    return {inset, inset, std::max(0.0, bounds.width - inset * 2.0),
            std::max(0.0, bounds.height - inset * 2.0)};
}

Rect PictureBox::image_bounds() const noexcept {
    const Rect content = content_bounds();
    const Size source = image_size();
    if (source.width <= 0.0 || source.height <= 0.0 || content.empty()) {
        return {};
    }
    switch (size_mode.read()) {
    case PictureBoxSizeMode::stretch_image:
        return content;
    case PictureBoxSizeMode::center_image:
        return {content.x + (content.width - source.width) * 0.5,
                content.y + (content.height - source.height) * 0.5,
                source.width, source.height};
    case PictureBoxSizeMode::zoom: {
        const double scale = std::min(content.width / source.width,
                                      content.height / source.height);
        const double width = source.width * scale;
        const double height = source.height * scale;
        return {content.x + (content.width - width) * 0.5,
                content.y + (content.height - height) * 0.5,
                width, height};
    }
    case PictureBoxSizeMode::normal:
    case PictureBoxSizeMode::auto_size:
        return {content.x, content.y, source.width, source.height};
    }
    return {};
}

Size PictureBox::measure(Size available) {
    const Rect requested = requested_bounds();
    if (size_mode.read() == PictureBoxSizeMode::auto_size) {
        const Size source = image_size();
        const double border = border_style.read() == BorderStyle::none ? 0.0 : 2.0;
        if (source.width > 0.0 && source.height > 0.0) {
            return {std::min(available.width, source.width + border),
                    std::min(available.height, source.height + border)};
        }
    }
    return {std::min(available.width, std::max(0.0, requested.width)),
            std::min(available.height, std::max(0.0, requested.height))};
}

void PictureBox::on_paint(Painter& painter, Rect) {
    paint_panel(painter, local_bounds());
    if (!has_valid_image() || image_opacity.read() <= 0.0) {
        return;
    }
    const Rect destination = image_bounds();
    if (!destination.empty()) {
        painter.draw_image(image.read(), destination, image_opacity.read());
    }
}

bool PictureBox::hit_test_local(Point) const {
    return false;
}

SemanticDescriptor PictureBox::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::image;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    const Size source = image_size();
    if (source.width > 0.0 && source.height > 0.0) {
        descriptor.value = std::to_string(static_cast<std::uint32_t>(source.width)) +
                           " x " +
                           std::to_string(static_cast<std::uint32_t>(source.height));
    }
    descriptor.exposed = has_valid_image() || !descriptor.name.empty() ||
                         !descriptor.description.empty();
    return descriptor;
}

namespace {

constexpr std::array<PartField<PictureBox::Parts>, 3> picture_box_extra_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &PictureBox::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &PictureBox::Parts::edge},
    {.declaration = {.name = "image", .role = PartRole::icon, .composed_from = {}},
     .member = &PictureBox::Parts::image},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 3>
    picture_box_parts = joined_declarations(scrollable_detail::parts,
                                            part_declarations(picture_box_extra_fields));

constexpr ControlAnatomy picture_box_anatomy{
    .control_id = "picture_box",
    .parts = picture_box_parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& PictureBox::anatomy() const noexcept {
    return picture_box_anatomy;
}

void PictureBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Panel::Parts panel = panel_parts();
    Parts record;
    record.scrollable = panel.scrollable;
    record.face = panel.face;
    record.edge = panel.edge;
    if (has_valid_image() && image_opacity.read() > 0.0) {
        record.image = PartGeometry::from_bounds(image_bounds());
    }
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), picture_box_extra_fields, record, geometry);
}

PictureBox::Parts PictureBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), picture_box_extra_fields, published.parts(), record);
    return record;
}

namespace {

// The events PictureBox adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 1> picture_box_interactions{{
    {.name = "image_changed", .payload = "ImageId", .mutable_payload = false},
}};

} // namespace

void PictureBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), picture_box_interactions.begin(), picture_box_interactions.end());
}

// property_codemod begin PictureBox (tools/property_families/picture_box.json; generated, do not edit)

namespace {

PropertyDescriptor picture_box_image_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "image";
    descriptor.kind = BindingValueKind::image;
    descriptor.category = "Appearance";
    descriptor.description = "Generational image resource displayed by this control.";
    descriptor.default_value = BindingValue{ImageId{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_picture_box_image() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        picture_box_image_descriptor(), PropertyOperations{true, true, true, true});
    return descriptor;
}

PropertyDescriptor picture_box_size_mode_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "size_mode";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Appearance";
    descriptor.description = "Image placement and scaling policy.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"picture_box_size_mode", "normal", 0}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.enumeration = picture_box_size_mode_enum();
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_picture_box_size_mode() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        picture_box_size_mode_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor picture_box_image_opacity_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "image_opacity";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Appearance";
    descriptor.description = "Image opacity from zero through one.";
    descriptor.default_value = BindingValue{1.0};
    descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_picture_box_image_opacity() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        picture_box_image_opacity_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<PictureBoxSizeMode>::type() {
    return PropertyType{BindingValueKind::enumeration, false, picture_box_size_mode_enum()};
}

BindingValue PropertyValueTraits<PictureBoxSizeMode>::to_binding(PictureBoxSizeMode value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = picture_box_size_mode_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

PictureBoxSizeMode PropertyValueTraits<PictureBoxSizeMode>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<PictureBoxSizeMode>(choice.value);
}

const std::array<PropertyEntry, PictureBox::picture_box_property_count>& PictureBox::picture_box_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 3U> own{{
        {"image", &describe_picture_box_image,
         &read_cell_entry<PictureBox, decltype(&PictureBox::image), &PictureBox::image>,
         &write_cell_entry<PictureBox, decltype(&PictureBox::image), &PictureBox::image>,
         &reset_member_entry<PictureBox, decltype(&PictureBox::clear_image), &PictureBox::clear_image>, &should_serialize_member_entry<PictureBox, decltype(&PictureBox::should_serialize_image), &PictureBox::should_serialize_image>, nullptr,
         &connect_event_entry<PictureBox, decltype(&PictureBox::image_changed_), &PictureBox::image_changed_>},
        {"size_mode", &describe_picture_box_size_mode,
         &read_cell_entry<PictureBox, decltype(&PictureBox::size_mode), &PictureBox::size_mode>,
         &write_cell_entry<PictureBox, decltype(&PictureBox::size_mode), &PictureBox::size_mode>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"image_opacity", &describe_picture_box_image_opacity,
         &read_cell_entry<PictureBox, decltype(&PictureBox::image_opacity), &PictureBox::image_opacity>,
         &write_cell_entry<PictureBox, decltype(&PictureBox::image_opacity), &PictureBox::image_opacity>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "PictureBox's property table names a property twice");
    static const std::array<PropertyEntry, picture_box_property_count> entries =
        join_property_tables(Panel::panel_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> PictureBox::property_table() const noexcept {
    const std::array<PropertyEntry, picture_box_property_count>& entries = picture_box_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void PictureBox::prepare_write(const decltype(image)&, ImageId&) {
    require_mutable();
}

void PictureBox::written(const decltype(image)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    publish_change(image_changed_, image.read());
}

void PictureBox::prepare_write(const decltype(size_mode)&, PictureBoxSizeMode&) {
    require_mutable();
}

void PictureBox::written(const decltype(size_mode)&) {
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    on_size_mode_changed();
}

void PictureBox::on_size_mode_changed() {}

void PictureBox::prepare_write(const decltype(image_opacity)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0 || incoming > 1.0) {
        throw std::invalid_argument("PictureBox image opacity must be finite and between zero and one");
    }
}

void PictureBox::written(const decltype(image_opacity)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

// property_codemod end PictureBox

} // namespace gui_forms
