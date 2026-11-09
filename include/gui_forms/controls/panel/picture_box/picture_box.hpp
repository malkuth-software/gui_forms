#pragma once

#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/controls/panel/picture_box/picture_box_values/picture_box_values.hpp"
#include "gui_forms/controls/panel/picture_box/picture_box_capabilities/picture_box_capabilities.hpp"

#include <cstdint>
#include <span>

namespace gui_forms {

class PictureBox : public Panel, public PictureBoxCapabilities<PictureBox> {
public:
    explicit PictureBox(StableId stable_id);

    // Resets image to none (the table's reset for image).
    void clear_image();
    [[nodiscard]] bool has_valid_image() const noexcept;
    [[nodiscard]] Size image_size() const noexcept;
    [[nodiscard]] Rect image_bounds() const noexcept;
    [[nodiscard]] Event<ImageId>& image_changed() noexcept { return image_changed_; }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members: Panel's parts and the image.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry image;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    [[nodiscard]] Rect content_bounds() const noexcept;
    [[nodiscard]] bool should_serialize_image() const noexcept;

    Event<ImageId> image_changed_;
    // property_codemod begin PictureBox (tools/property_families/picture_box.json; generated, do not edit)
public:
    // PictureBox's property table: Panel's entries, then its own.
    static constexpr std::size_t picture_box_property_count = Panel::panel_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, picture_box_property_count>& picture_box_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_size_mode_changed();

private:
    friend struct PictureBoxImageCapability<PictureBox>;
    friend struct PictureBoxSizeModePropertyCapability<PictureBox>;
    friend struct PictureBoxImageOpacityCapability<PictureBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(image)& cell, ImageId& incoming);
    void written(const decltype(image)& cell);
    void prepare_write(const decltype(size_mode)& cell, PictureBoxSizeMode& incoming);
    void written(const decltype(size_mode)& cell);
    void prepare_write(const decltype(image_opacity)& cell, double& incoming);
    void written(const decltype(image_opacity)& cell);
    // property_codemod end PictureBox
};

} // namespace gui_forms
