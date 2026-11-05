#include "gui_forms/control.hpp"

#include "gui_forms/binding.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "../../display/chunk/display_chunk.hpp"
#include "../../theme/theme_compiler/control_inline_style.hpp"
#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "gui_forms/theme/gui_forms_theme/gui_forms_theme.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <stdexcept>

namespace gui_forms {

namespace {

[[nodiscard]] Rect owner_decoration_box(
    const OwnerDecorationRecipe& decoration, Rect owner) noexcept {
    const double x = decoration.left
        ? *decoration.left
        : owner.width - *decoration.right - decoration.size.width;
    const double y = decoration.top
        ? *decoration.top
        : owner.height - *decoration.bottom - decoration.size.height;
    return {x, y, decoration.size.width, decoration.size.height};
}

[[nodiscard]] Point rotate_owner_decoration_point(
    Point point, Rect box, double degrees) noexcept {
    const double radians = degrees * 3.14159265358979323846 / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const Point center{box.x + box.width * 0.5, box.y + box.height * 0.5};
    const double x = point.x - center.x;
    const double y = point.y - center.y;
    return {center.x + x * cosine - y * sine,
            center.y + x * sine + y * cosine};
}

[[nodiscard]] Rect owner_decoration_visual_bounds(
    const OwnerDecorationRecipe& decoration, Rect owner) noexcept {
    const Rect box = owner_decoration_box(decoration, owner);
    const Point corners[]{
        rotate_owner_decoration_point({box.x, box.y}, box,
                                      decoration.rotation_degrees),
        rotate_owner_decoration_point({box.x + box.width, box.y}, box,
                                      decoration.rotation_degrees),
        rotate_owner_decoration_point(
            {box.x + box.width, box.y + box.height}, box,
            decoration.rotation_degrees),
        rotate_owner_decoration_point({box.x, box.y + box.height}, box,
                                      decoration.rotation_degrees),
    };
    double minimum_x = corners[0].x;
    double maximum_x = corners[0].x;
    double minimum_y = corners[0].y;
    double maximum_y = corners[0].y;
    for (std::size_t index = 1U; index < 4U; ++index) {
        minimum_x = std::min(minimum_x, corners[index].x);
        maximum_x = std::max(maximum_x, corners[index].x);
        minimum_y = std::min(minimum_y, corners[index].y);
        maximum_y = std::max(maximum_y, corners[index].y);
    }
    return {minimum_x, minimum_y,
            maximum_x - minimum_x, maximum_y - minimum_y};
}

void draw_owner_decoration_edge(
    Painter& painter, Point from, Point to, const MaterialBorder& border,
    Rect box, double rotation_degrees) {
    painter.draw_line(
        rotate_owner_decoration_point(from, box, rotation_degrees),
        rotate_owner_decoration_point(to, box, rotation_degrees),
        border.color, border.width);
}

[[nodiscard]] constexpr bool valid_bounds_specified(
    BoundsSpecified value) noexcept {
    return (static_cast<std::uint8_t>(value) &
            ~static_cast<std::uint8_t>(BoundsSpecified::all)) == 0U;
}

[[nodiscard]] constexpr bool valid_child_skip(
    GetChildAtPointSkip value) noexcept {
    constexpr std::uint8_t all =
        static_cast<std::uint8_t>(GetChildAtPointSkip::invisible) |
        static_cast<std::uint8_t>(GetChildAtPointSkip::disabled) |
        static_cast<std::uint8_t>(GetChildAtPointSkip::transparent);
    return (static_cast<std::uint8_t>(value) & ~all) == 0U;
}

[[nodiscard]] constexpr bool valid_binding_value_kind(
    BindingValueKind value) noexcept {
    return value == BindingValueKind::null ||
        value == BindingValueKind::boolean ||
        value == BindingValueKind::signed_integer ||
        value == BindingValueKind::unsigned_integer ||
        value == BindingValueKind::number ||
        value == BindingValueKind::text ||
        value == BindingValueKind::point ||
        value == BindingValueKind::size ||
        value == BindingValueKind::rectangle ||
        value == BindingValueKind::insets ||
        value == BindingValueKind::color ||
        value == BindingValueKind::font ||
        value == BindingValueKind::image ||
        value == BindingValueKind::enumeration ||
        value == BindingValueKind::object ||
        value == BindingValueKind::collection;
}

std::shared_ptr<const PropertyEnumDescriptor> dock_style_property_enum_impl() {
    static const std::shared_ptr<const gui_forms::PropertyEnumDescriptor> value = std::make_shared<const PropertyEnumDescriptor>(
        PropertyEnumDescriptor{
            "dock_style",
            {{"none", 0}, {"top", 1}, {"bottom", 2}, {"left", 3},
             {"right", 4}, {"fill", 5}},
            false});
    return value;
}

std::shared_ptr<const PropertyEnumDescriptor> anchor_styles_property_enum_impl() {
    static const std::shared_ptr<const gui_forms::PropertyEnumDescriptor> value = std::make_shared<const PropertyEnumDescriptor>(
        PropertyEnumDescriptor{
            "anchor_styles",
            {{"none", 0}, {"top", 1}, {"bottom", 2}, {"left", 4},
             {"right", 8}},
            true});
    return value;
}

std::shared_ptr<const PropertyEnumDescriptor> auto_size_mode_property_enum_impl() {
    static const std::shared_ptr<const gui_forms::PropertyEnumDescriptor> value = std::make_shared<const PropertyEnumDescriptor>(
        PropertyEnumDescriptor{
            "auto_size_mode",
            {{"grow_and_shrink", 0}, {"grow_only", 1}},
            false});
    return value;
}

std::shared_ptr<const PropertyEnumDescriptor> layout_direction_property_enum_impl() {
    static const std::shared_ptr<const gui_forms::PropertyEnumDescriptor> value = std::make_shared<const PropertyEnumDescriptor>(
        PropertyEnumDescriptor{
            "layout_direction",
            {{"inherit", 0}, {"left_to_right", 1}, {"right_to_left", 2}},
            false});
    return value;
}

BindingValue current_enum_property_value_impl(
    std::shared_ptr<const PropertyEnumDescriptor> enumeration,
    std::int64_t value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = std::move(enumeration);
    const std::optional<BindingValue> normalized = convert_property_value(BindingValue{value}, descriptor);
    if (!normalized) {
        throw std::logic_error(
            "GUI.Forms retained enum state is outside its property schema");
    }
    return *normalized;
}

[[nodiscard]] Size unconstrained_if_zero(Size proposed) {
    if (!std::isfinite(proposed.width) || !std::isfinite(proposed.height) ||
        proposed.width < 0.0 || proposed.height < 0.0) {
        throw std::invalid_argument(
            "GUI.Forms proposed size must be finite and nonnegative");
    }
    constexpr double practical_unbounded_extent = 1.0e9;
    if (proposed.width == 0.0) proposed.width = practical_unbounded_extent;
    if (proposed.height == 0.0) proposed.height = practical_unbounded_extent;
    return proposed;
}

[[nodiscard]] char32_t decode_utf8_scalar(std::string_view text,
                                          std::size_t offset,
                                          std::size_t& length) noexcept {
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    if (lead < 0x80U) {
        length = 1U;
        return static_cast<char32_t>(lead);
    }
    if ((lead & 0xE0U) == 0xC0U) length = 2U;
    else if ((lead & 0xF0U) == 0xE0U) length = 3U;
    else if ((lead & 0xF8U) == 0xF0U) length = 4U;
    else {
        length = 1U;
        return static_cast<char32_t>(lead);
    }
    if (offset + length > text.size()) {
        length = 1U;
        return static_cast<char32_t>(lead);
    }
    char32_t value = static_cast<char32_t>(
        lead & (length == 2U ? 0x1FU : length == 3U ? 0x0FU : 0x07U));
    for (std::size_t index = 1U; index < length; ++index) {
        const unsigned char continuation =
            static_cast<unsigned char>(text[offset + index]);
        if ((continuation & 0xC0U) != 0x80U) {
            length = 1U;
            return static_cast<char32_t>(lead);
        }
        value = (value << 6U) | static_cast<char32_t>(continuation & 0x3FU);
    }
    return value;
}

[[nodiscard]] constexpr char32_t fold_mnemonic(char32_t value) noexcept {
    return value >= U'A' && value <= U'Z' ? value + (U'a' - U'A') : value;
}

// The slot a child docked at `dock` takes from `remaining`, which loses it:
// an edge takes the child's `width` or `height` (clamped to what remains)
// along that edge, fill takes everything, and none takes nothing.
Rect take_dock_slot(DockStyle dock, double width, double height, Rect& remaining) noexcept {
    Rect slot = remaining;
    switch (dock) {
    case DockStyle::top: {
        const double extent = std::clamp(height, 0.0, remaining.height);
        slot.height = extent;
        remaining.y += extent;
        remaining.height -= extent;
        return slot;
    }
    case DockStyle::bottom: {
        const double extent = std::clamp(height, 0.0, remaining.height);
        slot.y = remaining.y + remaining.height - extent;
        slot.height = extent;
        remaining.height -= extent;
        return slot;
    }
    case DockStyle::left: {
        const double extent = std::clamp(width, 0.0, remaining.width);
        slot.width = extent;
        remaining.x += extent;
        remaining.width -= extent;
        return slot;
    }
    case DockStyle::right: {
        const double extent = std::clamp(width, 0.0, remaining.width);
        slot.x = remaining.x + remaining.width - extent;
        slot.width = extent;
        remaining.width -= extent;
        return slot;
    }
    case DockStyle::fill:
        remaining = {remaining.x, remaining.y, 0.0, 0.0};
        return slot;
    case DockStyle::none:
        // Undocked children are placed by their anchors, not here.
        return slot;
    }
    // A style outside the enumeration docks nowhere and takes nothing.
    return slot;
}

// `bounds` mirrored about the vertical centre line of `frame`: right to
// left layout is computed left to right and mirrored once, at output.
Rect mirror_horizontally(Rect bounds, Rect frame) noexcept {
    Rect mirrored = bounds;
    mirrored.x = frame.x + frame.width - (bounds.x - frame.x) - bounds.width;
    return mirrored;
}

// The cursor property's choices: inherit, then each CursorKind at its
// underlying value plus one.
std::shared_ptr<const PropertyEnumDescriptor> cursor_kind_property_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "cursor_kind",
            {{"inherit", 0}, {"arrow", 1}, {"text", 2}, {"hand", 3}, {"crosshair", 4},
             {"resize_horizontal", 5}, {"resize_vertical", 6}, {"wait", 7}, {"forbidden", 8},
             {"resize_diagonal_down", 9}, {"resize_diagonal_up", 10}},
            false});
    return value;
}

} // namespace

BindingValue Control::cursor_property_value() const {
    std::int64_t value = 0;
    if (cursor_) {
        value = static_cast<std::int64_t>(*cursor_) + 1;
    }
    const BindingValue current = current_enum_property_value(cursor_kind_property_enum(), value);
    return current;
}

void Control::set_cursor_property_value(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    if (choice.value == 0) {
        set_cursor(std::nullopt);
        return;
    }
    set_cursor(static_cast<CursorKind>(choice.value - 1));
}

BindingValue Control::back_color_property_value() const {
    if (!back_color_) {
        return BindingValue{};
    }
    return BindingValue{*back_color_};
}

void Control::set_back_color_property_value(const BindingValue& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        clear_back_color();
        return;
    }
    set_back_color(std::get<Color>(value));
}

BindingValue Control::fore_color_property_value() const {
    if (!fore_color_) {
        return BindingValue{};
    }
    return BindingValue{*fore_color_};
}

void Control::set_fore_color_property_value(const BindingValue& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        clear_fore_color();
        return;
    }
    set_fore_color(std::get<Color>(value));
}

std::shared_ptr<const PropertyEnumDescriptor>
Control::dock_style_property_enum() {
    return dock_style_property_enum_impl();
}

std::shared_ptr<const PropertyEnumDescriptor>
Control::anchor_styles_property_enum() {
    return anchor_styles_property_enum_impl();
}

std::shared_ptr<const PropertyEnumDescriptor>
Control::auto_size_mode_property_enum() {
    return auto_size_mode_property_enum_impl();
}

std::shared_ptr<const PropertyEnumDescriptor>
Control::layout_direction_property_enum() {
    return layout_direction_property_enum_impl();
}

BindingValue Control::current_enum_property_value(
    std::shared_ptr<const PropertyEnumDescriptor> enumeration,
    std::int64_t value) {
    return current_enum_property_value_impl(std::move(enumeration), value);
}

MnemonicText parse_mnemonic_text(std::string_view text) {
    MnemonicText result;
    result.display_text.reserve(text.size());
    for (std::size_t offset = 0U; offset < text.size();) {
        if (text[offset] != '&') {
            std::size_t length{};
            static_cast<void>(decode_utf8_scalar(text, offset, length));
            result.display_text.append(text.substr(offset, length));
            offset += length;
            continue;
        }
        if (offset + 1U < text.size() && text[offset + 1U] == '&') {
            result.display_text.push_back('&');
            offset += 2U;
            continue;
        }
        ++offset;
        if (offset >= text.size()) {
            // A terminal marker has no target and is retained literally.
            result.display_text.push_back('&');
            break;
        }
        std::size_t length{};
        const char32_t scalar = decode_utf8_scalar(text, offset, length);
        if (!result.mnemonic) result.mnemonic = scalar;
        result.display_text.append(text.substr(offset, length));
        offset += length;
    }
    return result;
}

std::string mnemonic_access_key(std::string_view text) {
    for (std::size_t offset = 0U; offset < text.size();) {
        if (text[offset] != '&') {
            std::size_t length{};
            static_cast<void>(decode_utf8_scalar(text, offset, length));
            offset += length;
            continue;
        }
        if (offset + 1U < text.size() && text[offset + 1U] == '&') {
            offset += 2U;
            continue;
        }
        ++offset;
        if (offset >= text.size()) break;
        std::size_t length{};
        static_cast<void>(decode_utf8_scalar(text, offset, length));
        const std::string key(text.substr(offset, length));
        return key;
    }
    return {};
}

bool is_mnemonic(char32_t character, std::string_view text) noexcept {
    if (character == U'&') return false;
    for (std::size_t offset = 0U; offset < text.size();) {
        if (text[offset] != '&') {
            std::size_t length{};
            static_cast<void>(decode_utf8_scalar(text, offset, length));
            offset += length;
            continue;
        }
        if (offset + 1U < text.size() && text[offset + 1U] == '&') {
            offset += 2U;
            continue;
        }
        ++offset;
        if (offset >= text.size()) return false;
        std::size_t length{};
        const char32_t scalar = decode_utf8_scalar(text, offset, length);
        if (fold_mnemonic(character) == fold_mnemonic(scalar)) return true;
        offset += length;
    }
    return false;
}

void append_semantic_description(SemanticDescriptor& descriptor,
                                 std::string_view prefix,
                                 std::string_view value) {
    if (value.empty()) return;
    if (!descriptor.description.empty()) descriptor.description += "\n";
    descriptor.description.append(prefix);
    descriptor.description.append(value);
}

std::atomic<std::uint64_t> Control::next_runtime_id_{1};

Control::Control(StableId stable_id)
    : runtime_id_{next_runtime_id_.fetch_add(1, std::memory_order_relaxed)},
      stable_id_(std::move(stable_id)), name_(stable_id_.value()) {
    define_bindable_property({
        {"name", BindingValueKind::text, "Design",
         "Stable authoring name used by inspection and generated surfaces.",
         BindingValue{name_}, Dirty::semantics | Dirty::accessibility},
        RegisteredPropertyGetter<RegisteredProperty::name>{this},
        RegisteredPropertySetter<RegisteredProperty::name>{this},
        RegisteredPropertyConnector<RegisteredProperty::name>{this}, {}, {}});
    define_bindable_property({
        {"visible", BindingValueKind::boolean, "Behavior",
         "Whether this control participates in retained presentation and input.",
         BindingValue{true}, invalidation::visibility, true},
        RegisteredPropertyGetter<RegisteredProperty::visible>{this},
        RegisteredPropertySetter<RegisteredProperty::visible>{this},
        RegisteredPropertyConnector<RegisteredProperty::visible>{this}, {},
        {}});
    define_bindable_property({
        {"enabled", BindingValueKind::boolean, "Behavior",
         "Whether this control can receive ordinary user input.",
         BindingValue{true}, invalidation::enabled},
        RegisteredPropertyGetter<RegisteredProperty::enabled>{this},
        RegisteredPropertySetter<RegisteredProperty::enabled>{this},
        RegisteredPropertyConnector<RegisteredProperty::enabled>{this}, {},
        {}});
    define_bindable_property({
        {"auto_size", BindingValueKind::boolean, "Layout",
         "Whether retained measurement determines this control's size.",
         BindingValue{false}, Dirty::measure | Dirty::arrange |
             Dirty::hit_test | Dirty::semantics | Dirty::accessibility},
        RegisteredPropertyGetter<RegisteredProperty::auto_size>{this},
        RegisteredPropertySetter<RegisteredProperty::auto_size>{this},
        RegisteredPropertyConnector<RegisteredProperty::auto_size>{this}, {},
        {}});
    define_bindable_property({
        {"causes_validation", BindingValueKind::boolean, "Behavior",
         "Whether moving focus from this control initiates validation.",
         BindingValue{true}, Dirty::none},
        RegisteredPropertyGetter<RegisteredProperty::causes_validation>{this},
        RegisteredPropertySetter<RegisteredProperty::causes_validation>{this},
        RegisteredPropertyConnector<RegisteredProperty::causes_validation>{
            this},
        {}, {}});
    define_bindable_property({
        {"tool_tip_text", BindingValueKind::text, "Behavior",
         "Plain tooltip text the window's presenter shows; empty never shows.",
         BindingValue{std::string{}}, Dirty::semantics},
        RegisteredPropertyGetter<RegisteredProperty::tool_tip_text>{this},
        RegisteredPropertySetter<RegisteredProperty::tool_tip_text>{this},
        RegisteredPropertyConnector<RegisteredProperty::tool_tip_text>{this},
        {}, {}});

    PropertyDescriptor cursor;
    cursor.name = "cursor";
    cursor.kind = BindingValueKind::enumeration;
    cursor.category = "Appearance";
    cursor.description = "The pointer's cursor over this control; inherit uses the parent's.";
    cursor.invalidation_effects = Dirty::none;
    cursor.bindable = false;
    cursor.enumeration = cursor_kind_property_enum();
    define_bindable_property({
        std::move(cursor),
        detail::BoundMemberFunction<BindingValue (Control::*)() const>(*this, &Control::cursor_property_value),
        detail::BoundMemberFunction<void (Control::*)(const BindingValue&)>(*this,
                                                                             &Control::set_cursor_property_value),
        {}, {}, {}});

    // The authored colours (BackColor, ForeColor): null when the control has
    // none of its own, so it shows its parent's (ambient_back_color).
    PropertyDescriptor back_color;
    back_color.name = "back_color";
    back_color.kind = BindingValueKind::color;
    back_color.category = "Appearance";
    back_color.description = "The control's own background colour; null takes its parent's.";
    back_color.invalidation_effects = Dirty::paint;
    back_color.invalidates_subtree = true;
    back_color.nullable = true;
    define_bindable_property({
        std::move(back_color),
        detail::BoundMemberFunction<BindingValue (Control::*)() const>(*this, &Control::back_color_property_value),
        detail::BoundMemberFunction<void (Control::*)(const BindingValue&)>(*this,
                                                                             &Control::set_back_color_property_value),
        {}, detail::BoundMemberFunction<void (Control::*)()>(*this, &Control::clear_back_color), {}, {}});

    PropertyDescriptor fore_color;
    fore_color.name = "fore_color";
    fore_color.kind = BindingValueKind::color;
    fore_color.category = "Appearance";
    fore_color.description = "The control's own text colour; null takes its parent's.";
    fore_color.invalidation_effects = Dirty::paint;
    fore_color.invalidates_subtree = true;
    fore_color.nullable = true;
    define_bindable_property({
        std::move(fore_color),
        detail::BoundMemberFunction<BindingValue (Control::*)() const>(*this, &Control::fore_color_property_value),
        detail::BoundMemberFunction<void (Control::*)(const BindingValue&)>(*this,
                                                                             &Control::set_fore_color_property_value),
        {}, detail::BoundMemberFunction<void (Control::*)()>(*this, &Control::clear_fore_color), {}, {}});

    PropertyDescriptor bounds;
    bounds.name = "bounds";
    bounds.kind = BindingValueKind::rectangle;
    bounds.category = "Layout";
    bounds.description = "Authored logical bounds before parent layout.";
    bounds.default_value = BindingValue{Rect{}};
    bounds.invalidation_effects = invalidation::bounds;
    define_structural_property(
        std::move(bounds),
        RegisteredPropertyGetter<RegisteredProperty::bounds>{this},
        RegisteredPropertySetter<RegisteredProperty::bounds>{this});

    PropertyDescriptor minimum_size;
    minimum_size.name = "minimum_size";
    minimum_size.kind = BindingValueKind::size;
    minimum_size.category = "Layout";
    minimum_size.description = "Minimum retained layout size.";
    minimum_size.default_value = BindingValue{Size{}};
    minimum_size.invalidation_effects = invalidation::bounds;
    define_structural_property(
        std::move(minimum_size),
        RegisteredPropertyGetter<RegisteredProperty::minimum_size>{this},
        RegisteredPropertySetter<RegisteredProperty::minimum_size>{this});

    PropertyDescriptor maximum_size;
    maximum_size.name = "maximum_size";
    maximum_size.kind = BindingValueKind::size;
    maximum_size.category = "Layout";
    maximum_size.description =
        "Maximum retained layout size; zero dimensions are unbounded.";
    maximum_size.default_value = BindingValue{Size{}};
    maximum_size.invalidation_effects = invalidation::bounds;
    define_structural_property(
        std::move(maximum_size),
        RegisteredPropertyGetter<RegisteredProperty::maximum_size>{this},
        RegisteredPropertySetter<RegisteredProperty::maximum_size>{this});

    PropertyDescriptor margin;
    margin.name = "margin";
    margin.kind = BindingValueKind::insets;
    margin.category = "Layout";
    margin.description = "External logical spacing requested from a layout owner.";
    margin.default_value = BindingValue{Insets{3.0, 3.0, 3.0, 3.0}};
    margin.invalidation_effects = Dirty::measure | Dirty::arrange |
        Dirty::hit_test | Dirty::semantics | Dirty::accessibility;
    define_structural_property(
        std::move(margin),
        RegisteredPropertyGetter<RegisteredProperty::margin>{this},
        RegisteredPropertySetter<RegisteredProperty::margin>{this});

    PropertyDescriptor padding;
    padding.name = "padding";
    padding.kind = BindingValueKind::insets;
    padding.category = "Layout";
    padding.description = "Internal logical spacing around child content.";
    padding.default_value = BindingValue{Insets{}};
    padding.invalidation_effects = invalidation::bounds;
    define_structural_property(
        std::move(padding),
        RegisteredPropertyGetter<RegisteredProperty::padding>{this},
        RegisteredPropertySetter<RegisteredProperty::padding>{this});

    PropertyDescriptor auto_scroll_offset;
    auto_scroll_offset.name = "auto_scroll_offset";
    auto_scroll_offset.kind = BindingValueKind::point;
    auto_scroll_offset.category = "Layout";
    auto_scroll_offset.description =
        "Logical offset used when revealing this control in a scroll owner.";
    auto_scroll_offset.default_value = BindingValue{Point{}};
    auto_scroll_offset.invalidation_effects =
        Dirty::semantics | Dirty::accessibility;
    define_structural_property(
        std::move(auto_scroll_offset),
        RegisteredPropertyGetter<RegisteredProperty::auto_scroll_offset>{this},
        RegisteredPropertySetter<RegisteredProperty::auto_scroll_offset>{
            this});

    PropertyDescriptor dock;
    dock.name = "dock";
    dock.kind = BindingValueKind::enumeration;
    dock.category = "Layout";
    dock.description = "Edge or fill participation in parent layout.";
    dock.default_value = BindingValue{PropertyEnumValue{
        "dock_style", "none", 0}};
    dock.invalidation_effects = invalidation::bounds;
    dock.enumeration = dock_style_property_enum();
    define_structural_property(
        std::move(dock),
        RegisteredPropertyGetter<RegisteredProperty::dock>{this},
        RegisteredPropertySetter<RegisteredProperty::dock>{this});

    PropertyDescriptor anchor;
    anchor.name = "anchor";
    anchor.kind = BindingValueKind::enumeration;
    anchor.category = "Layout";
    anchor.description = "Flags retaining distances to parent client edges.";
    anchor.default_value = BindingValue{PropertyEnumValue{
        "anchor_styles", "top, left", 5}};
    anchor.invalidation_effects = invalidation::bounds;
    anchor.enumeration = anchor_styles_property_enum();
    define_structural_property(
        std::move(anchor),
        RegisteredPropertyGetter<RegisteredProperty::anchor>{this},
        RegisteredPropertySetter<RegisteredProperty::anchor>{this});

    PropertyDescriptor auto_size_mode;
    auto_size_mode.name = "auto_size_mode";
    auto_size_mode.kind = BindingValueKind::enumeration;
    auto_size_mode.category = "Layout";
    auto_size_mode.description = "Whether automatic sizing may shrink.";
    auto_size_mode.default_value = BindingValue{PropertyEnumValue{
        "auto_size_mode", "grow_only", 1}};
    auto_size_mode.invalidation_effects = Dirty::measure | Dirty::arrange |
        Dirty::hit_test | Dirty::semantics | Dirty::accessibility;
    auto_size_mode.enumeration = auto_size_mode_property_enum();
    define_structural_property(
        std::move(auto_size_mode),
        RegisteredPropertyGetter<RegisteredProperty::auto_size_mode>{this},
        RegisteredPropertySetter<RegisteredProperty::auto_size_mode>{this});

    PropertyDescriptor layout_direction;
    layout_direction.name = "layout_direction";
    layout_direction.kind = BindingValueKind::enumeration;
    layout_direction.category = "Layout";
    layout_direction.description =
        "Inline direction of child layout: inherited, left to right, or mirrored right to left.";
    layout_direction.default_value = BindingValue{PropertyEnumValue{
        "layout_direction", "inherit", 0}};
    layout_direction.invalidation_effects = Dirty::arrange | Dirty::paint |
        Dirty::hit_test | Dirty::semantics | Dirty::accessibility;
    layout_direction.enumeration = layout_direction_property_enum();
    define_structural_property(
        std::move(layout_direction),
        RegisteredPropertyGetter<RegisteredProperty::layout_direction>{this},
        RegisteredPropertySetter<RegisteredProperty::layout_direction>{this});

    PropertyDescriptor tab_index;
    tab_index.name = "tab_index";
    tab_index.kind = BindingValueKind::unsigned_integer;
    tab_index.category = "Behavior";
    tab_index.description = "Keyboard traversal order within the retained tree.";
    tab_index.default_value = BindingValue{std::uint64_t{0}};
    tab_index.invalidation_effects = Dirty::semantics | Dirty::accessibility;
    define_structural_property(
        std::move(tab_index),
        RegisteredPropertyGetter<RegisteredProperty::tab_index>{this},
        RegisteredPropertySetter<RegisteredProperty::tab_index>{this});

    PropertyDescriptor tab_stop;
    tab_stop.name = "tab_stop";
    tab_stop.kind = BindingValueKind::boolean;
    tab_stop.category = "Behavior";
    tab_stop.description = "Whether keyboard traversal may focus this control.";
    tab_stop.default_value = BindingValue{true};
    tab_stop.invalidation_effects = Dirty::semantics | Dirty::accessibility;
    define_structural_property(
        std::move(tab_stop),
        RegisteredPropertyGetter<RegisteredProperty::tab_stop>{this},
        RegisteredPropertySetter<RegisteredProperty::tab_stop>{this});

    PropertyDescriptor allow_drop;
    allow_drop.name = "allow_drop";
    allow_drop.kind = BindingValueKind::boolean;
    allow_drop.category = "Behavior";
    allow_drop.description = "Whether this control may receive drag data.";
    allow_drop.default_value = BindingValue{false};
    allow_drop.invalidation_effects = Dirty::semantics | Dirty::hit_test;
    define_structural_property(
        std::move(allow_drop),
        RegisteredPropertyGetter<RegisteredProperty::allow_drop>{this},
        RegisteredPropertySetter<RegisteredProperty::allow_drop>{this});

    PropertyDescriptor hit_test_transparent;
    hit_test_transparent.name = "hit_test_transparent";
    hit_test_transparent.kind = BindingValueKind::boolean;
    hit_test_transparent.category = "Behavior";
    hit_test_transparent.description =
        "Whether ordinary pointer routing passes through this control.";
    hit_test_transparent.default_value = BindingValue{false};
    hit_test_transparent.invalidation_effects =
        Dirty::hit_test | Dirty::semantics | Dirty::accessibility;
    hit_test_transparent.browsable = false;
    define_structural_property(
        std::move(hit_test_transparent),
        RegisteredPropertyGetter<RegisteredProperty::hit_test_transparent>{
            this},
        RegisteredPropertySetter<RegisteredProperty::hit_test_transparent>{
            this});

    PropertyDescriptor accessible_name;
    accessible_name.name = "accessible_name";
    accessible_name.kind = BindingValueKind::text;
    accessible_name.category = "Accessibility";
    accessible_name.description = "Authored assistive name override.";
    accessible_name.default_value = BindingValue{std::string{}};
    accessible_name.invalidation_effects = Dirty::semantics;
    define_structural_property(
        std::move(accessible_name),
        RegisteredPropertyGetter<RegisteredProperty::accessible_name>{this},
        RegisteredPropertySetter<RegisteredProperty::accessible_name>{this});

    PropertyDescriptor accessible_description;
    accessible_description.name = "accessible_description";
    accessible_description.kind = BindingValueKind::text;
    accessible_description.category = "Accessibility";
    accessible_description.description = "Authored assistive description.";
    accessible_description.default_value = BindingValue{std::string{}};
    accessible_description.invalidation_effects = Dirty::semantics;
    define_structural_property(
        std::move(accessible_description),
        RegisteredPropertyGetter<RegisteredProperty::accessible_description>{
            this},
        RegisteredPropertySetter<RegisteredProperty::accessible_description>{
            this});
}

void Control::define_structural_property(
    PropertyDescriptor descriptor, PropertyRegistration::Getter get,
    PropertyRegistration::Setter set) {
    descriptor.bindable = false;
    define_bindable_property({std::move(descriptor), std::move(get),
                              std::move(set), {}, {}, {}});
}

Control::~Control() = default;

double Control::effective_text_scale() const noexcept {
    return surface_root_ != nullptr
        ? (*surface_root_).presentation_settings().text_scale
        : 1.0;
}

FontSpec Control::effective_font(FontSpec authored) const noexcept {
    const double scale = effective_text_scale();
    authored.size *= scale;
    authored.letter_spacing *= scale;
    return authored;
}

ResolvedFont Control::resolved_text_font(FontSpec own) const noexcept {
    if (font_from_code_) {
        ResolvedFont coded{};
        coded.font = own;
        coded.from_code = true;
        return coded;
    }
    const Theme& theme = effective_theme();
    MediaFacts media{};
    const CompiledThemeRules* user_rules = nullptr;
    const Window* const window = attached_window();
    if (window != nullptr) media = window_media_facts(*window);
    if (window != nullptr) user_rules = (*window).user_theme_rules();
    const CompiledThemeRules* const inline_rules = inline_style_rules();
    TextFontCache& cache = text_font_cache_;
    const bool current = cache.valid && cache.own == own && cache.theme == &theme && cache.media == media &&
                         cache.user_rules == user_rules && cache.inline_rules == inline_rules;
    if (current) return cache.resolved;
    cache.resolved = theme.resolve_font(*this, own);
    cache.own = own;
    cache.theme = &theme;
    cache.media = media;
    cache.user_rules = user_rules;
    cache.inline_rules = inline_rules;
    cache.valid = true;
    return cache.resolved;
}

FontSpec Control::text_font(FontSpec own) const noexcept {
    const ResolvedFont resolved = resolved_text_font(own);
    const FontSpec scaled = effective_font(resolved.font);
    return scaled;
}

void Control::set_font_from_code(bool from_code) noexcept {
    font_from_code_ = from_code;
    text_font_cache_.valid = false;
}

Dirty Control::refresh_text_font() noexcept {
    TextFontCache& cache = text_font_cache_;
    if (!cache.valid) return Dirty::none;
    const ResolvedFont before = cache.resolved;
    cache.valid = false;
    const ResolvedFont after = resolved_text_font(cache.own);
    if (after.font == before.font) return Dirty::none;
    return Dirty::layout;
}

ResolvedTextLayout Control::resolve_text_layout_utf8(
    std::string_view utf8, FontSpec effective) const {
    return surface_root_ != nullptr
        ? (*surface_root_).resolve_text_layout_utf8(utf8, effective)
        : estimate_text_layout_utf8(utf8, effective);
}

TextResolutionStatus Control::fill_line_break_opportunities_utf8(
    std::string_view utf8, std::span<LineBreakOpportunity> opportunities) const {
    if (surface_root_ == nullptr) return estimate_line_break_opportunities_utf8(utf8, opportunities);
    const TextResolutionStatus status =
        (*surface_root_).fill_line_break_opportunities_utf8(utf8, opportunities);
    return status;
}

double Control::snap_text_baseline(double logical_baseline) const noexcept {
    return gui_forms::snap_text_baseline(
        logical_baseline, surface_root_ != nullptr ? (*surface_root_).scale() : 1.0);
}

const Theme& Control::effective_theme() const noexcept {
    const Theme& chosen = chosen_theme();
    if (surface_root_ == nullptr || chosen.supports((*surface_root_).presentation_settings().colour_scheme)) {
        return chosen;
    }
    // GUI.Forms' own theme has both variants; it lives for the program.
    const Theme& fallback = *gui_forms_theme_build().theme;
    return fallback;
}

const Theme& Control::chosen_theme() const noexcept {
    if (theme_override_) return *theme_override_;
    if (const Ptr visual_parent = parent_.lock()) return (*visual_parent).chosen_theme();
    if (surface_root_ != nullptr) return (*surface_root_).application_theme();
    return *default_theme();
}

bool Control::application_colours_suspended() const noexcept {
    if (surface_root_ == nullptr) return false;
    const bool suspended = !chosen_theme().supports((*surface_root_).presentation_settings().colour_scheme);
    return suspended;
}

void Control::warn_inline_colour(std::string_view property) const {
    theme_language::ThemeDiagnostic warning{};
    warning.code = theme_language::DiagnosticCode::inline_colour;
    warning.file = "inline:" + std::string(stable_id().value());
    warning.property = std::string(property);
    warning.detail = "set the colour in the theme, with a light and a dark variant";
    theme_language::ThemeDiagnosticList list{};
    list.add(std::move(warning));
    theme_language::write_theme_diagnostics(list, stderr);
}

const Theme& Control::inherited_theme() const noexcept {
    if (const Ptr visual_parent = parent_.lock()) {
        return (*visual_parent).effective_theme();
    }
    if (surface_root_ != nullptr) return (*surface_root_).theme();
    return *default_theme();
}

const KeyMap& Control::effective_key_map() const noexcept {
    // Made once, on first use; never modified.
    static const KeyMap detached_key_map = pc_key_map();
    if (surface_root_ != nullptr) return (*surface_root_).key_map();
    return detached_key_map;
}

void Control::set_theme_override(std::shared_ptr<const Theme> theme) {
    require_mutable();
    if (!theme) {
        throw std::invalid_argument("control theme override may not be null");
    }
    if ((*theme).definition().rule_set != ThemeRuleSet::program) {
        throw std::invalid_argument("a user theme is not a control theme override");
    }
    if (theme_override_ == theme) return;
    std::vector<detail::InlineRestyle> restyles{};
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    if (!collect_inline_restyles(*theme, restyles, diagnostics)) {
        throw std::invalid_argument(detail::inline_restyle_refusal("control theme override", diagnostics));
    }
    theme_override_ = std::move(theme);
    commit_inline_restyles(restyles);
    invalidate_subtree(invalidation::conservative_subtree);
}

void Control::clear_theme_override() {
    require_mutable();
    if (!theme_override_) return;
    std::vector<detail::InlineRestyle> restyles{};
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    if (!collect_inline_restyles(inherited_theme(), restyles, diagnostics)) {
        throw std::invalid_argument(detail::inline_restyle_refusal("control theme override removal", diagnostics));
    }
    theme_override_.reset();
    commit_inline_restyles(restyles);
    invalidate_subtree(invalidation::conservative_subtree);
}

bool Control::collect_inline_restyles(const Theme& theme, std::vector<detail::InlineRestyle>& restyles,
                                      std::vector<ThemeCompileDiagnostic>& diagnostics) {
    const std::uint64_t serial = theme.engine().rules().serial();
    if (inline_style_ != nullptr && (*inline_style_).theme_bound && (*inline_style_).rules.theme_serial() != serial) {
        const std::string file = "inline:" + std::string(stable_id().value());
        std::shared_ptr<const detail::ControlInlineStyle> compiled = detail::compile_inline_style(
            (*inline_style_).text, anatomy(), stable_id().value(), file, theme, diagnostics);
        if (compiled == nullptr) return false;
        restyles.push_back(detail::InlineRestyle{.control = this, .style = std::move(compiled)});
    }
    for (const Ptr& child : children_) {
        if ((*child).theme_override_) continue;
        if (!(*child).collect_inline_restyles(theme, restyles, diagnostics)) return false;
    }
    return true;
}

void Control::commit_inline_restyles(std::vector<detail::InlineRestyle>& restyles) {
    for (detail::InlineRestyle& restyle : restyles) {
        (*restyle.control).inline_style_ = std::move(restyle.style);
        (*restyle.control).invalidate(Dirty::style | Dirty::paint | (*restyle.control).refresh_text_font());
    }
    restyles.clear();
}

void Control::refresh_inline_style() {
    if (inline_style_ == nullptr || !(*inline_style_).theme_bound) return;
    const Theme& theme = effective_theme();
    if ((*inline_style_).rules.theme_serial() == theme.engine().rules().serial()) return;
    const std::string file = "inline:" + std::string(stable_id().value());
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    std::shared_ptr<const detail::ControlInlineStyle> compiled =
        detail::compile_inline_style((*inline_style_).text, anatomy(), stable_id().value(), file, theme, diagnostics);
    if (compiled == nullptr) {
        // Kept as it was, with one log line (owner, 2026-10-31).
        const std::string line = detail::inline_restyle_kept(file, diagnostics);
        std::fprintf(stderr, "%s\n", line.c_str());
        return;
    }
    inline_style_ = std::move(compiled);
    invalidate(Dirty::style | Dirty::paint | refresh_text_font());
}

void Control::set_authored_surface_material(SurfaceMaterial material) {
    require_mutable();
    if (!valid_surface_material(material)) {
        throw std::invalid_argument("control authored surface material is invalid");
    }
    validate_authored_surface_material_images(material);
    if (authored_surface_material_ && *authored_surface_material_ == material) {
        return;
    }
    authored_surface_material_ = std::move(material);
    invalidate(Dirty::style | Dirty::paint);
}

void Control::clear_authored_surface_material() {
    require_mutable();
    if (!authored_surface_material_) return;
    authored_surface_material_.reset();
    invalidate(Dirty::style | Dirty::paint);
}

void Control::set_back_color(Color color) {
    require_mutable();
    if (back_color_ == color) return;
    if (!back_color_warned_) {
        back_color_warned_ = true;
        warn_inline_colour("back_color");
    }
    back_color_ = color;
    invalidate_subtree(Dirty::paint);
}

void Control::clear_back_color() {
    require_mutable();
    if (!back_color_) return;
    back_color_.reset();
    invalidate_subtree(Dirty::paint);
}

void Control::set_fore_color(Color color) {
    require_mutable();
    if (fore_color_ == color) return;
    if (!fore_color_warned_) {
        fore_color_warned_ = true;
        warn_inline_colour("fore_color");
    }
    fore_color_ = color;
    invalidate_subtree(Dirty::paint);
}

void Control::clear_fore_color() {
    require_mutable();
    if (!fore_color_) return;
    fore_color_.reset();
    invalidate_subtree(Dirty::paint);
}

std::optional<Color> Control::ambient_back_color() const noexcept {
    for (const Control* current = this; current != nullptr;) {
        if ((*current).back_color_) return (*current).back_color_;
        const std::shared_ptr<Control> parent = (*current).parent_.lock();
        current = parent.get();
    }
    return std::nullopt;
}

std::optional<Color> Control::ambient_fore_color() const noexcept {
    for (const Control* current = this; current != nullptr;) {
        if ((*current).fore_color_) return (*current).fore_color_;
        const std::shared_ptr<Control> parent = (*current).parent_.lock();
        current = parent.get();
    }
    return std::nullopt;
}

void Control::set_owned_decorations(
    const OwnerDecorationRecipe* decorations, std::size_t decoration_count) {
    require_mutable();
    constexpr std::size_t maximum_owner_decorations = 8U;
    if (decoration_count > maximum_owner_decorations ||
        (decoration_count != 0U && decorations == nullptr)) {
        throw std::invalid_argument(
            "control owned decorations require zero through eight pointer/count recipes");
    }
    std::vector<OwnerDecorationRecipe> candidate;
    if (decoration_count != 0U) {
        candidate.assign(decorations, decorations + decoration_count);
    }
    for (const OwnerDecorationRecipe& decoration : candidate) {
        const bool valid_layer =
            decoration.layer == OwnerDecorationLayer::before_content ||
            decoration.layer == OwnerDecorationLayer::after_content;
        const bool horizontal = decoration.left.has_value() !=
            decoration.right.has_value();
        const bool vertical = decoration.top.has_value() !=
            decoration.bottom.has_value();
        const std::optional<double>* offsets[]{
            &decoration.left, &decoration.top,
            &decoration.right, &decoration.bottom,
        };
        bool valid_offsets = true;
        for (const std::optional<double>* offset : offsets) {
            if ((*offset).has_value() &&
                (!std::isfinite((*offset).value()) ||
                 std::abs((*offset).value()) > 4096.0)) {
                valid_offsets = false;
            }
        }
        const std::optional<MaterialBorder>* borders[]{
            &decoration.border_edges.top, &decoration.border_edges.right,
            &decoration.border_edges.bottom, &decoration.border_edges.left,
        };
        bool valid_borders = !decoration.border_edges.empty();
        for (const std::optional<MaterialBorder>* border : borders) {
            if ((*border).has_value() &&
                (!std::isfinite((*border).value().width) ||
                 (*border).value().width <= 0.0 ||
                 (*border).value().width > 64.0)) {
                valid_borders = false;
            }
        }
        if (!valid_layer || !std::isfinite(decoration.size.width) ||
            !std::isfinite(decoration.size.height) ||
            decoration.size.width <= 0.0 || decoration.size.height <= 0.0 ||
            decoration.size.width > 4096.0 ||
            decoration.size.height > 4096.0 || !horizontal || !vertical ||
            !valid_offsets || !std::isfinite(decoration.rotation_degrees) ||
            std::abs(decoration.rotation_degrees) > 360.0 || !valid_borders) {
            throw std::invalid_argument(
                "control owned decoration recipe is outside bounded geometry");
        }
    }
    if (owned_decorations_ == candidate) return;
    owned_decorations_ = std::move(candidate);
    invalidate(Dirty::style | Dirty::paint);
}

void Control::clear_owned_decorations() {
    set_owned_decorations(nullptr, 0U);
}

void Control::validate_authored_surface_material_images(
    const SurfaceMaterial& material) const {
    if (surface_root_ == nullptr) return;
    for (const MaterialFillLayer& fill : material.fills) {
        if (fill.kind != MaterialFillKind::image) continue;
        const std::optional<ImageResourceView> resource =
            (*surface_root_).image_resources().find(fill.image);
        if (!resource ||
            static_cast<double>((*resource).metadata.width) !=
                fill.image_pixel_size.width ||
            static_cast<double>((*resource).metadata.height) !=
                fill.image_pixel_size.height) {
            throw std::invalid_argument(
                "control authored surface image is missing or has stale pixel geometry");
        }
    }
}

void Control::set_visual_status(ControlVisualStatus status) {
    require_mutable();
    if (status != ControlVisualStatus::normal &&
        status != ControlVisualStatus::pending &&
        status != ControlVisualStatus::invalid) {
        throw std::invalid_argument("control visual status is invalid");
    }
    if (visual_status_ == status) return;
    visual_status_ = status;
    invalidate(Dirty::style | Dirty::paint | Dirty::semantics);
}

ControlVisualContext Control::visual_context(
    bool hovered, bool pressed, bool selected, bool focused,
    bool defaulted) const noexcept {
    ControlVisualContext result;
    result.selected = selected;
    result.focused = focused;
    result.defaulted = defaulted;
    result.high_contrast = surface_root_ != nullptr &&
                           (*surface_root_).presentation_settings().high_contrast;
    result.dark = surface_root_ != nullptr &&
                  (*surface_root_).presentation_settings().colour_scheme == ColourScheme::dark;
    if (!effectively_enabled()) {
        result.surface = ControlSurfaceState::disabled;
    } else if (visual_status_ == ControlVisualStatus::invalid) {
        result.surface = ControlSurfaceState::invalid;
    } else if (visual_status_ == ControlVisualStatus::pending) {
        result.surface = ControlSurfaceState::pending;
    } else if (pressed) {
        result.surface = ControlSurfaceState::pressed;
    } else if (hovered) {
        result.surface = ControlSurfaceState::hot;
    } else if (surface_root_ != nullptr && !(*surface_root_).active()) {
        result.surface = ControlSurfaceState::deactivated;
    }
    return result;
}

void Control::set_tag(std::any tag) {
    require_mutable();
    tag_ = std::move(tag);
}

void Control::define_bindable_property(BindableProperty property) {
    PropertyDescriptor& descriptor = property.descriptor;
    // Any spelling is accepted; the property is registered under its stored
    // snake_case name.
    const std::string canonical = registered_name(descriptor.name);
    if (!is_registered_name(canonical)) {
        throw std::invalid_argument(
            "GUI.Forms property name must convert to a snake_case registered name");
    }
    descriptor.name = canonical;
    if (!descriptor.readable && !descriptor.writable) {
        throw std::invalid_argument(
            "GUI.Forms property must be readable or writable");
    }
    if (!valid_binding_value_kind(descriptor.kind)) {
        throw std::invalid_argument(
            "GUI.Forms property value kind is invalid");
    }
    if ((descriptor.kind == BindingValueKind::enumeration) !=
        static_cast<bool>(descriptor.enumeration) ||
        (descriptor.enumeration &&
         !valid_property_enum_descriptor(*descriptor.enumeration))) {
        throw std::invalid_argument(
            "GUI.Forms enumeration property requires one valid enum descriptor");
    }
    if (!validate_utf8(descriptor.converter_name).valid() ||
        !validate_utf8(descriptor.editor_name).valid() ||
        !validate_utf8(descriptor.standard_values_provider_name).valid() ||
        descriptor.converter_name.size() > 256U ||
        descriptor.editor_name.size() > 256U ||
        descriptor.standard_values_provider_name.size() > 256U) {
        throw std::invalid_argument(
            "GUI.Forms property service names must be bounded valid UTF-8");
    }
    if (descriptor.standard_values.size() > maximum_property_standard_values ||
        (descriptor.standard_values_exclusive &&
         descriptor.standard_values.empty() &&
         descriptor.standard_values_provider_name.empty())) {
        throw std::invalid_argument(
            "GUI.Forms property standard-value contract is invalid or unbounded");
    }
    std::vector<BindingValue> normalized_standard_values;
    normalized_standard_values.reserve(descriptor.standard_values.size());
    for (const BindingValue& standard : descriptor.standard_values) {
        const std::optional<BindingValue> converted = convert_property_value(standard, descriptor);
        if (!converted ||
            std::find(normalized_standard_values.begin(),
                      normalized_standard_values.end(), *converted) !=
                normalized_standard_values.end()) {
            throw std::invalid_argument(
                "GUI.Forms property standard values must be unique and convertible");
        }
        normalized_standard_values.push_back(*converted);
    }
    descriptor.standard_values = std::move(normalized_standard_values);
    if (descriptor.readable && !property.get) {
        throw std::invalid_argument(
            "GUI.Forms readable property requires a getter");
    }
    if (descriptor.writable && !property.set) {
        throw std::invalid_argument(
            "GUI.Forms writable property requires a setter");
    }
    if (descriptor.serialization_visibility !=
            PropertySerializationVisibility::hidden &&
        descriptor.serialization_visibility !=
            PropertySerializationVisibility::visible &&
        descriptor.serialization_visibility !=
            PropertySerializationVisibility::content) {
        throw std::invalid_argument(
            "GUI.Forms property serialization visibility is invalid");
    }
    if (without_dirty(descriptor.invalidation_effects,
                      invalidation::conservative_subtree) != Dirty::none) {
        throw std::invalid_argument(
            "GUI.Forms property declares unknown invalidation effects");
    }
    if (descriptor.default_value) {
        const std::optional<BindingValue> converted = convert_property_value(
            *descriptor.default_value, descriptor);
        if (!converted) {
            throw std::invalid_argument(
                "GUI.Forms property default cannot convert to its declared kind");
        }
        descriptor.default_value = *converted;
    }
    descriptor.resettable = static_cast<bool>(property.reset) ||
        (descriptor.writable && descriptor.default_value.has_value());
    descriptor.change_notifications =
        static_cast<bool>(property.connect_changed);
    // Derived stock controls may replace a base descriptor when they own a
    // more specific implementation of the same canonical property.
    bindable_properties_.insert_or_assign(canonical, std::move(property));
}

void Control::clear_bindable_properties() {
    require_mutable();
    bindable_properties_.clear();
}

const BindableProperty* Control::find_bindable_property(
    std::string_view name) const {
    const BindablePropertyMap::const_iterator found =
        bindable_properties_.find(registered_name(name));
    return found == bindable_properties_.end() ||
            !(*found).second.descriptor.bindable
        ? nullptr : &(*found).second;
}

bool Control::has_bindable_property(std::string_view name) const {
    return find_bindable_property(name) != nullptr;
}

std::vector<std::string> Control::bindable_property_names() const {
    std::vector<std::string> result;
    result.reserve(bindable_properties_.size());
    for (const std::pair<const std::string, BindableProperty>& property_entry :
         bindable_properties_) {
        const BindableProperty& property = property_entry.second;
        if (property.descriptor.bindable) {
            result.push_back(property.descriptor.name);
        }
    }
    return result;
}

std::optional<PropertyDescriptor> Control::property_descriptor(
    std::string_view name) const {
    const BindablePropertyMap::const_iterator found =
        bindable_properties_.find(registered_name(name));
    if (found == bindable_properties_.end()) return std::nullopt;
    return (*found).second.descriptor;
}

std::vector<PropertyDescriptor> Control::property_descriptors() const {
    std::vector<PropertyDescriptor> result;
    result.reserve(bindable_properties_.size());
    for (const std::pair<const std::string, BindableProperty>& property_entry :
         bindable_properties_) {
        const BindableProperty& property = property_entry.second;
        result.push_back(property.descriptor);
    }
    return result;
}

std::optional<BindingValue> Control::property_value(std::string_view name) const {
    if (!is_alive()) {
        throw std::logic_error("GUI.Forms cannot query a disposed control property");
    }
    if (surface_root_) (*surface_root_).require_root_thread("control property query");
    const BindablePropertyMap::const_iterator found =
        bindable_properties_.find(registered_name(name));
    if (found == bindable_properties_.end() ||
        !(*found).second.descriptor.readable || !(*found).second.get) {
        return std::nullopt;
    }
    const PropertyRegistration::Getter getter = (*found).second.get;
    const BindingValue value = getter();
    const std::optional<BindingValue> normalized = convert_property_value(
        value, (*found).second.descriptor);
    if (!normalized || !valid_property_value_tree(*normalized)) {
        throw std::logic_error(
            "GUI.Forms property getter violated its declared schema: " +
            (*found).second.descriptor.name);
    }
    return normalized;
}

void Control::set_property_value(std::string_view name, BindingValue value) {
    require_mutable();
    const std::string canonical = registered_name(name);
    const BindablePropertyMap::iterator found =
        bindable_properties_.find(canonical);
    if (found == bindable_properties_.end()) {
        throw std::invalid_argument("GUI.Forms property is not registered: " +
                                    canonical);
    }
    const PropertyRegistration registration = (*found).second;
    if (!registration.descriptor.writable || !registration.set) {
        throw std::logic_error("GUI.Forms property is read-only: " +
                               registration.descriptor.name);
    }
    const std::optional<BindingValue> converted = convert_property_value(
        value, registration.descriptor);
    if (!converted) {
        throw std::invalid_argument(
            "GUI.Forms property value cannot convert to its declared kind: " +
            registration.descriptor.name);
    }
    registration.set(*converted);
}

SubscriptionToken Control::subscribe_property_changed(
    std::string_view name, Component& owner, std::function<void()> changed) {
    require_mutable();
    const std::string canonical = registered_name(name);
    const BindablePropertyMap::iterator found =
        bindable_properties_.find(canonical);
    if (found == bindable_properties_.end()) {
        throw std::invalid_argument("GUI.Forms property is not registered: " +
                                    canonical);
    }
    const PropertyRegistration::ChangeConnector connector =
        (*found).second.connect_changed;
    return connector && changed
        ? connector(owner, std::move(changed)) : SubscriptionToken{};
}

bool Control::reset_property(std::string_view name) {
    require_mutable();
    const BindablePropertyMap::iterator found =
        bindable_properties_.find(registered_name(name));
    if (found == bindable_properties_.end()) return false;
    const PropertyRegistration registration = (*found).second;
    if (registration.reset) {
        registration.reset();
        return true;
    }
    if (!registration.descriptor.writable || !registration.set ||
        !registration.descriptor.default_value) {
        return false;
    }
    registration.set(*registration.descriptor.default_value);
    return true;
}

bool Control::should_serialize_property(std::string_view name) const {
    if (!is_alive()) {
        throw std::logic_error(
            "GUI.Forms cannot inspect a disposed control property");
    }
    if (surface_root_) (*surface_root_).require_root_thread("control property inspection");
    const std::string canonical = registered_name(name);
    const BindablePropertyMap::const_iterator found =
        bindable_properties_.find(canonical);
    if (found == bindable_properties_.end()) {
        throw std::invalid_argument("GUI.Forms property is not registered: " +
                                    canonical);
    }
    const PropertyRegistration registration = (*found).second;
    if (registration.descriptor.serialization_visibility ==
        PropertySerializationVisibility::hidden) {
        return false;
    }
    if (registration.should_serialize) {
        return registration.should_serialize();
    }
    if (!registration.descriptor.readable || !registration.get) return false;
    if (!registration.descriptor.default_value) return true;
    return registration.get() != *registration.descriptor.default_value;
}

PropertyValueOrigin Control::property_value_origin(
    std::string_view name) const {
    if (!is_alive()) {
        throw std::logic_error(
            "GUI.Forms cannot inspect a disposed control property");
    }
    if (surface_root_) {
        (*surface_root_).require_root_thread("control property origin inspection");
    }
    const std::string canonical = registered_name(name);
    const BindablePropertyMap::const_iterator found =
        bindable_properties_.find(canonical);
    if (found == bindable_properties_.end()) {
        throw std::invalid_argument("GUI.Forms property is not registered: " +
                                    canonical);
    }
    const PropertyRegistration registration = (*found).second;
    if (registration.origin) return registration.origin();
    if (registration.descriptor.readable && registration.get &&
        registration.descriptor.default_value) {
        return registration.get() == *registration.descriptor.default_value
            ? PropertyValueOrigin::defaulted
            : PropertyValueOrigin::local;
    }
    return PropertyValueOrigin::computed;
}

ControlBindingsCollection& Control::data_bindings() {
    require_mutable();
    if (!data_bindings_) {
        data_bindings_ = std::make_unique<ControlBindingsCollection>(*this);
    }
    return *data_bindings_;
}

const ControlBindingsCollection& Control::data_bindings() const {
    require_mutable();
    if (!data_bindings_) {
        data_bindings_ = std::make_unique<ControlBindingsCollection>(
            *const_cast<Control*>(this));
    }
    return *data_bindings_;
}

void Control::set_name(std::string name) {
    require_mutable();
    if (name_ == name) return;
    name_ = std::move(name);
    invalidate(Dirty::semantics | Dirty::accessibility);
    publish_change(name_changed_, name_);
}

void Control::set_style(ControlStyles style, bool enabled) {
    require_mutable();
    const std::uint32_t requested = static_cast<std::uint32_t>(style);
    constexpr unsigned int known = (1U << 17U) - 1U;
    if ((requested & ~known) != 0U) {
        throw std::invalid_argument("GUI.Forms control style contains unknown bits");
    }
    const std::uint32_t current = static_cast<std::uint32_t>(styles_);
    const std::uint32_t updated = enabled ? current | requested : current & ~requested;
    if (updated == current) return;
    styles_ = static_cast<ControlStyles>(updated);
}

void Control::set_double_buffered(bool enabled) {
    require_mutable();
    // Keep the two historical buffering requests synchronized. They are
    // compatibility facts; coherent retained release remains unconditional.
    set_style(ControlStyles::double_buffer | ControlStyles::optimized_double_buffer,
              enabled);
}

namespace {

void validate_insets(Insets value, const char* message) {
    if (!std::isfinite(value.left) || !std::isfinite(value.top) ||
        !std::isfinite(value.right) || !std::isfinite(value.bottom) ||
        value.left < 0.0 || value.top < 0.0 || value.right < 0.0 ||
        value.bottom < 0.0) {
        throw std::invalid_argument(message);
    }
}

[[nodiscard]] Rect client_rect(Size size, Insets padding) noexcept {
    return {padding.left, padding.top,
            std::max(0.0, size.width - padding.left - padding.right),
            std::max(0.0, size.height - padding.top - padding.bottom)};
}

[[nodiscard]] bool valid_dock(DockStyle dock) noexcept {
    switch (dock) {
    case DockStyle::none:
    case DockStyle::top:
    case DockStyle::bottom:
    case DockStyle::left:
    case DockStyle::right:
    case DockStyle::fill:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_anchor(AnchorStyles anchor) noexcept {
    constexpr std::uint8_t valid =
        static_cast<std::uint8_t>(AnchorStyles::top) |
        static_cast<std::uint8_t>(AnchorStyles::bottom) |
        static_cast<std::uint8_t>(AnchorStyles::left) |
        static_cast<std::uint8_t>(AnchorStyles::right);
    return (static_cast<std::uint8_t>(anchor) & ~valid) == 0U;
}

[[nodiscard]] Rect anchored_bounds(Rect reference, Rect old_client,
                                   Rect new_client, AnchorStyles anchor) noexcept {
    const double left = reference.x - old_client.x;
    const double right = old_client.x + old_client.width -
                         (reference.x + reference.width);
    const double top = reference.y - old_client.y;
    const double bottom = old_client.y + old_client.height -
                          (reference.y + reference.height);
    const bool anchored_left = has_anchor(anchor, AnchorStyles::left);
    const bool anchored_right = has_anchor(anchor, AnchorStyles::right);
    const bool anchored_top = has_anchor(anchor, AnchorStyles::top);
    const bool anchored_bottom = has_anchor(anchor, AnchorStyles::bottom);

    Rect result = reference;
    if (anchored_left && anchored_right) {
        result.x = new_client.x + left;
        result.width = std::max(0.0, new_client.width - left - right);
    } else if (anchored_right) {
        result.x = new_client.x + new_client.width - right - reference.width;
    } else if (anchored_left) {
        result.x = new_client.x + left;
    } else {
        const double offset = reference.x + reference.width * 0.5 -
                              (old_client.x + old_client.width * 0.5);
        result.x = new_client.x + new_client.width * 0.5 + offset -
                   reference.width * 0.5;
    }

    if (anchored_top && anchored_bottom) {
        result.y = new_client.y + top;
        result.height = std::max(0.0, new_client.height - top - bottom);
    } else if (anchored_bottom) {
        result.y = new_client.y + new_client.height - bottom - reference.height;
    } else if (anchored_top) {
        result.y = new_client.y + top;
    } else {
        const double offset = reference.y + reference.height * 0.5 -
                              (old_client.y + old_client.height * 0.5);
        result.y = new_client.y + new_client.height * 0.5 + offset -
                   reference.height * 0.5;
    }
    return result;
}

} // namespace

void Control::add_child(Ptr child) {
    require_mutable();
    if (lifecycle_notification_ ||
        (surface_root_ != nullptr && (*surface_root_).in_lifecycle_notification_)) {
        throw std::logic_error("GUI.Forms cannot mutate the visual tree during lifecycle notification");
    }
    if (!child) {
        throw std::invalid_argument("GUI.Forms cannot add a null child");
    }
    if (child.get() == this) {
        throw std::logic_error("GUI.Forms control cannot parent itself");
    }
    if (!(*child).is_alive()) {
        throw std::logic_error("GUI.Forms cannot attach a disposed control");
    }
    if ((*child).surface_root_) {
        (*(*child).surface_root_).require_root_thread("visual-tree mutation");
        if ((*(*child).surface_root_).in_lifecycle_notification_) {
            throw std::logic_error("GUI.Forms cannot mutate the visual tree during lifecycle notification");
        }
    }
    for (std::shared_ptr<gui_forms::Control> ancestor = shared_from_this(); ancestor; ancestor = (*ancestor).parent()) {
        if (ancestor == child) {
            throw std::logic_error("GUI.Forms control tree cannot contain a cycle");
        }
    }
    if ((*child).parent().get() == this) {
        return;
    }
    if ((*child).surface_root_ && (*child).surface_root_ != surface_root_) {
        throw std::logic_error("GUI.Forms control belongs to another window");
    }
    if (Ptr previous_parent = (*child).parent()) {
        static_cast<void>((*previous_parent).remove_child((*child).runtime_id()));
    } else if ((*child).surface_root_) {
        throw std::logic_error("GUI.Forms cannot reparent another window root");
    }
    if (!(*child).is_alive()) {
        throw std::logic_error("GUI.Forms callback disposed control during reparenting");
    }

    (*child).parent_ = weak_from_this();
    (*child).layout_slot_.reset();
    (*child).anchor_reference_.reset();
    children_.push_back(child);
    if (surface_root_) {
        try {
            (*surface_root_).attach_subtree(child, weak_from_this());
        } catch (...) {
            children_.pop_back();
            (*child).parent_.reset();
            throw;
        }
    }
    invalidate_declared(invalidation::visual_tree);
}

Control::Ptr Control::remove_child(RuntimeId child_id) {
    require_mutable();
    if (lifecycle_notification_ ||
        (surface_root_ != nullptr && (*surface_root_).in_lifecycle_notification_)) {
        throw std::logic_error("GUI.Forms cannot mutate the visual tree during lifecycle notification");
    }
    ChildList::iterator found = children_.begin();
    while (found != children_.end() &&
           (*(*found)).runtime_id() != child_id) {
        ++found;
    }
    if (found == children_.end()) {
        return {};
    }
    Ptr removed = *found;
    if (surface_root_) {
        (*surface_root_).detach_subtree(removed);
    }
    ChildList::iterator current = children_.begin();
    while (current != children_.end() &&
           (*(*current)).runtime_id() != child_id) {
        ++current;
    }
    if (current != children_.end()) {
        children_.erase(current);
    }
    if ((*removed).parent_.lock().get() == this) {
        (*removed).parent_.reset();
    }
    (*removed).layout_slot_.reset();
    (*removed).anchor_reference_.reset();
    if (surface_root_) {
        static_cast<void>((*surface_root_).recompute_subtree_dirty((*surface_root_).root_));
    }
    invalidate_declared(invalidation::visual_tree);
    return removed;
}

bool Control::set_child_index(RuntimeId child_id, std::size_t index) {
    require_mutable();
    ChildList::iterator found = children_.begin();
    while (found != children_.end() &&
           (*(*found)).runtime_id() != child_id) {
        ++found;
    }
    if (found == children_.end()) {
        return false;
    }
    Ptr child = *found;
    children_.erase(found);
    // WinForms index zero is topmost. GUI.Forms paints from front to back, so
    // the topmost retained child is the final vector element.
    const std::size_t bounded = std::min(index, children_.size());
    children_.insert(children_.end() - static_cast<std::ptrdiff_t>(bounded),
                     std::move(child));
    invalidate_declared(invalidation::visual_tree);
    return true;
}

std::optional<std::size_t> Control::child_index(RuntimeId child_id) const noexcept {
    ChildList::const_iterator found = children_.begin();
    while (found != children_.end() &&
           (!*found || (*(*found)).runtime_id() != child_id)) {
        ++found;
    }
    if (found == children_.end()) return std::nullopt;
    const std::size_t painter_index = static_cast<std::size_t>(
        std::distance(children_.begin(), found));
    return children_.size() - 1U - painter_index;
}

void Control::clear_children() {
    require_mutable();
    if (lifecycle_notification_ ||
        (surface_root_ != nullptr && (*surface_root_).in_lifecycle_notification_)) {
        throw std::logic_error("GUI.Forms cannot mutate the visual tree during lifecycle notification");
    }
    while (!children_.empty()) {
        static_cast<void>(remove_child((*children_.back()).runtime_id()));
    }
}

void Control::set_requested_bounds(Rect bounds) {
    require_mutable();
    if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
        !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
        bounds.width < 0.0 || bounds.height < 0.0) {
        throw std::invalid_argument(
            "GUI.Forms control bounds must be finite with nonnegative size");
    }
    bounds.width = std::max(bounds.width, minimum_size_.width);
    bounds.height = std::max(bounds.height, minimum_size_.height);
    if (maximum_size_.width > 0.0) {
        bounds.width = std::min(bounds.width, maximum_size_.width);
    }
    if (maximum_size_.height > 0.0) {
        bounds.height = std::min(bounds.height, maximum_size_.height);
    }
    if (requested_bounds_ == bounds) {
        return;
    }
    requested_bounds_ = bounds;
    anchor_reference_.reset();
    invalidate_declared(invalidation::bounds);
}

void Control::set_bounds(Rect values, BoundsSpecified specified) {
    require_mutable();
    if (!valid_bounds_specified(specified)) {
        throw std::invalid_argument(
            "GUI.Forms BoundsSpecified contains unknown flags");
    }
    Rect next = requested_bounds_;
    if (has_bounds_specified(specified, BoundsSpecified::x)) next.x = values.x;
    if (has_bounds_specified(specified, BoundsSpecified::y)) next.y = values.y;
    if (has_bounds_specified(specified, BoundsSpecified::width)) {
        next.width = values.width;
    }
    if (has_bounds_specified(specified, BoundsSpecified::height)) {
        next.height = values.height;
    }
    set_requested_bounds(next);
}

void Control::set_minimum_size(Size size) {
    require_mutable();
    if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
        size.width < 0.0 || size.height < 0.0 ||
        (maximum_size_.width > 0.0 && size.width > maximum_size_.width) ||
        (maximum_size_.height > 0.0 && size.height > maximum_size_.height)) {
        throw std::invalid_argument("GUI.Forms minimum size is invalid");
    }
    if (minimum_size_ == size) return;
    minimum_size_ = size;
    Rect next = requested_bounds_;
    next.width = std::max(next.width, size.width);
    next.height = std::max(next.height, size.height);
    if (next != requested_bounds_) {
        set_requested_bounds(next);
    } else {
        invalidate(invalidation::bounds);
    }
}

void Control::set_maximum_size(Size size) {
    require_mutable();
    if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
        size.width < 0.0 || size.height < 0.0 ||
        (size.width > 0.0 && size.width < minimum_size_.width) ||
        (size.height > 0.0 && size.height < minimum_size_.height)) {
        throw std::invalid_argument("GUI.Forms maximum size is invalid");
    }
    if (maximum_size_ == size) return;
    maximum_size_ = size;
    Rect next = requested_bounds_;
    if (size.width > 0.0) next.width = std::min(next.width, size.width);
    if (size.height > 0.0) next.height = std::min(next.height, size.height);
    if (next != requested_bounds_) {
        set_requested_bounds(next);
    } else {
        invalidate(invalidation::bounds);
    }
}

void Control::set_child_layout(const Ptr& child, Rect bounds) {
    require_mutable();
    if (!child || !(*child).is_alive() || (*child).parent().get() != this) {
        throw std::invalid_argument(
            "GUI.Forms child layout requires a live direct child");
    }
    if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
        !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
        bounds.width < 0.0 || bounds.height < 0.0) {
        throw std::invalid_argument(
            "GUI.Forms child layout bounds must be finite and nonnegative");
    }
    if ((*child).layout_slot_ == bounds) return;
    (*child).layout_slot_ = bounds;
    if (surface_root_ != nullptr) {
        (*surface_root_).mark_child_layout_slot(*child);
    } else {
        constexpr Dirty effects = Dirty::arrange | Dirty::hit_test |
                                  Dirty::semantics | Dirty::accessibility;
        (*child).dirty_ |= effects;
        (*child).subtree_dirty_ |= effects;
        for (Ptr ancestor = (*child).parent(); ancestor;
             ancestor = (*ancestor).parent()) {
            (*ancestor).subtree_dirty_ |= effects;
        }
    }
}

void Control::note_containing_block(const Control& child, const Control& ancestor) {
    if (surface_root_ != nullptr) (*surface_root_).note_containing_block(child, ancestor);
}

void Control::set_containing_block_dependencies(std::span<const RuntimeId> watched) {
    if (surface_root_ != nullptr) (*surface_root_).set_containing_block_dependencies(*this, watched);
}

void Control::notify_positioning_changed(const Control& child) {
    if (surface_root_ == nullptr) return;
    // The child places its own absolute children against a new box, and
    // the containers that resolved through it re-resolve. The window marks
    // below the child, which it needs mutable; the child is found among
    // this control's own children (a positioning change is rare, so the
    // search is not per pass).
    for (const Ptr& candidate : children_) {
        if (candidate.get() != &child) continue;
        (*candidate).invalidate(Dirty::arrange);
        (*surface_root_).mark_containing_block_dependents(*candidate);
        return;
    }
}

void Control::arrange_self(Rect final_bounds) noexcept {
    final_bounds.width = std::max(final_bounds.width, minimum_size_.width);
    final_bounds.height = std::max(final_bounds.height, minimum_size_.height);
    if (maximum_size_.width > 0.0) {
        final_bounds.width = std::min(final_bounds.width, maximum_size_.width);
    }
    if (maximum_size_.height > 0.0) {
        final_bounds.height = std::min(final_bounds.height, maximum_size_.height);
    }
    arranged_bounds_ = final_bounds;
}

void Control::set_margin(Insets margin) {
    require_mutable();
    validate_insets(margin, "GUI.Forms margin must be finite and nonnegative");
    if (margin_ == margin) return;
    margin_ = margin;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::hit_test |
               Dirty::semantics | Dirty::accessibility);
}

void Control::set_padding(Insets padding) {
    require_mutable();
    validate_insets(padding, "GUI.Forms padding must be finite and nonnegative");
    if (padding_ == padding) return;
    padding_ = padding;
    invalidate(invalidation::bounds);
}

void Control::set_auto_scroll_offset(Point offset) {
    require_mutable();
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y)) {
        throw std::invalid_argument(
            "GUI.Forms AutoScrollOffset must be finite");
    }
    if (auto_scroll_offset_ == offset) return;
    auto_scroll_offset_ = offset;
    invalidate(Dirty::semantics | Dirty::accessibility);
}

void Control::set_dock(DockStyle dock) {
    require_mutable();
    if (!valid_dock(dock)) {
        throw std::invalid_argument("GUI.Forms DockStyle value is invalid");
    }
    if (dock_ == dock) return;
    dock_ = dock;
    anchor_reference_.reset();
    if (dock_ == DockStyle::none) {
        if (const Ptr owner = parent(); owner && (*owner).surface_root_ != nullptr &&
            ((*owner).arranged_bounds_.width > 0.0 ||
             (*owner).arranged_bounds_.height > 0.0) &&
            (arranged_bounds_.width > 0.0 || arranged_bounds_.height > 0.0)) {
            anchor_reference_ = AnchorReference{
                arranged_bounds_,
                client_rect({(*owner).arranged_bounds_.width,
                             (*owner).arranged_bounds_.height}, (*owner).padding_)};
        }
    }
    invalidate(invalidation::bounds);
}

void Control::set_anchor(AnchorStyles anchor) {
    require_mutable();
    if (!valid_anchor(anchor)) {
        throw std::invalid_argument("GUI.Forms AnchorStyles contains unknown flags");
    }
    if (anchor_ == anchor) return;
    anchor_ = anchor;
    anchor_reference_.reset();
    if (dock_ == DockStyle::none) {
        if (const Ptr owner = parent(); owner && (*owner).surface_root_ != nullptr &&
            ((*owner).arranged_bounds_.width > 0.0 ||
             (*owner).arranged_bounds_.height > 0.0) &&
            (arranged_bounds_.width > 0.0 || arranged_bounds_.height > 0.0)) {
            anchor_reference_ = AnchorReference{
                arranged_bounds_,
                client_rect({(*owner).arranged_bounds_.width,
                             (*owner).arranged_bounds_.height}, (*owner).padding_)};
        }
    }
    invalidate(invalidation::bounds);
}

void Control::set_auto_size(bool auto_size) {
    require_mutable();
    if (auto_size_ == auto_size) return;
    auto_size_ = auto_size;
    anchor_reference_.reset();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::hit_test |
               Dirty::semantics | Dirty::accessibility);
    publish_change(auto_size_changed_, auto_size_);
}

void Control::set_auto_size_mode(AutoSizeMode mode) {
    require_mutable();
    if (mode != AutoSizeMode::grow_and_shrink &&
        mode != AutoSizeMode::grow_only) {
        throw std::invalid_argument("GUI.Forms AutoSizeMode value is invalid");
    }
    if (auto_size_mode_ == mode) return;
    auto_size_mode_ = mode;
    if (auto_size_) {
        anchor_reference_.reset();
        invalidate(Dirty::measure | Dirty::arrange | Dirty::hit_test |
                   Dirty::semantics | Dirty::accessibility);
    }
}

Size Control::get_preferred_size(Size proposed) {
    return measure(unconstrained_if_zero(proposed));
}

Rect Control::arranged_bounds() const {
    if (surface_root_) {
        (*surface_root_).ensure_layout(true);
    }
    return arranged_bounds_;
}

void Control::suspend_layout() {
    require_mutable();
    if (layout_suspend_depth_ == std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("GUI.Forms layout suspension depth overflow");
    }
    // The subtree's layout flags are read; touches below settle first.
    if (surface_root_ != nullptr) (*surface_root_).settle_dirty();
    if (layout_suspend_depth_ == 0U &&
        has_dirty(subtree_dirty_, Dirty::layout)) {
        layout_deferred_ = true;
        ++layout_requested_revision_;
        if (layout_requested_revision_ == 0U) ++layout_requested_revision_;
    }
    ++layout_suspend_depth_;
}

void Control::resume_layout(bool perform_pending_layout) {
    require_mutable();
    // WinForms tolerates an unmatched ResumeLayout. Keeping that behavior is
    // important for generated/designer code which may unwind conditionally.
    if (layout_suspend_depth_ == 0U) return;
    // The pending touches note their requests while this control is still
    // suspended, as they did at their writes.
    if (surface_root_ != nullptr) (*surface_root_).settle_dirty();
    --layout_suspend_depth_;
    if (layout_suspend_depth_ != 0U || !perform_pending_layout ||
        !layout_deferred_) {
        return;
    }
    if (surface_root_ != nullptr) {
        (*surface_root_).ensure_layout(false);
    }
}

void Control::perform_layout() {
    require_mutable();
    // A suspended request is revisioned by the ordinary invalidation path so
    // every suspended ancestor observes the same mutation exactly once.
    if (layout_suspend_depth_ == 0U) {
        ++layout_requested_revision_;
        if (layout_requested_revision_ == 0U) ++layout_requested_revision_;
    }
    layout_deferred_ = true;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::hit_test |
               Dirty::semantics | Dirty::accessibility);
    if (surface_root_ != nullptr && layout_suspend_depth_ == 0U) {
        (*surface_root_).ensure_layout(false);
    }
}

LayoutTransactionState Control::layout_transaction_state() const {
    if (surface_root_ != nullptr) (*surface_root_).settle_dirty();
    return {layout_suspend_depth_, layout_deferred_, layout_requested_revision_,
            layout_committed_revision_};
}

Dirty Control::dirty() const {
    if (surface_root_ != nullptr) (*surface_root_).settle_dirty();
    return dirty_;
}

std::uint64_t Control::measure_generation() const {
    if (surface_root_ != nullptr) (*surface_root_).settle_dirty();
    return measure_generation_;
}

Dirty Control::subtree_dirty() const {
    if (surface_root_ != nullptr) (*surface_root_).settle_dirty();
    return subtree_dirty_;
}

Rect Control::absolute_bounds() const {
    if (surface_root_) {
        (*surface_root_).ensure_layout(true);
        return (*surface_root_).absolute_bounds_of(*this);
    }
    Rect result = arranged_bounds_;
    for (Ptr ancestor = parent(); ancestor; ancestor = (*ancestor).parent()) {
        result.x += (*ancestor).arranged_bounds_.x;
        result.y += (*ancestor).arranged_bounds_.y;
    }
    return result;
}

Point Control::point_to_window(Point local) const {
    const Rect absolute = absolute_bounds();
    return {absolute.x + local.x, absolute.y + local.y};
}

Point Control::point_from_window(Point window_point) const {
    const Rect absolute = absolute_bounds();
    return {window_point.x - absolute.x, window_point.y - absolute.y};
}

Rect Control::rectangle_to_window(Rect local) const {
    const Point origin = point_to_window({local.x, local.y});
    return {origin.x, origin.y, local.width, local.height};
}

Rect Control::rectangle_from_window(Rect window_rectangle) const {
    const Point origin = point_from_window(
        {window_rectangle.x, window_rectangle.y});
    return {origin.x, origin.y, window_rectangle.width,
            window_rectangle.height};
}

bool Control::contains(const Control& candidate) const noexcept {
    for (Ptr current = candidate.parent(); current; current = (*current).parent()) {
        if (current.get() == this) return true;
    }
    return false;
}

Control::Ptr Control::get_child_at_point(
    Point client_point, GetChildAtPointSkip skip) const {
    if (!std::isfinite(client_point.x) || !std::isfinite(client_point.y)) {
        throw std::invalid_argument(
            "GUI.Forms child lookup point must be finite");
    }
    if (!valid_child_skip(skip)) {
        throw std::invalid_argument(
            "GUI.Forms GetChildAtPointSkip contains unknown flags");
    }
    if (surface_root_ != nullptr) (*surface_root_).ensure_layout(true);
    if (!child_viewport_rectangle().contains(client_point)) return {};
    for (ChildList::const_reverse_iterator current = children_.rbegin();
         current != children_.rend();
         ++current) {
        const Ptr& child = *current;
        if (!child || !(*child).is_alive()) continue;
        if (has_child_skip(skip, GetChildAtPointSkip::invisible) &&
            !(*child).visible_) continue;
        if (has_child_skip(skip, GetChildAtPointSkip::disabled) &&
            !(*child).enabled_) continue;
        if (has_child_skip(skip, GetChildAtPointSkip::transparent) &&
            (*child).hit_test_transparent_) continue;
        if ((*child).arranged_bounds_.contains(client_point)) return child;
    }
    return {};
}

Control::Ptr Control::get_next_control(const Ptr& control,
                                       bool forward) const {
    std::vector<Ptr> ordered;
    collect_tab_order_controls(*this, ordered);
    if (ordered.empty()) return {};
    if (!control || control.get() == this) {
        return forward ? ordered.front() : ordered.back();
    }
    const ChildList::iterator found =
        std::find(ordered.begin(), ordered.end(), control);
    if (found == ordered.end()) return {};
    if (forward) {
        const ChildList::iterator next = std::next(found);
        return next == ordered.end() ? Ptr{} : *next;
    }
    return found == ordered.begin() ? Ptr{} : *std::prev(found);
}

bool Control::tab_order_less(const Ptr& left, const Ptr& right) noexcept {
    if (!left) return false;
    if (!right) return true;
    return (*left).tab_index_ < (*right).tab_index_;
}

void Control::collect_tab_order_controls(const Control& owner,
                                         std::vector<Ptr>& ordered) {
    std::vector<Ptr> children(owner.children_.begin(), owner.children_.end());
    std::stable_sort(children.begin(), children.end(), &Control::tab_order_less);
    for (const Ptr& child : children) {
        if (!child || !(*child).is_alive() ||
            (*child).parent().get() != &owner) {
            continue;
        }
        ordered.push_back(child);
        collect_tab_order_controls(*child, ordered);
    }
}

void Control::bring_to_front() {
    require_mutable();
    if (Ptr owner = parent()) {
        static_cast<void>((*owner).set_child_index(runtime_id_, 0U));
    }
}

void Control::send_to_back() {
    require_mutable();
    if (Ptr owner = parent()) {
        const std::size_t index = (*owner).children_.empty()
            ? 0U : (*owner).children_.size() - 1U;
        static_cast<void>((*owner).set_child_index(runtime_id_, index));
    }
}

void Control::set_visible(bool visible) {
    require_mutable();
    if (visible_ == visible) {
        return;
    }
    visible_ = visible;
    if (surface_root_ && !visible) {
        (*surface_root_).on_eligibility_changed(shared_from_this());
    }
    if (!is_alive()) {
        return;
    }
    invalidate_subtree(invalidation::visibility);
    if (surface_root_) (*surface_root_).publish_control_availability(*this);
    if (!is_alive()) return;
    publish_change(visible_changed_, visible_);
}

void Control::set_enabled(bool enabled) {
    require_mutable();
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    if (surface_root_ && !enabled) {
        (*surface_root_).on_eligibility_changed(shared_from_this());
    }
    if (!is_alive()) {
        return;
    }
    invalidate_declared(invalidation::enabled);
    if (surface_root_) (*surface_root_).publish_control_availability(*this);
    if (!is_alive()) return;
    publish_change(enabled_changed_, enabled_);
}

void Control::set_focusable(bool focusable) {
    require_mutable();
    if (focusable_ == focusable) {
        return;
    }
    focusable_ = focusable;
    if (surface_root_ && !focusable) {
        (*surface_root_).on_eligibility_changed(shared_from_this());
    }
    if (!is_alive()) {
        return;
    }
    invalidate_declared(invalidation::focusability);
}

void Control::set_causes_validation(bool causes_validation) {
    require_mutable();
    if (causes_validation_ == causes_validation) return;
    causes_validation_ = causes_validation;
    publish_change(causes_validation_changed_, causes_validation_);
}

bool Control::perform_validation(Control* destination, bool bulk) {
    require_mutable();
    if (!causes_validation_) return true;
    ControlValidationEvent event{this, destination, bulk, false};
    validating_.emit(event);
    if (!is_alive() || event.cancel) return !event.cancel;
    validated_.emit();
    return true;
}

void Control::set_tab_index(std::uint32_t index) {
    require_mutable();
    if (tab_index_ == index) return;
    tab_index_ = index;
    invalidate(Dirty::semantics | Dirty::accessibility);
}

void Control::set_tab_stop(bool enabled) {
    require_mutable();
    if (tab_stop_ == enabled) return;
    tab_stop_ = enabled;
    invalidate(Dirty::semantics | Dirty::accessibility);
}

void Control::set_allow_drop(bool allow_drop) {
    require_mutable();
    if (allow_drop_ == allow_drop) {
        return;
    }
    allow_drop_ = allow_drop;
    if (surface_root_ && !allow_drop) {
        (*surface_root_).on_eligibility_changed(shared_from_this());
    }
    if (!is_alive()) {
        return;
    }
    invalidate(Dirty::semantics | Dirty::hit_test);
}

void Control::set_hit_test_transparent(bool transparent) {
    require_mutable();
    if (hit_test_transparent_ == transparent) return;
    hit_test_transparent_ = transparent;
    if (surface_root_ && transparent) {
        (*surface_root_).on_hit_test_transparency_changed(shared_from_this());
    }
    if (!is_alive()) return;
    invalidate(Dirty::hit_test | Dirty::semantics | Dirty::accessibility);
}

void Control::set_cursor(std::optional<CursorKind> cursor) {
    require_mutable();
    if (cursor_ == cursor && !cursor_images_) {
        return;
    }
    cursor_images_.reset();
    cursor_ = cursor;
    invalidate(Dirty::semantics);
}

void Control::set_classes(std::vector<std::string> classes) {
    require_mutable();
    std::vector<std::string> unique;
    unique.reserve(classes.size());
    for (std::string& name : classes) {
        if (!valid_style_class_name(name)) {
            throw std::invalid_argument("GUI.Forms style class names are lowercase letters, digits, '-' and '_'");
        }
        const bool repeated = std::find(unique.begin(), unique.end(), name) != unique.end();
        if (!repeated) unique.push_back(std::move(name));
    }
    if (unique == classes_) return;
    classes_ = std::move(unique);
    invalidate(Dirty::style | Dirty::paint | refresh_text_font());
}

void Control::set_ceremonial(bool ceremonial) {
    require_mutable();
    if (ceremonial_ == ceremonial) return;
    ceremonial_ = ceremonial;
    invalidate_subtree(Dirty::paint);
}

bool Control::on_ceremonial_surface() const noexcept {
    for (const Control* current = this; current != nullptr;) {
        if ((*current).ceremonial_) return true;
        const std::shared_ptr<Control> parent = (*current).parent_.lock();
        current = parent.get();
    }
    return false;
}

bool Control::add_class(std::string_view name) {
    require_mutable();
    if (!valid_style_class_name(name)) {
        throw std::invalid_argument("GUI.Forms style class names are lowercase letters, digits, '-' and '_'");
    }
    if (has_class(name)) return false;
    classes_.emplace_back(name);
    invalidate(Dirty::style | Dirty::paint | refresh_text_font());
    return true;
}

bool Control::remove_class(std::string_view name) {
    require_mutable();
    const std::vector<std::string>::iterator found = std::find(classes_.begin(), classes_.end(), name);
    if (found == classes_.end()) return false;
    classes_.erase(found);
    invalidate(Dirty::style | Dirty::paint | refresh_text_font());
    return true;
}

bool Control::has_class(std::string_view name) const noexcept {
    for (const std::string& present : classes_) {
        if (present == name) return true;
    }
    return false;
}

InlineStyleResult Control::set_inline_style(std::string_view text) {
    require_mutable();
    if (text.empty()) {
        if (inline_style_ == nullptr) return InlineStyleResult::applied({});
        inline_style_.reset();
        invalidate(Dirty::style | Dirty::paint);
        return InlineStyleResult::applied({});
    }
    const std::string file = "inline:" + std::string(stable_id().value());
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    std::shared_ptr<const detail::ControlInlineStyle> compiled =
        detail::compile_inline_style(text, anatomy(), stable_id().value(), file, effective_theme(), diagnostics);
    // A refused style changes nothing (B2).
    if (compiled == nullptr) return InlineStyleResult::refused(std::move(diagnostics));
    inline_style_ = std::move(compiled);
    invalidate(Dirty::style | Dirty::paint);
    return InlineStyleResult::applied(std::move(diagnostics));
}

std::string_view Control::inline_style() const noexcept {
    if (inline_style_ == nullptr) return {};
    return (*inline_style_).text;
}

const CompiledThemeRules* Control::inline_style_rules() const noexcept {
    if (inline_style_ == nullptr) return nullptr;
    return &(*inline_style_).rules;
}

void Control::set_custom_cursor(CursorImagesPtr images, CursorKind fallback) {
    require_mutable();
    if (cursor_images_ == images && cursor_ == fallback) return;
    cursor_images_ = std::move(images);
    cursor_ = fallback;
    invalidate(Dirty::semantics);
}

CursorImagesPtr Control::effective_cursor_images() const noexcept {
    const Control* current = this;
    Ptr owner;
    while (current != nullptr) {
        if ((*current).cursor_.has_value()) return (*current).cursor_images_;
        owner = (*current).parent_.lock();
        current = owner.get();
    }
    return {};
}

void Control::set_accessible_name(std::string name) {
    require_mutable();
    if (accessible_name_ == name) return;
    accessible_name_ = std::move(name);
    invalidate(Dirty::semantics);
}

void Control::set_accessible_description(std::string description) {
    require_mutable();
    if (accessible_description_ == description) return;
    accessible_description_ = std::move(description);
    invalidate(Dirty::semantics);
}

void Control::set_tool_tip_text(std::string text) {
    require_mutable();
    if (!validate_utf8(text).valid()) {
        throw std::invalid_argument("GUI.Forms tooltip text must be valid UTF-8");
    }
    if (tool_tip_text_ == text) return;
    tool_tip_text_ = std::move(text);
    // A field hint is painted by the field itself: this control, or the
    // field_hint editors it encloses, which paint its text when they have
    // none of their own.
    const bool hinted = tool_tip_presentation_ == ToolTipPresentation::field_hint;
    const Dirty effects = hinted ? Dirty::semantics | Dirty::paint : Dirty::semantics;
    invalidate(effects);
    if (hinted) {
        for (const Ptr& child : children_) {
            if ((*child).tool_tip_presentation_ == ToolTipPresentation::field_hint) (*child).invalidate(Dirty::paint);
        }
    }
    publish_change(tool_tip_text_changed_, tool_tip_text_);
    if (surface_root_ != nullptr) (*surface_root_).tool_tip_text_changed(*this);
}

std::string Control::field_hint_text() const {
    const Control* current = this;
    Ptr owner;
    while (current != nullptr && (*current).tool_tip_presentation_ == ToolTipPresentation::field_hint) {
        if (!(*current).tool_tip_text_.empty()) return (*current).tool_tip_text_;
        owner = (*current).parent_.lock();
        current = owner.get();
    }
    return {};
}

CursorKind Control::effective_cursor() const noexcept {
    const Control* current = this;
    Ptr owner;
    while (current != nullptr) {
        if ((*current).cursor_.has_value()) {
            return *(*current).cursor_;
        }
        owner = (*current).parent_.lock();
        current = owner.get();
    }
    return CursorKind::arrow;
}

bool Control::effectively_visible() const noexcept {
    if (!visible_ || layout_collapsed_ || !is_alive()) {
        return false;
    }
    for (Ptr ancestor = parent(); ancestor; ancestor = (*ancestor).parent()) {
        if (!(*ancestor).visible_ || (*ancestor).layout_collapsed_ ||
            !(*ancestor).is_alive()) {
            return false;
        }
    }
    return true;
}

bool Control::effectively_enabled() const noexcept {
    if (!enabled_ || !is_alive()) {
        return false;
    }
    for (Ptr ancestor = parent(); ancestor; ancestor = (*ancestor).parent()) {
        if (!(*ancestor).enabled_ || !(*ancestor).is_alive()) {
            return false;
        }
    }
    return true;
}

bool Control::eligible_for_input() const noexcept {
    return surface_root_ != nullptr && !initialization_blocked() &&
           effectively_visible() && effectively_enabled();
}

void Control::set_pointer_capture(bool captured) {
    require_mutable();
    if (surface_root_ == nullptr) {
        if (!captured) return;
        throw std::logic_error(
            "GUI.Forms pointer capture requires an attached control");
    }
    if (captured) {
        (*surface_root_).capture_pointer(shared_from_this());
    } else if ((*surface_root_).captured_control().get() == this) {
        (*surface_root_).release_pointer();
    }
}

bool Control::has_pointer_capture() const noexcept {
    return surface_root_ != nullptr && (*surface_root_).captured_control().get() == this;
}

void Control::set_paint_plane(PaintPlane plane) {
    require_mutable();
    if (!is_valid_paint_plane(plane)) {
        throw std::invalid_argument("invalid paint plane");
    }
    if (paint_plane_ == plane) {
        return;
    }
    if (surface_root_) {
        (*surface_root_).change_paint_plane(*this, plane);
        return;
    }
    paint_plane_ = plane;
    display_chunk_.reset();
    invalidate(invalidation::paint_only);
}

std::optional<DisplayChunkInfo> Control::display_chunk_info() const noexcept {
    return display_chunk_ ? std::optional((*display_chunk_).info()) : std::nullopt;
}

void Control::invalidate(Dirty requested_dirty) {
    require_mutable();
    if (requested_dirty == Dirty::none) {
        return;
    }
    if (has_dirty(requested_dirty, Dirty::measure)) {
        requested_dirty |= Dirty::arrange;
    }
    if (initialization_depth_ != 0) {
        pending_initialization_dirty_ |= requested_dirty;
        return;
    }
    if (surface_root_) {
        (*surface_root_).mark_dirty(*this, requested_dirty);
        return;
    }
    add_dirty(requested_dirty);
    // A detached tree marks no ancestor's own flags, but an ancestor's cached
    // measure may read this control, so its generation still advances.
    const bool measure_requested = has_dirty(requested_dirty, Dirty::measure);
    for (Control* current = this; current != nullptr;) {
        (*current).subtree_dirty_ |= requested_dirty;
        if (measure_requested && current != this) {
            (*current).advance_measure_generation();
        }
        if (has_dirty(requested_dirty, Dirty::layout) &&
            (*current).layout_suspend_depth_ != 0U) {
            (*current).layout_deferred_ = true;
            ++(*current).layout_requested_revision_;
            if ((*current).layout_requested_revision_ == 0U) {
                ++(*current).layout_requested_revision_;
            }
        }
        const std::shared_ptr<gui_forms::Control> visual_parent = (*current).parent_.lock();
        current = visual_parent.get();
    }
}

void Control::invalidate(Rect local_damage) {
    require_mutable();
    if (!local_damage.finite()) {
        throw std::invalid_argument("local paint damage must be finite");
    }
    local_damage = Rect::intersection(local_damage, client_rectangle());
    if (local_damage.empty()) return;
    if (initialization_depth_ != 0) {
        pending_initialization_dirty_ |= Dirty::paint;
        return;
    }
    if (surface_root_) {
        (*surface_root_).mark_paint_dirty(*this, local_damage);
        return;
    }
    dirty_ |= Dirty::paint;
    for (Control* current = this; current != nullptr;) {
        (*current).subtree_dirty_ |= Dirty::paint;
        const std::shared_ptr<gui_forms::Control> visual_parent = (*current).parent_.lock();
        current = visual_parent.get();
    }
}

void Control::invalidate_subtree(Dirty requested_dirty) {
    require_mutable();
    if (requested_dirty == Dirty::none) {
        return;
    }
    if (has_dirty(requested_dirty, Dirty::measure)) {
        requested_dirty |= Dirty::arrange;
    }
    if (initialization_depth_ != 0) {
        pending_initialization_dirty_ |= requested_dirty;
        pending_initialization_subtree_ = true;
        return;
    }
    if (surface_root_) {
        (*surface_root_).mark_subtree_dirty(*this, requested_dirty);
        return;
    }
    apply_subtree_dirty(*this, requested_dirty);
    for (Ptr visual_parent = parent(); visual_parent;
         visual_parent = (*visual_parent).parent()) {
        (*visual_parent).subtree_dirty_ |= requested_dirty;
        if (has_dirty(requested_dirty, Dirty::measure)) {
            (*visual_parent).advance_measure_generation();
        }
        if (has_dirty(requested_dirty, Dirty::layout) &&
            (*visual_parent).layout_suspend_depth_ != 0U) {
            (*visual_parent).layout_deferred_ = true;
            ++(*visual_parent).layout_requested_revision_;
            if ((*visual_parent).layout_requested_revision_ == 0U) {
                ++(*visual_parent).layout_requested_revision_;
            }
        }
    }
}

void Control::apply_subtree_dirty(Control& control, Dirty requested_dirty) {
    control.add_dirty(requested_dirty);
    control.subtree_dirty_ |= requested_dirty;
    if (has_dirty(requested_dirty, Dirty::layout) &&
        control.layout_suspend_depth_ != 0U) {
        control.layout_deferred_ = true;
        ++control.layout_requested_revision_;
        if (control.layout_requested_revision_ == 0U) {
            ++control.layout_requested_revision_;
        }
    }
    for (const std::shared_ptr<gui_forms::Control>& child :
         control.children_) {
        apply_subtree_dirty(*child, requested_dirty);
    }
}

void Control::invalidate_declared(Dirty declared_effects) {
    require_mutable();
    if (declared_effects != Dirty::none) {
        invalidate(declared_effects);
        return;
    }
    if (surface_root_) {
        (*surface_root_).metrics_.record_undeclared_mutation();
    }
#ifndef NDEBUG
    throw std::logic_error("GUI.Forms mutation has no declared invalidation effects");
#else
    invalidate_subtree(invalidation::conservative_subtree);
#endif
}

void Control::begin_init() {
    require_mutable();
    ++initialization_depth_;
    if (initialization_depth_ == 1U && surface_root_ != nullptr) {
        (*surface_root_).on_eligibility_changed(shared_from_this());
    }
}

void Control::publish_change(const void* event_key,
                             std::function<void()> publication) {
    if (!publication) return;
    if (initialization_depth_ == 0) {
        publication();
        return;
    }
    DeferredInitializationChangeList::iterator pending =
        pending_initialization_changes_.begin();
    while (pending != pending_initialization_changes_.end() &&
           (*pending).event_key != event_key) {
        ++pending;
    }
    if (pending != pending_initialization_changes_.end()) {
        (*pending).publication = std::move(publication);
        return;
    }
    pending_initialization_changes_.push_back(
        DeferredInitializationChange{event_key, std::move(publication)});
}

void Control::end_init() {
    require_mutable();
    if (initialization_depth_ == 0) {
        throw std::logic_error("GUI.Forms EndInit has no matching BeginInit");
    }
    --initialization_depth_;
    if (initialization_depth_ != 0) {
        return;
    }

    const Dirty pending = std::exchange(pending_initialization_dirty_, Dirty::none);
    const bool subtree = std::exchange(pending_initialization_subtree_, false);
    if (pending != Dirty::none) {
        if (subtree) {
            invalidate_subtree(pending);
        } else {
            invalidate(pending);
        }
    }
    std::vector<gui_forms::Control::DeferredInitializationChange> changes = std::exchange(pending_initialization_changes_, {});
    for (std::size_t index = 0; index < changes.size(); ++index) {
        if (!is_alive()) return;
        changes[index].publication();
        // A callback may deliberately begin a new initialization transaction.
        // Do not deliver the previous transaction's remaining observers into
        // that newly partial state; retain them in original order instead.
        if (initialization_depth_ != 0) {
            for (++index; index < changes.size(); ++index) {
                publish_change(changes[index].event_key,
                               std::move(changes[index].publication));
            }
            return;
        }
    }
    if (!is_alive()) return;
    initialization_completed_.emit(pending, subtree);
}

Size Control::measure(Size available) {
    available = {std::max(0.0, available.width),
                 std::max(0.0, available.height)};
    Size desired{requested_bounds_.width, requested_bounds_.height};
    if (auto_size_) {
        double content_width = padding_.left + padding_.right;
        double content_height = padding_.top + padding_.bottom;
        const std::vector<Ptr> retained = snapshot_layout_children();
        for (const Ptr& child : retained) {
            if (!is_current_layout_child(child) || !(*child).visible_) continue;
            const Size child_desired = (*child).get_preferred_size(available);
            if (!is_alive()) return {};
            if (!is_current_layout_child(child) || !(*child).visible_) continue;
            content_width = std::max(
                content_width,
                std::max(0.0, (*child).requested_bounds_.x) +
                    child_desired.width +
                    (*child).margin_.right + padding_.right);
            content_height = std::max(
                content_height,
                std::max(0.0, (*child).requested_bounds_.y) +
                    child_desired.height +
                    (*child).margin_.bottom + padding_.bottom);
        }
        if (auto_size_mode_ == AutoSizeMode::grow_only) {
            content_width = std::max(content_width, requested_bounds_.width);
            content_height = std::max(content_height, requested_bounds_.height);
        }
        desired = {content_width, content_height};
    }
    Size result{std::min(desired.width, available.width),
                std::min(desired.height, available.height)};
    result.width = std::max(result.width, minimum_size_.width);
    result.height = std::max(result.height, minimum_size_.height);
    if (maximum_size_.width > 0.0) result.width = std::min(result.width, maximum_size_.width);
    if (maximum_size_.height > 0.0) result.height = std::min(result.height, maximum_size_.height);
    return result;
}

Size Control::measure_for_layout(const LayoutConstraint& constraint) {
    // The cache is UI-thread state, like the tree the answer reads.
    if (surface_root_ != nullptr) (*surface_root_).require_root_thread("layout measurement");
    if (!valid_layout_constraint(constraint)) {
        throw std::invalid_argument(
            "GUI.Forms layout constraint extents must be finite and nonnegative");
    }
    // A descendant's pending touch advances this control's generation at
    // the settle (mark_layout_ancestors).
    if (surface_root_ != nullptr) (*surface_root_).settle_dirty();
    const std::uint64_t generation = measure_generation_;
    for (const MeasureCacheEntry& entry : measure_cache_) {
        if (entry.generation == generation && entry.constraint == constraint) {
            if (surface_root_ != nullptr) (*surface_root_).metrics().record_measure_cache_hit();
            const Size cached = entry.result;
            return cached;
        }
    }
    if (surface_root_ != nullptr) {
        const std::uint64_t pass = (*surface_root_).layout_pass_sequence_;
        if (measure_pass_sequence_ != pass) {
            measure_pass_sequence_ = pass;
            measure_pass_callbacks_ = 0U;
        }
        ++measure_pass_callbacks_;
        (*surface_root_).metrics().record_measure_cache_miss(measure_pass_callbacks_);
    }
    Size measured = measure_intrinsic(constraint);
    bool clamped = false;
    if (!std::isfinite(measured.width) || measured.width < 0.0) {
        measured.width = 0.0;
        clamped = true;
    }
    if (!std::isfinite(measured.height) || measured.height < 0.0) {
        measured.height = 0.0;
        clamped = true;
    }
    if (clamped && surface_root_ != nullptr) (*surface_root_).metrics().record_layout_measure_clamp();
    // A callback that invalidated this control made its own answer stale.
    if (measure_generation_ != generation || !is_alive()) return measured;
    std::size_t slot = 0U;
    if (constraint.width.kind == AvailableExtentKind::min_content) {
        slot = 0U;
    } else if (constraint.width.kind == AvailableExtentKind::max_content) {
        slot = 1U;
    } else {
        slot = measure_cache_next_general_slot_;
        measure_cache_next_general_slot_ = slot == 2U ? 3U : 2U;
    }
    measure_cache_[slot] = MeasureCacheEntry{constraint, measured, generation};
    return measured;
}

std::optional<double> Control::first_baseline(double width) {
    static_cast<void>(width);
    return std::nullopt;
}

std::optional<double> Control::last_baseline(double width) {
    static_cast<void>(width);
    return std::nullopt;
}

Size Control::measure_intrinsic(const LayoutConstraint& constraint) {
    const Size available = measure_available_size(constraint);
    Size measured = measure(available);
    if (constraint.width.kind == AvailableExtentKind::definite) {
        measured.width = constraint.width.extent.value();
    }
    if (constraint.height.kind == AvailableExtentKind::definite) {
        measured.height = constraint.height.extent.value();
    }
    return measured;
}

bool Control::size_depends_on_content() const noexcept { return true; }

bool Control::sizes_children_from_content() const noexcept { return true; }

bool Control::measures_children_in_arrange() const noexcept { return false; }

void Control::set_layout_direction(LayoutDirection direction) {
    require_mutable();
    if (direction != LayoutDirection::inherit &&
        direction != LayoutDirection::left_to_right &&
        direction != LayoutDirection::right_to_left) {
        throw std::invalid_argument("GUI.Forms LayoutDirection value is invalid");
    }
    if (layout_direction_ == direction) return;
    layout_direction_ = direction;
    // Direction mirrors where children and text are placed, never their
    // measured sizes; every descendant that inherits it moves.
    constexpr Dirty effects = Dirty::arrange | Dirty::paint | Dirty::hit_test |
                              Dirty::semantics | Dirty::accessibility;
    invalidate_subtree(effects);
}

LayoutDirection Control::effective_layout_direction() const noexcept {
    LayoutDirection direction = layout_direction_;
    Ptr ancestor = parent_.lock();
    while (direction == LayoutDirection::inherit && ancestor) {
        direction = (*ancestor).layout_direction_;
        ancestor = (*ancestor).parent_.lock();
    }
    if (direction == LayoutDirection::inherit) direction = LayoutDirection::left_to_right;
    return direction;
}

void Control::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    if (children_.empty()) return;

    const std::vector<Ptr> retained = snapshot_layout_children();

    const Rect viewport = child_viewport_rectangle();
    const Rect client{
        viewport.x + padding_.left,
        viewport.y + padding_.top,
        std::max(0.0, viewport.width - padding_.left - padding_.right),
        std::max(0.0, viewport.height - padding_.top - padding_.bottom)};
    Rect remaining = client;
    // In a right-to-left parent the dock and anchor layout is computed left
    // to right and mirrored about the viewport, padding included: dock
    // left docks right, anchor left anchors right, and a requested x is
    // measured from the right edge (Windows Forms' RightToLeftLayout).
    const bool mirrored = effective_layout_direction() == LayoutDirection::right_to_left;
    // The retained vector is painter order: backmost first, topmost last.
    // Dock consumes that reverse public z-order (backmost to topmost), matching
    // Forms semantics while leaving index zero consistently topmost for public
    // child indexing, hit testing, BringToFront, and SendToBack.
    for (const Ptr& child : retained) {
        if (!is_current_layout_child(child) || !(*child).visible_ ||
            (*child).dock_ == DockStyle::none) {
            continue;
        }
        const Size desired = (*child).measure({remaining.width, remaining.height});
        if (!is_alive()) return;
        if (!is_current_layout_child(child) || !(*child).visible_ ||
            (*child).dock_ == DockStyle::none) {
            continue;
        }
        const double width = (*child).auto_size()
            ? ((*child).auto_size_mode_ == AutoSizeMode::grow_only
                   ? std::max((*child).requested_bounds_.width, desired.width)
                   : desired.width)
            : ((*child).requested_bounds_.width > 0.0
                   ? (*child).requested_bounds_.width : desired.width);
        const double height = (*child).auto_size()
            ? ((*child).auto_size_mode_ == AutoSizeMode::grow_only
                   ? std::max((*child).requested_bounds_.height, desired.height)
                   : desired.height)
            : ((*child).requested_bounds_.height > 0.0
                   ? (*child).requested_bounds_.height : desired.height);
        const Rect slot = take_dock_slot((*child).dock_, width, height, remaining);
        (*child).anchor_reference_.reset();
        set_child_layout(child, mirrored ? mirror_horizontally(slot, viewport) : slot);
    }

    for (const Ptr& child : retained) {
        if (!is_current_layout_child(child) || !(*child).visible_ ||
            (*child).dock_ != DockStyle::none) {
            continue;
        }
        Rect authored = (*child).requested_bounds_;
        if ((*child).auto_size()) {
            const Size preferred = (*child).get_preferred_size(
                {client.width, client.height});
            if (!is_alive()) return;
            if (!is_current_layout_child(child) || !(*child).visible_ ||
                (*child).dock_ != DockStyle::none) {
                continue;
            }
            if ((*child).auto_size_mode_ == AutoSizeMode::grow_only) {
                authored.width = std::max(authored.width, preferred.width);
                authored.height = std::max(authored.height, preferred.height);
            } else {
                authored.width = preferred.width;
                authored.height = preferred.height;
            }
        }
        if ((*child).auto_size()) {
            (*child).anchor_reference_ = AnchorReference{authored, client};
        } else if (!(*child).anchor_reference_) {
            (*child).anchor_reference_ = AnchorReference{
                authored, client};
        }
        const Rect anchored = anchored_bounds((*(*child).anchor_reference_).bounds,
                                              (*(*child).anchor_reference_).client, client,
                                              (*child).anchor_);
        set_child_layout(child, mirrored ? mirror_horizontally(anchored, viewport) : anchored);
    }
}

std::vector<Control::Ptr> Control::snapshot_layout_children() const {
    return children_;
}

bool Control::is_current_layout_child(const Ptr& child) const noexcept {
    return child && (*child).is_alive() && (*child).parent_.lock().get() == this &&
           (*child).surface_root_ == surface_root_;
}

void Control::set_child_layout_collapsed(const Ptr& child, bool collapsed) {
    if (!is_current_layout_child(child)) {
        throw std::invalid_argument(
            "layout collapse requires a live direct child");
    }
    if ((*child).layout_collapsed_ == collapsed) return;
    (*child).layout_collapsed_ = collapsed;
    if (collapsed && (*child).surface_root_ != nullptr) {
        (*(*child).surface_root_).on_eligibility_changed(child);
    }
    (*child).invalidate_subtree(invalidation::visibility);
    if ((*child).surface_root_ != nullptr && (*child).is_alive()) {
        (*(*child).surface_root_).publish_control_availability(*child);
    }
}

void Control::on_paint(Painter&, Rect) {}
void Control::on_paint_overlay(Painter&, Rect) {}

void Control::report_opaque_cover(const Painter& painter, const Rect local) const noexcept {
    if (surface_root_ != nullptr) (*surface_root_).note_opaque_cover(*this, painter, local);
}

void Control::paint_back_color(Painter& painter, Rect bounds) const {
    if (!back_color_) return;
    const bool high_contrast = surface_root_ != nullptr && (*surface_root_).presentation_settings().high_contrast;
    if (high_contrast || application_colours_suspended()) return;
    painter.fill_rect(bounds, *back_color_);
}

void Control::paint_authored_surface(Painter& painter, Rect bounds) const {
    if (authored_surface_material_ && !application_colours_suspended()) {
        paint_surface_material(painter, bounds, *authored_surface_material_);
    }
}

void Control::paint_owned_decorations(
    Painter& painter, Rect bounds, OwnerDecorationLayer layer) const {
    for (const OwnerDecorationRecipe& decoration : owned_decorations_) {
        if (decoration.layer != layer) continue;
        const Rect box = owner_decoration_box(decoration, bounds);
        const MaterialBorderEdges& edges = decoration.border_edges;
        if (edges.top) {
            const double y = box.y + (*edges.top).width * 0.5;
            draw_owner_decoration_edge(
                painter, {box.x, y}, {box.x + box.width, y}, *edges.top,
                box, decoration.rotation_degrees);
        }
        if (edges.right) {
            const double x = box.x + box.width - (*edges.right).width * 0.5;
            draw_owner_decoration_edge(
                painter, {x, box.y}, {x, box.y + box.height}, *edges.right,
                box, decoration.rotation_degrees);
        }
        if (edges.bottom) {
            const double y = box.y + box.height - (*edges.bottom).width * 0.5;
            draw_owner_decoration_edge(
                painter, {box.x, y}, {box.x + box.width, y}, *edges.bottom,
                box, decoration.rotation_degrees);
        }
        if (edges.left) {
            const double x = box.x + (*edges.left).width * 0.5;
            draw_owner_decoration_edge(
                painter, {x, box.y}, {x, box.y + box.height}, *edges.left,
                box, decoration.rotation_degrees);
        }
    }
}

Insets Control::effective_visual_outsets() const noexcept {
    Insets result = visual_outsets();
    if (authored_surface_material_) {
        const Insets surface =
            surface_material_visual_outsets(*authored_surface_material_);
        result.left = std::max(result.left, surface.left);
        result.top = std::max(result.top, surface.top);
        result.right = std::max(result.right, surface.right);
        result.bottom = std::max(result.bottom, surface.bottom);
    }
    const Rect owner{0.0, 0.0, arranged_bounds_.width, arranged_bounds_.height};
    for (const OwnerDecorationRecipe& decoration : owned_decorations_) {
        const Rect visual = owner_decoration_visual_bounds(decoration, owner);
        result.left = std::max(result.left, std::max(0.0, -visual.x));
        result.top = std::max(result.top, std::max(0.0, -visual.y));
        result.right = std::max(
            result.right, std::max(0.0, visual.x + visual.width - owner.width));
        result.bottom = std::max(
            result.bottom, std::max(0.0, visual.y + visual.height - owner.height));
    }
    return result;
}

Insets Control::visual_outsets() const noexcept {
    return {};
}

bool Control::on_ambient_frame(FrameTime) {
    return false;
}

bool Control::paint_depends_on_size() const noexcept {
    const bool depends = has_style(ControlStyles::resize_redraw);
    return depends;
}

bool Control::hit_test_local(Point local_point) const {
    return Rect{0.0, 0.0, arranged_bounds_.width, arranged_bounds_.height}.contains(local_point);
}

void Control::on_pointer_preview(PointerEvent&) {}
void Control::on_pointer(PointerEvent&) {}
void Control::on_pointer_bubble(PointerEvent&) {}
void Control::on_key_preview(KeyEvent&) {}
void Control::on_key(KeyEvent&) {}
void Control::on_key_bubble(KeyEvent&) {}
void Control::on_text_input(TextInputEvent&) {}
bool Control::process_mnemonic(char32_t character) {
    if (!is_alive() || initialization_blocked() || !effectively_visible() ||
        !effectively_enabled()) {
        return false;
    }

    // Materialize the arbitration set before invoking application code. A
    // mnemonic may dispose/reparent controls, so recursive callback traversal
    // would otherwise make the winner depend on mutation during dispatch.
    std::vector<Ptr> candidates;
    collect_mnemonic_candidates(shared_from_this(), character, candidates);
    for (const Ptr& candidate : candidates) {
        if (candidate && (*candidate).is_alive() &&
            (*candidate).process_mnemonic_self(character)) {
            return true;
        }
    }
    return false;
}

void Control::collect_mnemonic_candidates(const Ptr& control,
                                          char32_t character,
                                          std::vector<Ptr>& candidates) {
    if (!control || !(*control).is_alive() ||
        !(*control).effectively_visible() ||
        !(*control).effectively_enabled()) {
        return;
    }
    if ((*control).mnemonic_matches(character)) {
        candidates.push_back(control);
    }
    std::vector<Ptr> retained((*control).children_.begin(),
                              (*control).children_.end());
    std::stable_sort(retained.begin(), retained.end(),
                     &Control::tab_order_less);
    for (const Ptr& child : retained) {
        if (!child || (*child).parent().get() != control.get() ||
            (*child).surface_root_ != (*control).surface_root_) {
            continue;
        }
        collect_mnemonic_candidates(child, character, candidates);
    }
}

bool Control::prepare_command_activation() {
    if (!is_alive() || initialization_blocked() || !effectively_visible() ||
        !effectively_enabled()) {
        return false;
    }
    return surface_root_ == nullptr ||
           (*surface_root_).validate_command_activation(shared_from_this());
}

bool Control::focus_next_after_self() {
    return surface_root_ != nullptr &&
           (*surface_root_).move_focus_after(shared_from_this());
}

bool Control::perform_dialog_command() { return false; }
bool Control::supports_dialog_command() const noexcept { return false; }
DialogResult Control::command_dialog_result() const noexcept {
    return DialogResult::none;
}
void Control::assign_cancel_dialog_result() {}
bool Control::mnemonic_matches(char32_t) const noexcept { return false; }
bool Control::process_mnemonic_self(char32_t) { return false; }
void Control::notify_default(bool) {}

SemanticDescriptor Control::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.name = accessible_name_;
    descriptor.description = accessible_description_;
    return descriptor;
}

void Control::apply_provider_semantics(SemanticDescriptor& descriptor) const {
    if ((!provider_errors_.empty() || !provider_help_.empty()) &&
        !descriptor.exposed) {
        descriptor.exposed = true;
        if (descriptor.role == SemanticRole::generic) {
            descriptor.role = SemanticRole::group;
        }
    }
    for (const std::pair<const std::uint64_t, std::string>& provider_error :
         provider_errors_) {
        const std::string& error = provider_error.second;
        append_semantic_description(descriptor, "Error: ", error);
        descriptor.states |= SemanticState::invalid;
    }
    for (const std::pair<const std::uint64_t, std::string>& provider_help :
         provider_help_) {
        const std::string& help = provider_help.second;
        append_semantic_description(descriptor, {}, help);
    }
}

std::vector<SemanticNode> Control::semantic_virtual_children() const {
    return {};
}

bool Control::on_semantic_action(SemanticAction action, std::string_view) {
    if (action == SemanticAction::focus && surface_root_ != nullptr && focusable_) {
        return (*surface_root_).request_focus(shared_from_this());
    }
    return false;
}
bool Control::on_semantic_child_action(std::string_view, SemanticAction,
                                       std::string_view) {
    return false;
}
SemanticTextResult Control::on_semantic_text_layout(SemanticTextLayout&) {
    return SemanticTextResult::no_text;
}
SemanticTextResult Control::on_set_semantic_text_selection(SemanticTextSelection) {
    return SemanticTextResult::no_text;
}
void Control::on_drag_preview(DragEvent&) {}
void Control::on_drag(DragEvent&) {}
void Control::on_drag_bubble(DragEvent&) {}
void Control::on_focus_changed(bool) {}
void Control::on_focus_cue_changed(bool) {}
void Control::on_activate() {}

namespace {

constexpr std::array<PartField<Control::Parts>, 2> control_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &Control::Parts::region},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &Control::Parts::focus},
}};

constexpr std::array<StateField<Control::States>, 5> control_state_fields{{
    {.state = AnatomyState::normal, .member = &Control::States::normal},
    {.state = AnatomyState::enabled, .member = &Control::States::enabled},
    {.state = AnatomyState::disabled, .member = &Control::States::disabled},
    {.state = AnatomyState::focused, .member = &Control::States::focused},
    {.state = AnatomyState::focus_visible, .member = &Control::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 2> control_parts = part_declarations(control_part_fields);
constexpr std::array<AnatomyState, 5> control_states = state_declarations(control_state_fields);

constexpr ControlAnatomy base_anatomy{
    .control_id = "control",
    .parts = control_parts,
    .states = control_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& base_control_anatomy() noexcept {
    return base_anatomy;
}

const ControlAnatomy& Control::anatomy() const noexcept {
    return base_control_anatomy();
}

void Control::fill_part_geometry(std::span<PartGeometry> geometry) const {
    const ControlAnatomy& declared = anatomy();
    if (geometry.size() != declared.parts.size()) {
        throw std::invalid_argument(
            "part geometry storage must hold one record per declared part");
    }
    const PartGeometryBuffer published = published_part_geometry();
    const std::span<const PartGeometry> parts = published.parts();
    std::copy(parts.begin(), parts.end(), geometry.begin());
}

PartGeometryBuffer Control::published_part_geometry() const {
    PartGeometryBuffer buffer;
    const ControlAnatomy& declared = anatomy();
    buffer.count = std::min(declared.parts.size(), maximum_control_parts);
    const std::span<PartGeometry> parts(buffer.geometry.data(), buffer.count);
    publish_part_geometry(parts);
    return buffer;
}

AnatomyStateSet Control::anatomy_states() const {
    return base_anatomy_states();
}

void Control::publish_part_geometry(std::span<PartGeometry> geometry) const {
    // The region and the focus cue cover the client rectangle; any other
    // declared part stays absent until its type publishes it.
    const Rect client = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(client);
    record.focus = PartGeometry::from_bounds(client);
    write_parts(anatomy(), control_part_fields, record, geometry);
}

Control::Parts Control::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), control_part_fields, published.parts(), record);
    return record;
}

std::size_t Control::realized_item_count() const {
    return 0U;
}

std::string Control::realized_item_stable_id(std::size_t) const {
    return {};
}

ItemSystemIndex Control::realized_item_system(std::size_t item) const {
    if (item >= realized_item_count()) {
        throw std::out_of_range("realized item index is past the realized items");
    }
    const ItemSystemIndex system = publish_realized_item_system(item);
    return system;
}

ItemSystemIndex Control::publish_realized_item_system(std::size_t) const {
    const ItemSystemIndex first;
    return first;
}

void Control::fill_realized_item_geometry(std::size_t item,
                                          std::span<PartGeometry> geometry) const {
    const ItemSystemIndex system = realized_item_system(item);
    const std::span<const ItemSystemDeclaration> systems = anatomy().item_systems;
    const std::size_t declared_parts =
        system.value < systems.size() ? systems[system.value].parts.size() : 0U;
    if (geometry.size() != declared_parts) {
        throw std::invalid_argument(
            "item geometry storage must hold one record per declared item part");
    }
    const PartGeometryBuffer published = published_realized_item_geometry(item);
    const std::span<const PartGeometry> parts = published.parts();
    std::copy(parts.begin(), parts.end(), geometry.begin());
}

AnatomyStateSet Control::realized_item_anatomy_states(std::size_t) const {
    AnatomyStateSet states;
    return states;
}

void Control::publish_realized_item_geometry(std::size_t, std::span<PartGeometry>) const {}

PartGeometryBuffer Control::published_realized_item_geometry(std::size_t item) const {
    PartGeometryBuffer buffer;
    const ItemSystemIndex system = publish_realized_item_system(item);
    const std::span<const ItemSystemDeclaration> systems = anatomy().item_systems;
    const std::size_t declared_parts =
        system.value < systems.size() ? systems[system.value].parts.size() : 0U;
    buffer.count = std::min(declared_parts, maximum_control_parts);
    const std::span<PartGeometry> parts(buffer.geometry.data(), buffer.count);
    publish_realized_item_geometry(item, parts);
    return buffer;
}

Control::States Control::states() const {
    States record;
    read_states(control_state_fields, anatomy_states(), record);
    return record;
}

AnatomyStateSet Control::base_anatomy_states() const {
    AnatomyStateSet states;
    states.insert(AnatomyState::normal);
    if (effectively_enabled()) {
        states.insert(AnatomyState::enabled);
    } else {
        states.insert(AnatomyState::disabled);
    }
    if (pointer_over_) states.insert(AnatomyState::hot);
    if (surface_root_ != nullptr) {
        const Ptr focused = (*surface_root_).focused_control();
        if (focused.get() == this) {
            states.insert(AnatomyState::focused);
            if ((*surface_root_).focus_cue_visible()) {
                states.insert(AnatomyState::focus_visible);
            }
        }
    }
    return states;
}

void Control::on_attached_to_window() {}
void Control::on_attachment_committed() noexcept {}
void Control::on_detaching_from_window(SurfaceRoot&) noexcept {}
void Control::on_detached_from_window() noexcept {}

void Control::clear_dirty(Dirty cleared) noexcept {
    dirty_ = without_dirty(dirty_, cleared);
}

void Control::add_dirty(Dirty added) noexcept {
    dirty_ |= added;
    if (has_dirty(added, Dirty::measure)) {
        advance_measure_generation();
    }
}

void Control::advance_measure_generation() noexcept {
    ++measure_generation_;
    // Zero marks an empty cache entry, so it is never a current generation.
    if (measure_generation_ == 0U) ++measure_generation_;
}

void Control::clear_subtree_dirty(Dirty cleared) noexcept {
    subtree_dirty_ = without_dirty(subtree_dirty_, cleared);
}

void Control::set_provider_error(std::uint64_t provider_id, std::string error) {
    require_mutable();
    if (error.empty()) {
        clear_provider_error(provider_id);
        return;
    }
    const ProviderTextMap::iterator found = provider_errors_.find(provider_id);
    if (found != provider_errors_.end() && (*found).second == error) return;
    provider_errors_[provider_id] = std::move(error);
    invalidate(Dirty::semantics | Dirty::accessibility);
}

void Control::clear_provider_error(std::uint64_t provider_id) {
    require_mutable();
    if (provider_errors_.erase(provider_id) != 0U) {
        invalidate(Dirty::semantics | Dirty::accessibility);
    }
}

void Control::set_provider_help(std::uint64_t provider_id, std::string help) {
    require_mutable();
    if (help.empty()) {
        clear_provider_help(provider_id);
        return;
    }
    const ProviderTextMap::iterator found = provider_help_.find(provider_id);
    if (found != provider_help_.end() && (*found).second == help) return;
    provider_help_[provider_id] = std::move(help);
    invalidate(Dirty::semantics | Dirty::accessibility);
}

void Control::clear_provider_help(std::uint64_t provider_id) {
    require_mutable();
    if (provider_help_.erase(provider_id) != 0U) {
        invalidate(Dirty::semantics | Dirty::accessibility);
    }
}

std::uint64_t Control::subtree_size() const noexcept {
    std::uint64_t result = 1;
    for (const Ptr& child : children_) {
        result += (*child).subtree_size();
    }
    return result;
}

bool Control::initialization_blocked() const noexcept {
    const Control* current = this;
    Ptr retained;
    while (current != nullptr) {
        if ((*current).initialization_depth_ != 0U) return true;
        retained = (*current).parent_.lock();
        current = retained.get();
    }
    return false;
}

void Control::require_mutable() const {
    if (!is_alive()) {
        throw std::logic_error("GUI.Forms cannot mutate a disposed control");
    }
    if (surface_root_) {
        (*surface_root_).require_root_thread("control mutation");
    }
}

void Control::verify_dispose_thread() {
    if (lifecycle_notification_ ||
        (surface_root_ != nullptr && (*surface_root_).in_lifecycle_notification_)) {
        throw std::logic_error("GUI.Forms cannot dispose a control during lifecycle notification");
    }
    if (surface_root_) {
        (*surface_root_).require_root_thread("control disposal");
    }
}

void Control::on_dispose() noexcept {
    if (data_bindings_) {
        (*data_bindings_).clear();
        data_bindings_.reset();
    }
    initialization_depth_ = 0;
    pending_initialization_dirty_ = Dirty::none;
    pending_initialization_subtree_ = false;
    pending_initialization_changes_.clear();
    tag_.reset();
    theme_override_.reset();
    provider_errors_.clear();
    provider_help_.clear();
    causes_validation_changed_.disconnect_all();
    validating_.disconnect_all();
    validated_.disconnect_all();
    Ptr self = weak_from_this().lock();
    if (surface_root_ && self) {
        (*surface_root_).dispose_subtree(self);
    } else if (std::shared_ptr<gui_forms::Control> visual_parent = parent_.lock()) {
        ChildList::iterator found = (*visual_parent).children_.begin();
        while (found != (*visual_parent).children_.end() &&
               (*found).get() != this) {
            ++found;
        }
        if (found != (*visual_parent).children_.end()) {
            (*visual_parent).children_.erase(found);
        }
        parent_.reset();
    }

    std::vector<Ptr> visual_children = std::move(children_);
    children_.clear();
    display_chunk_.reset();
    for (const std::shared_ptr<gui_forms::Control>& child : visual_children) {
        (*child).parent_.reset();
        (*child).dispose();
    }
}

namespace {

// The events Control adds to those of Component, in declaration order.
constexpr std::array<InteractionDeclaration, 14> control_interactions{{
    {.name = "name_changed", .payload = "std::string", .mutable_payload = false},
    {.name = "visible_changed", .payload = "bool", .mutable_payload = false},
    {.name = "enabled_changed", .payload = "bool", .mutable_payload = false},
    {.name = "auto_size_changed", .payload = "bool", .mutable_payload = false},
    {.name = "causes_validation_changed", .payload = "bool", .mutable_payload = false},
    {.name = "validating", .payload = "ControlValidationEvent", .mutable_payload = true},
    {.name = "validated", .payload = "", .mutable_payload = false},
    {.name = "tool_tip_text_changed", .payload = "std::string", .mutable_payload = false},
    {.name = "initialization_completed", .payload = "Dirty, bool", .mutable_payload = false},
    {.name = "pointer_observed", .payload = "PointerEvent", .mutable_payload = false},
    {.name = "pointer_preview_observed", .payload = "PointerEvent", .mutable_payload = false},
    {.name = "focus_observed", .payload = "bool", .mutable_payload = false},
    {.name = "arranged_bounds_changed", .payload = "Rect", .mutable_payload = false},
    {.name = "help_requested", .payload = "HelpRequestEvent", .mutable_payload = true},
}};

} // namespace

void Control::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Component::append_interactions(interactions);
    interactions.insert(interactions.end(), control_interactions.begin(), control_interactions.end());
}

} // namespace gui_forms
