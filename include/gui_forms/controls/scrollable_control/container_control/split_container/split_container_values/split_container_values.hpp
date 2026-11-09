#pragma once

// SplitContainer's value types, apart from the control so its capabilities
// header can name them.

#include <cstdint>

namespace gui_forms {

enum class SplitFixedPanel : std::uint8_t {
    none,
    first,
    second,
};

enum class SplitChangeReason : std::uint8_t {
    programmatic,
    pointer,
    keyboard,
    semantic,
    cancel,
    collapse,
    container_resize,
};

enum class SplitCollapseOrigin : std::uint8_t {
    none,
    programmatic,
    user,
    automatic_accommodation,
};

struct SplitChangeEvent final {
    double old_distance{};
    double new_distance{};
    SplitChangeReason reason{SplitChangeReason::programmatic};
    SplitCollapseOrigin collapse_origin{SplitCollapseOrigin::none};
};

// A split seam may retain an authored logical thickness or collapse its paint
// to one physical device pixel. Hit extents remain logical in both modes so a
// crisp line never becomes the only usable pointer target.
enum class SplitSeamThicknessPolicy : std::uint8_t {
    logical,
    device_pixel_hairline,
};

enum class SplitSeamState : std::uint8_t {
    idle,
    near,
    hot,
    dragging,
    focused,
    disabled,
    collapsed,
};

struct SplitSeamGeometry final {
    static constexpr double maximum_visible_thickness = 64.0;
    static constexpr double maximum_hit_extension = 128.0;
    static constexpr double maximum_hit_target = 256.0;

    double visible_thickness{3.0};
    SplitSeamThicknessPolicy thickness_policy{
        SplitSeamThicknessPolicy::logical};
    double hit_before{3.0};
    double hit_after{3.0};
    double minimum_hit_target{9.0};

    friend constexpr bool operator==(const SplitSeamGeometry& left,
                                     const SplitSeamGeometry& right) noexcept {
        return left.visible_thickness == right.visible_thickness &&
            left.thickness_policy == right.thickness_policy &&
            left.hit_before == right.hit_before &&
            left.hit_after == right.hit_after &&
            left.minimum_hit_target == right.minimum_hit_target;
    }
};

} // namespace gui_forms
