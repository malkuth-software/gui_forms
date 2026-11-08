#include "gui_forms/controls/panel/picture_box/picture_box.hpp"
#include "../../basic/basic_control_rendering.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"

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
    PropertyDescriptor image;
    image.name = "image";
    image.kind = BindingValueKind::image;
    image.category = "Appearance";
    image.description = "Generational image resource displayed by this control.";
    image.default_value = BindingValue{ImageId{}};
    image.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    define_bindable_property({
        std::move(image),
        detail::BindingMemberGetter<PictureBox, ImageId>(
            *this, &PictureBox::image_),
        detail::DirectPropertySetter<PictureBox, ImageId>(
            *this, &PictureBox::set_image),
        detail::EventChangeConnector<ImageId>(image_changed_),
        detail::BoundMemberFunction<void (PictureBox::*)()>(
            *this, &PictureBox::clear_image),
        detail::BoundMemberFunction<
            bool (PictureBox::*)() const noexcept>(
                *this, &PictureBox::should_serialize_image)});

    PropertyDescriptor size_mode;
    size_mode.name = "size_mode";
    size_mode.kind = BindingValueKind::enumeration;
    size_mode.category = "Appearance";
    size_mode.description = "Image placement and scaling policy.";
    size_mode.default_value = BindingValue{PropertyEnumValue{
        "picture_box_size_mode", "normal", 0}};
    size_mode.invalidation_effects =
        Dirty::measure | Dirty::paint | Dirty::semantics;
    size_mode.bindable = false;
    size_mode.enumeration = picture_box_size_mode_enum();
    define_bindable_property({
        std::move(size_mode),
        detail::BoundMemberFunction<BindingValue (PictureBox::*)() const>(
            *this, &PictureBox::size_mode_property_value),
        detail::BoundMemberFunction<
            void (PictureBox::*)(const BindingValue&)>(
                *this, &PictureBox::set_size_mode_property_value),
        {}, {}, {}});

    PropertyDescriptor opacity;
    opacity.name = "image_opacity";
    opacity.kind = BindingValueKind::number;
    opacity.category = "Appearance";
    opacity.description = "Image opacity from zero through one.";
    opacity.default_value = BindingValue{1.0};
    opacity.invalidation_effects = Dirty::paint | Dirty::semantics;
    opacity.bindable = false;
    define_bindable_property({
        std::move(opacity),
        detail::BindingMemberGetter<PictureBox, double>(
            *this, &PictureBox::image_opacity_),
        detail::DirectPropertySetter<PictureBox, double>(
            *this, &PictureBox::set_image_opacity), {}, {}, {}});
}

bool PictureBox::should_serialize_image() const noexcept {
    return image_.value != 0U;
}

BindingValue PictureBox::size_mode_property_value() const {
    return picture_box_size_mode_value(size_mode_);
}

void PictureBox::set_size_mode_property_value(const BindingValue& value) {
    set_size_mode(static_cast<PictureBoxSizeMode>(
        std::get<PropertyEnumValue>(value).value));
}

void PictureBox::set_image(ImageId image) {
    require_mutable();
    if (image_ == image) {
        return;
    }
    image_ = image;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    publish_change(image_changed_, image_);
}

void PictureBox::clear_image() {
    set_image({});
}

bool PictureBox::has_valid_image() const noexcept {
    return window() != nullptr && image_.value != 0U &&
           (*window()).image_resources().find(image_).has_value();
}

Size PictureBox::image_size() const noexcept {
    if (window() == nullptr || image_.value == 0U) {
        return {};
    }
    const std::optional<ImageResourceView> resource = (*window()).image_resources().find(image_);
    if (!resource) {
        return {};
    }
    return {static_cast<double>((*resource).metadata.width),
            static_cast<double>((*resource).metadata.height)};
}

void PictureBox::set_size_mode(PictureBoxSizeMode mode) {
    require_mutable();
    if (size_mode_ == mode) {
        return;
    }
    size_mode_ = mode;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void PictureBox::set_image_opacity(double opacity) {
    require_mutable();
    if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
        throw std::invalid_argument(
            "PictureBox image opacity must be finite and between zero and one");
    }
    if (image_opacity_ == opacity) {
        return;
    }
    image_opacity_ = opacity;
    invalidate(Dirty::paint | Dirty::semantics);
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
    switch (size_mode_) {
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
    if (size_mode_ == PictureBoxSizeMode::auto_size) {
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
    if (!has_valid_image() || image_opacity_ <= 0.0) {
        return;
    }
    const Rect destination = image_bounds();
    if (!destination.empty()) {
        painter.draw_image(image_, destination, image_opacity_);
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
    if (has_valid_image() && image_opacity_ > 0.0) {
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

} // namespace gui_forms
