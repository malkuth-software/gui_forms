#include "gui_forms/controls/button_base/button_base.hpp"
#include "gui_forms/controls/button_base/button/button.hpp"
#include "gui_forms/controls/button_base/check_box/check_box.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/connected_controls.hpp"
#include "../basic/basic_control_rendering.hpp"
#include "choice_indicator_property.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

std::shared_ptr<const PropertyEnumDescriptor> button_base_detail::choice_indicator_style_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "indicator_style", {{"classic", 0}, {"modern", 1}, {"toggle", 2}}, false});
    return value;
}

class ButtonBase::CommandConnection final
    : public std::enable_shared_from_this<CommandConnection> {
public:
    CommandConnection(ButtonBase& button, Command& command)
        : button_(button), command_(command) {
        if (!command.is_alive()) throw std::invalid_argument("cannot bind a disposed Command");
        const Delegate<const CommandState&> changed =
            Delegate<const CommandState&>::bind<CommandConnection,
                &CommandConnection::apply>(*this);
        state_ = command.state_changed().subscribe(button, changed);
        const Delegate<ButtonBase&> clicked = Delegate<ButtonBase&>::bind<
            CommandConnection, &CommandConnection::invoke>(*this);
        click_ = button.clicked().subscribe(button, clicked);
        CheckBox* const check_box = dynamic_cast<CheckBox*>(&button);
        if (check_box != nullptr) {
            const Delegate<bool> checked = Delegate<bool>::bind<
                CommandConnection, &CommandConnection::publish_checked>(*this);
            checked_ = (*check_box).checked_changed().subscribe(button, checked);
        }
    }

    [[nodiscard]] bool connected() const noexcept {
        const bool connected = state_.connected();
        return connected;
    }
    void synchronize() { apply(command_.state()); }
    void disconnect() noexcept {
        state_.disconnect();
        click_.disconnect();
        checked_.disconnect();
    }

private:
    void apply(const CommandState& state) {
        const std::shared_ptr<CommandConnection> connection = shared_from_this();
        const Control::Ptr retained = button_.weak_from_this().lock();
        const bool enabled = state.enabled;
        const bool checked = state.checked;
        const bool checkable = state.checkable;
        const std::uint64_t generation = state.generation;
        ButtonBase& button = button_;
        button.set_enabled(enabled);
        // set_enabled may remove this connection; use only the retained control.
        if (!button.is_alive() || !state_.connected()) return;
        if (command_.state().generation != generation) return;
        CheckBox* const check_box = dynamic_cast<CheckBox*>(&button);
        if (check_box != nullptr && checkable) {
            if ((*check_box).checked() != checked) (*check_box).set_checked(checked);
            return;
        }
        Button* const push_button = dynamic_cast<Button*>(&button);
        if (push_button != nullptr) (*push_button).set_selected(checkable && checked);
    }
    void publish_checked(const bool checked) {
        if (!state_.connected()) return;
        command_.set_checked(checked);
    }
    // Through the window's command chain when the button has a window
    // (owner decision DK-47); a detached button executes the command.
    void invoke(ButtonBase& source) {
        if (!state_.connected()) return;
        Window* const window = source.attached_window();
        if (window != nullptr) {
            static_cast<void>(command_.execute_routed(*window, source.stable_id().value()));
            return;
        }
        static_cast<void>(command_.execute(source.stable_id().value()));
    }
    ButtonBase& button_;
    Command& command_;
    SubscriptionToken state_{};
    SubscriptionToken click_{};
    SubscriptionToken checked_{};
};

ButtonBase::~ButtonBase() = default;

void ButtonBase::bind(Command& command) {
    require_mutable();
    const Control::Ptr retained = weak_from_this().lock();
    std::shared_ptr<CommandConnection> connection =
        std::make_shared<CommandConnection>(*this, command);
    unbind_command();
    command_connection_ = std::move(connection);
    (*command_connection_).synchronize();
}

bool ButtonBase::command_connected() const noexcept {
    const bool connected = command_connection_ && (*command_connection_).connected();
    return connected;
}

void ButtonBase::unbind_command() noexcept {
    if (command_connection_) (*command_connection_).disconnect();
    command_connection_.reset();
}


namespace {

// aligned_rect for a button's text block: a block taller than the content
// box keeps its own height and overflows by its alignment (centred for a
// middle row), instead of being pinned to the content's top, which put the
// baseline on or below the bottom edge of a short button (sweep defect D6).
[[nodiscard]] Rect aligned_text_block(Rect content, Size size, ContentAlignment alignment) noexcept {
    Rect placed = aligned_rect(content, size, alignment);
    if (!(size.height > content.height)) return placed;
    double share = 0.5;
    switch (alignment) {
    case ContentAlignment::top_left:
    case ContentAlignment::top_center:
    case ContentAlignment::top_right:
        share = 0.0;
        break;
    case ContentAlignment::middle_left:
    case ContentAlignment::middle_center:
    case ContentAlignment::middle_right:
        share = 0.5;
        break;
    case ContentAlignment::bottom_left:
    case ContentAlignment::bottom_center:
    case ContentAlignment::bottom_right:
        share = 1.0;
        break;
    default:
        // ContentAlignment names only the nine values above; a value outside
        // them centres the block like the middle row.
        share = 0.5;
        break;
    }
    placed.y = content.y + (content.height - size.height) * share;
    placed.height = size.height;
    return placed;
}

std::shared_ptr<const PropertyEnumDescriptor> content_alignment_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "content_alignment",
            {{"top_left", 0}, {"top_center", 1}, {"top_right", 2}, {"middle_left", 3}, {"middle_center", 4},
             {"middle_right", 5}, {"bottom_left", 6}, {"bottom_center", 7}, {"bottom_right", 8}},
            false});
    return value;
}

std::vector<std::string_view> button_text_lines(std::string_view text) {
    std::vector<std::string_view> result;
    if (text.empty()) return result;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find('\n', start);
        std::string_view line = text.substr(start, end == std::string_view::npos ? end : end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        result.push_back(line);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

// The extent of a text block `text` and an image `image` placed by
// `relation`, `gap` apart when side by side or stacked.
Size combined_content_size(TextImageRelation relation, Size text, Size image, double gap) noexcept {
    switch (relation) {
    case TextImageRelation::image_before_text:
    case TextImageRelation::text_before_image: {
        const Size beside{image.width + gap + text.width, std::max(image.height, text.height)};
        return beside;
    }
    case TextImageRelation::image_above_text:
    case TextImageRelation::text_above_image: {
        const Size stacked{std::max(image.width, text.width), image.height + gap + text.height};
        return stacked;
    }
    case TextImageRelation::overlay: {
        const Size overlaid{std::max(image.width, text.width), std::max(image.height, text.height)};
        return overlaid;
    }
    }
    // A relation outside the enumeration overlays the two.
    const Size overlaid{std::max(image.width, text.width), std::max(image.height, text.height)};
    return overlaid;
}
}

ButtonBase::ButtonBase(StableId stable_id, std::string text)
    : Control(std::move(stable_id)), text_(std::move(text)) {
    set_focusable(true);
    set_cursor(CursorKind::hand);

    PropertyDescriptor text_alignment;
    text_alignment.name = "text_alignment";
    text_alignment.kind = BindingValueKind::enumeration;
    text_alignment.category = "Appearance";
    text_alignment.description = "Where the text sits in the content area.";
    text_alignment.invalidation_effects = Dirty::paint | Dirty::semantics;
    text_alignment.bindable = false;
    text_alignment.enumeration = content_alignment_enum();
    define_bindable_property({
        std::move(text_alignment),
        detail::EnumerationPropertyGetter<ButtonBase, ContentAlignment>(
            *this, &ButtonBase::text_alignment, content_alignment_enum()),
        detail::EnumerationPropertySetter<ButtonBase, ContentAlignment>(*this,
                                                                        &ButtonBase::set_text_alignment),
        {}, {}, {}});
    define_bindable_property({
        {"text", BindingValueKind::text, "Appearance",
         "Text displayed by the button.", BindingValue{std::string{}},
         invalidation::text_content},
        detail::BindingMemberGetter<ButtonBase, std::string>(
            *this, &ButtonBase::text_),
        detail::ConvertedPropertySetter<ButtonBase, std::string>(
            *this, &ButtonBase::set_text, BindingValueKind::text,
            "ButtonBase.Text binding requires text"),
        detail::EventChangeConnector<const std::string&>(text_changed_), {}, {}});

    PropertyDescriptor font;
    font.name = "font";
    font.kind = BindingValueKind::font;
    font.category = "Appearance";
    font.description = "Font used for button measurement and painting.";
    font.default_value = BindingValue{
        FontSpec{FontRole::control, 12.0, 400, false, 0.24}};
    font.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    font.bindable = false;
    define_bindable_property({
        std::move(font),
        detail::BindingMemberGetter<ButtonBase, FontSpec>(
            *this, &ButtonBase::font_),
        detail::DirectPropertySetter<ButtonBase, FontSpec>(
            *this, &ButtonBase::set_font), {}, {}, {}});

    PropertyDescriptor image;
    image.name = "image";
    image.kind = BindingValueKind::image;
    image.category = "Appearance";
    image.description = "Direct generational image resource for button content.";
    image.default_value = BindingValue{ImageId{}};
    image.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    image.bindable = false;
    define_bindable_property({
        std::move(image),
        detail::BindingMemberGetter<ButtonBase, ImageId>(
            *this, &ButtonBase::image_),
        detail::DirectPropertySetter<ButtonBase, ImageId>(
            *this, &ButtonBase::set_image), {},
        detail::BoundMemberFunction<void (ButtonBase::*)()>(
            *this, &ButtonBase::clear_image),
        detail::BoundMemberFunction<bool (ButtonBase::*)() const noexcept>(
            *this, &ButtonBase::should_serialize_image)});

    PropertyDescriptor content_padding;
    content_padding.name = "content_padding";
    content_padding.kind = BindingValueKind::insets;
    content_padding.category = "Layout";
    content_padding.description =
        "Insets between the button frame and its image/text content.";
    content_padding.default_value = BindingValue{
        Insets{6.0, 4.0, 6.0, 4.0}};
    content_padding.invalidation_effects = Dirty::measure | Dirty::paint;
    content_padding.bindable = false;
    define_bindable_property({
        std::move(content_padding),
        detail::BindingMemberGetter<ButtonBase, Insets>(
            *this, &ButtonBase::content_padding_),
        detail::DirectPropertySetter<ButtonBase, Insets>(
            *this, &ButtonBase::set_content_padding), {}, {}, {}});
}

bool ButtonBase::should_serialize_image() const noexcept {
    return image_.value != 0U || image_index_ >= 0 || !image_key_.empty();
}

void ButtonBase::set_text(std::string text) {
    require_mutable();
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    publish_change(text_changed_, text_);
}

void ButtonBase::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) {
        throw std::invalid_argument("Button font specification is invalid");
    }
    if (font_ == font) {
        return;
    }
    font_ = font;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void ButtonBase::set_text_line_spacing(double spacing) {
    require_mutable();
    if (!std::isfinite(spacing) || spacing < 0.75 || spacing > 3.0) {
        throw std::invalid_argument(
            "Button text line spacing must be finite and between 0.75 and 3.0");
    }
    if (text_line_spacing_ == spacing) return;
    text_line_spacing_ = spacing;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void ButtonBase::set_style(BasicControlStyle style) {
    require_mutable();
    if (style_override_ && *style_override_ == style) {
        return;
    }
    style_override_ = std::move(style);
    invalidate(Dirty::paint | Dirty::semantics);
}

const BasicControlStyle& ButtonBase::style() const noexcept {
    return style_override_ ? *style_override_ : effective_theme().basic_style();
}

void ButtonBase::clear_style() {
    require_mutable();
    if (!style_override_) return;
    style_override_.reset();
    invalidate(Dirty::style | Dirty::paint | Dirty::semantics);
}

void ButtonBase::set_visual_recipes(ControlStateRecipes recipes) {
    require_mutable();
    if (!valid_control_state_recipes(recipes)) {
        throw std::invalid_argument("Button visual recipes are invalid");
    }
    if (visual_recipes_override_ && *visual_recipes_override_ == recipes) {
        return;
    }
    visual_recipes_override_ = std::move(recipes);
    invalidate(Dirty::style | Dirty::paint | Dirty::semantics);
}

void ButtonBase::clear_visual_recipes() {
    require_mutable();
    if (!visual_recipes_override_) return;
    visual_recipes_override_.reset();
    invalidate(Dirty::style | Dirty::paint | Dirty::semantics);
}

void ButtonBase::set_connection_topology(
    std::optional<ConnectedControlTopology> topology) {
    require_mutable();
    if (topology && !valid_connected_control_topology(*topology)) {
        throw std::invalid_argument(
            "ButtonBase connection topology requires count >= 2 and index < count");
    }
    if (connection_topology_ == topology) return;
    connection_topology_ = topology;
    invalidate(Dirty::style | Dirty::paint | Dirty::semantics);
}

void ButtonBase::set_image(ImageId image) {
    require_mutable();
    if (image.value != 0U && window() != nullptr &&
        !(*window()).image_resources().find(image)) {
        throw std::invalid_argument("Button image ID is not live in its Window");
    }
    if (image_ == image && image_index_ == -1 && image_key_.empty()) return;
    image_ = image;
    image_index_ = -1;
    image_key_.clear();
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void ButtonBase::clear_image() {
    set_image({});
}

void ButtonBase::set_image_list(std::shared_ptr<ImageList> image_list) {
    require_mutable();
    if (image_list && !(*image_list).is_alive()) {
        throw std::invalid_argument("Button requires a live ImageList");
    }
    if (image_list && window() != nullptr &&
        !(*image_list).belongs_to(*window())) {
        throw std::invalid_argument(
            "Button and ImageList must belong to the same Window");
    }
    if (image_list && image_index_ >= 0 &&
        static_cast<std::size_t>(image_index_) >= (*image_list).count()) {
        throw std::out_of_range(
            "Button image index is outside the assigned ImageList");
    }
    if (image_list_ == image_list) return;
    image_list_changed_.disconnect();
    image_list_ = std::move(image_list);
    if (image_list_) {
        image_list_changed_ = (*image_list_).changed().subscribe(
            *this,
            Delegate<const ImageListChange&>::bind<
                ButtonBase, &ButtonBase::on_image_list_changed>(*this));
    }
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void ButtonBase::on_image_list_changed(const ImageListChange&) {
    if (is_alive()) {
        invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    }
}

void ButtonBase::set_image_index(int image_index) {
    require_mutable();
    if (image_index < -1) {
        throw std::out_of_range("Button image index must be -1 or nonnegative");
    }
    if (image_index >= 0 && image_list_ &&
        static_cast<std::size_t>(image_index) >= (*image_list_).count()) {
        throw std::out_of_range("Button image index is outside its ImageList");
    }
    if (image_index_ == image_index && image_.value == 0U && image_key_.empty()) {
        return;
    }
    image_ = {};
    image_key_.clear();
    image_index_ = image_index;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void ButtonBase::set_image_key(std::string image_key) {
    require_mutable();
    if (image_key.size() > 256U) {
        throw std::invalid_argument("Button image key exceeds 256 bytes");
    }
    if (!image_key.empty() && !validate_utf8(image_key).valid()) {
        throw std::invalid_argument("Button image key must be valid UTF-8");
    }
    if (image_key_ == image_key && image_.value == 0U && image_index_ == -1) {
        return;
    }
    image_ = {};
    image_index_ = -1;
    image_key_ = std::move(image_key);
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void ButtonBase::set_image_alignment(ContentAlignment alignment) {
    require_mutable();
    if (!valid_content_alignment(alignment)) {
        throw std::invalid_argument("invalid Button image alignment");
    }
    if (image_alignment_ == alignment) return;
    image_alignment_ = alignment;
    invalidate(Dirty::paint | Dirty::semantics);
}

void ButtonBase::set_text_alignment(ContentAlignment alignment) {
    require_mutable();
    if (!valid_content_alignment(alignment)) {
        throw std::invalid_argument("invalid Button text alignment");
    }
    if (text_alignment_ == alignment) return;
    text_alignment_ = alignment;
    invalidate(Dirty::paint | Dirty::semantics);
}

void ButtonBase::set_text_image_relation(TextImageRelation relation) {
    require_mutable();
    if (!valid_text_image_relation(relation)) {
        throw std::invalid_argument("invalid Button text/image relation");
    }
    if (text_image_relation_ == relation) return;
    text_image_relation_ = relation;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void ButtonBase::set_image_gap(double gap) {
    require_mutable();
    if (!std::isfinite(gap) || gap < 0.0 || gap > 64.0) {
        throw std::invalid_argument(
            "Button image gap must be finite and between zero and 64");
    }
    if (image_gap_ == gap) return;
    image_gap_ = gap;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void ButtonBase::set_content_padding(Insets padding) {
    require_mutable();
    const double values[] = {
        padding.left, padding.top, padding.right, padding.bottom};
    for (const double value : values) {
        if (!std::isfinite(value) || value < 0.0 || value > 128.0) {
            throw std::invalid_argument(
                "Button content padding must be finite and between zero and 128");
        }
    }
    if (content_padding_ == padding) return;
    content_padding_ = padding;
    invalidate(Dirty::measure | Dirty::paint);
}

void ButtonBase::set_use_mnemonic(bool value) {
    require_mutable();
    if (use_mnemonic_ == value) return;
    use_mnemonic_ = value;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

std::string ButtonBase::display_text() const {
    return use_mnemonic_ ? parse_mnemonic_text(text_).display_text : text_;
}

bool ButtonBase::perform_click() {
    const Control::Ptr retained = weak_from_this().lock();
    if (!prepare_command_activation()) return false;
    on_activate();
    return true;
}

bool ButtonBase::perform_dialog_command() { return perform_click(); }

void ButtonBase::on_attached_to_window() {
    Control::on_attached_to_window();
    if (image_list_ && !(*image_list_).belongs_to(*window())) {
        throw std::logic_error("Button cannot attach to a different ImageList Window");
    }
    if (image_.value != 0U && !(*window()).image_resources().find(image_)) {
        throw std::logic_error("Button cannot attach with a foreign or stale image ID");
    }
}

void ButtonBase::set_expanded_state(std::optional<bool> expanded) {
    require_mutable();
    if (expanded_state_ == expanded) return;
    expanded_state_ = expanded;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(expanded_changed_, expanded);
}

Size ButtonBase::measure(Size available) {
    const Rect requested = requested_bounds();
    const FontSpec font = effective_font(font_);
    const std::string display = display_text();
    const std::vector<std::string_view> lines = button_text_lines(display);
    double text_width = 0.0;
    for (const std::string_view line : lines) {
        text_width = std::max(text_width, estimated_text_width(line, font));
    }
    const double text_height = static_cast<double>(lines.size()) * font.size * text_line_spacing_;
    Size image_size{};
    if (image_.value != 0U) {
        if (window() != nullptr) {
            if (const std::optional<ImageResourceView> resource = (*window()).image_resources().find(image_)) {
                image_size = {static_cast<double>((*resource).metadata.width),
                              static_cast<double>((*resource).metadata.height)};
            }
        }
    } else if (image_list_ && (*image_list_).is_alive() &&
               (!image_key_.empty() || image_index_ >= 0)) {
        image_size = (*image_list_).image_size();
    }
    const bool has_text = text_width > 0.0;
    const bool has_image = image_size.width > 0.0 && image_size.height > 0.0;
    const double gap = has_text && has_image ? image_gap_ : 0.0;
    Size content{text_width, text_height};
    if (has_image) {
        content = combined_content_size(text_image_relation_, {text_width, text_height},
                                        image_size, gap);
    }
    const double preferred_width = requested.width > 0.0
        ? requested.width
        : content.width + content_padding_.left + content_padding_.right + 10.0;
    const double preferred_height = requested.height > 0.0
        ? requested.height
        : std::max(24.0, content.height + content_padding_.top +
                                     content_padding_.bottom + 4.0);
    return {std::min(available.width, preferred_width),
            std::min(available.height, preferred_height)};
}

Rect ButtonBase::local_bounds() const noexcept {
    const Rect arranged = committed_arranged_bounds();
    return {0.0, 0.0, arranged.width, arranged.height};
}

bool ButtonBase::focus_cue_visible() const noexcept {
    if (!focused_) return false;
    Window* owner = window();
    return owner == nullptr || (*owner).focused_control().get() != this ||
           (*owner).focus_cue_visible();
}

ImageListResolution ButtonBase::resolved_button_image(bool selected) const noexcept {
    ImageListResolution result;
    const ImageVisualState state = !effectively_enabled()
        ? ImageVisualState::disabled
        : pressed_visual() ? ImageVisualState::pressed
        : hovered_visual() ? ImageVisualState::hot
        : selected ? ImageVisualState::selected
                   : ImageVisualState::normal;
    result.requested_state = state;
    result.requested_scale = window() ? (*window()).scale() : 1.0;
    if (image_.value != 0U && window() != nullptr) {
        const std::optional<ImageResourceView> resource = (*window()).image_resources().find(image_);
        if (!resource) return result;
        result.image = image_;
        result.source_size = {static_cast<double>((*resource).metadata.width),
                              static_cast<double>((*resource).metadata.height)};
        result.resolved_state = ImageVisualState::normal;
        result.resolved_scale = 1.0;
        return result;
    }
    if (!image_list_ || !(*image_list_).is_alive() || window() == nullptr ||
        !(*image_list_).belongs_to(*window())) return result;
    if (!image_key_.empty()) {
        return (*image_list_).resolve(image_key_, state, (*window()).scale());
    }
    if (image_index_ >= 0) {
        return (*image_list_).resolve(static_cast<std::size_t>(image_index_), state,
                                    (*window()).scale());
    }
    return result;
}

Rect ButtonBase::content_box(Rect face, bool command_alignment) const noexcept {
    const double command_leading = command_alignment ? 6.0 : 0.0;
    const Rect content{
        face.x + content_padding_.left + command_leading,
        face.y + content_padding_.top,
        std::max(0.0, face.width - content_padding_.left -
                          content_padding_.right - command_leading),
        std::max(0.0, face.height - content_padding_.top -
                          content_padding_.bottom)};
    return content;
}

ButtonBase::ContentLayout ButtonBase::layout_content(
    Rect content, std::span<const Size> line_sizes, FontSpec font,
    bool selected, bool command_alignment) const {
    ContentLayout layout;
    double text_width = 0.0;
    double line_height = font.size * text_line_spacing_;
    for (std::size_t index = 0; index < line_sizes.size(); ++index) {
        text_width = std::max(text_width, line_sizes[index].width);
        line_height = std::max(line_height, line_sizes[index].height);
    }
    layout.line_height = line_height;
    const Size text_size{text_width,
                         static_cast<double>(line_sizes.size()) * line_height};
    const ImageListResolution image = resolved_button_image(selected);
    Size image_size{};
    if (image) {
        image_size = image_list_ && image_.value == 0U
            ? (*image_list_).image_size() : image.source_size;
    }
    const bool has_text = text_size.width > 0.0 && text_size.height > 0.0;
    const bool has_image = image && image_size.width > 0.0 &&
                           image_size.height > 0.0;
    const ContentAlignment group_alignment = command_alignment
        ? ContentAlignment::middle_left : text_alignment_;
    const double gap = has_text && has_image ? image_gap_ : 0.0;

    if (has_text && has_image && text_image_relation_ != TextImageRelation::overlay) {
        const bool horizontal =
            text_image_relation_ == TextImageRelation::image_before_text ||
            text_image_relation_ == TextImageRelation::text_before_image;
        const Size group_size = horizontal
            ? Size{image_size.width + gap + text_size.width,
                   std::max(image_size.height, text_size.height)}
            : Size{std::max(image_size.width, text_size.width),
                   image_size.height + gap + text_size.height};
        const Rect group = aligned_text_block(content, group_size, group_alignment);
        if (horizontal) {
            const bool image_first =
                text_image_relation_ == TextImageRelation::image_before_text;
            const double image_x = image_first ? group.x
                                               : group.x + text_size.width + gap;
            const double text_x = image_first ? group.x + image_size.width + gap
                                              : group.x;
            layout.image = Rect{image_x,
                                group.y + (group.height - image_size.height) * 0.5,
                                image_size.width, image_size.height};
            layout.text = Rect{text_x,
                               group.y + (group.height - text_size.height) * 0.5,
                               text_size.width, text_size.height};
        } else {
            const bool image_first =
                text_image_relation_ == TextImageRelation::image_above_text;
            const double image_y = image_first ? group.y
                                               : group.y + text_size.height + gap;
            const double text_y = image_first ? group.y + image_size.height + gap
                                              : group.y;
            layout.image = Rect{group.x + (group.width - image_size.width) * 0.5,
                                image_y, image_size.width, image_size.height};
            layout.text = Rect{group.x + (group.width - text_size.width) * 0.5,
                               text_y, text_size.width, text_size.height};
        }
    } else {
        if (has_image) layout.image = aligned_rect(content, image_size, image_alignment_);
        if (has_text) layout.text = aligned_text_block(content, text_size, group_alignment);
    }
    return layout;
}

ButtonBase::ContentLayout ButtonBase::published_content_layout(
    Rect face, bool selected, bool command_alignment) const {
    const Rect content = content_box(face, command_alignment);
    if (content.empty()) {
        ContentLayout empty;
        return empty;
    }
    const FontSpec font = effective_font(font_);
    const std::string display = display_text();
    const std::vector<std::string_view> lines = button_text_lines(display);
    std::vector<Size> line_sizes(lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        // The host installs its raster painter as the window's text metrics,
        // so this matches what paint measures; a detached control estimates.
        const ResolvedTextLayout measured = window() != nullptr
            ? (*window()).resolve_text_layout_utf8(lines[index], font)
            : estimate_text_layout_utf8(lines[index], font);
        line_sizes[index] = measured.logical_size;
    }
    const ContentLayout layout =
        layout_content(content, line_sizes, font, selected, command_alignment);
    return layout;
}

void ButtonBase::paint_button_content(Painter& painter, Rect bounds,
                                      std::string_view text, const ButtonContentInk& ink,
                                      Point offset, bool selected,
                                      bool command_alignment) const {
    const Rect content = content_box(bounds, command_alignment);
    if (content.empty()) return;
    const FontSpec font = effective_font(font_);
    const std::vector<std::string_view> lines = button_text_lines(text);
    std::vector<Size> line_sizes(lines.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        line_sizes[index] = painter.measure_text_utf8(lines[index], font);
    }
    const ContentLayout layout =
        layout_content(content, line_sizes, font, selected, command_alignment);
    const Rect image_rect = layout.image.value_or(Rect{});
    const Rect text_rect = layout.text.value_or(Rect{});
    const double line_height = layout.line_height;

    if (layout.image.has_value() && !image_rect.empty()) {
        const ImageListResolution image = resolved_button_image(selected);
        // The icon role dims a disabled button's image; an image list's own
        // disabled image is already the disabled look, so it is drawn as
        // given, in the image role.
        const bool authored_disabled = image.resolved_state == ImageVisualState::disabled;
        PartPaint icon;
        icon.family = ink.family;
        icon.role = authored_disabled ? PartRole::image : PartRole::icon;
        icon.image = image.image;
        icon.bounds = {image_rect.x + offset.x, image_rect.y + offset.y, image_rect.width, image_rect.height};
        if (!effectively_enabled()) icon.states.insert(AnatomyState::disabled);
        paint_part(painter, *this, icon);
    }
    if (layout.text.has_value() && !text_rect.empty()) {
        for (std::size_t index = 0; index < lines.size(); ++index) {
            const Size measured = line_sizes[index];
            double x = text_rect.x;
            if (text_alignment_ == ContentAlignment::top_center ||
                text_alignment_ == ContentAlignment::middle_center ||
                text_alignment_ == ContentAlignment::bottom_center) {
                x += (text_rect.width - measured.width) * 0.5;
            } else if (text_alignment_ == ContentAlignment::top_right ||
                       text_alignment_ == ContentAlignment::middle_right ||
                       text_alignment_ == ContentAlignment::bottom_right) {
                x += text_rect.width - measured.width;
            }
            // The label part centres its line box on its bounds, so the
            // bounds are that box, placed to keep the layout's baseline.
            const double box_height = std::max(measured.height, font.size * 1.2);
            const double box_ascent = std::max(font.size, box_height * 0.82);
            const double baseline = text_rect.y + offset.y + static_cast<double>(index) * line_height +
                                    std::max(font.size, line_height * 0.82);
            const double right = std::max(x + measured.width, text_rect.x + text_rect.width);
            PartPaint label;
            label.family = ink.family;
            label.role = PartRole::label;
            label.states = ink.states;
            label.text = lines[index];
            label.font = font;
            label.bounds = {x + offset.x, baseline - box_ascent, right - x, box_height};
            label.authored_foreground = ink.authored;
            paint_part(painter, *this, label);
        }
    }
}

namespace {

constexpr std::array<PartField<ButtonBase::Parts>, 5> button_base_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &ButtonBase::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &ButtonBase::Parts::face},
    {.declaration = {.name = "icon", .role = PartRole::icon, .composed_from = {}},
     .member = &ButtonBase::Parts::icon},
    {.declaration = {.name = "label", .role = PartRole::label, .composed_from = {}},
     .member = &ButtonBase::Parts::label},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &ButtonBase::Parts::focus},
}};

constexpr std::array<StateField<ButtonBase::States>, 9> button_base_state_fields{{
    {.state = AnatomyState::normal, .member = &ButtonBase::States::normal},
    {.state = AnatomyState::hot, .member = &ButtonBase::States::hot},
    {.state = AnatomyState::pressed, .member = &ButtonBase::States::pressed},
    {.state = AnatomyState::enabled, .member = &ButtonBase::States::enabled},
    {.state = AnatomyState::disabled, .member = &ButtonBase::States::disabled},
    {.state = AnatomyState::focused, .member = &ButtonBase::States::focused},
    {.state = AnatomyState::focus_visible, .member = &ButtonBase::States::focus_visible},
    {.state = AnatomyState::open, .member = &ButtonBase::States::open},
    {.state = AnatomyState::closed, .member = &ButtonBase::States::closed},
}};

constexpr std::array<PartDeclaration, 5> button_base_parts =
    part_declarations(button_base_part_fields);
constexpr std::array<AnatomyState, 9> button_base_states =
    state_declarations(button_base_state_fields);

constexpr ControlAnatomy button_base_anatomy{
    .control_id = "button_base",
    .parts = button_base_parts,
    .states = button_base_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& ButtonBase::anatomy() const noexcept {
    return button_base_anatomy;
}

void ButtonBase::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Parts record = push_button_parts(false, false);
    write_parts(anatomy(), button_base_part_fields, record, geometry);
}

ButtonBase::Parts ButtonBase::push_button_parts(bool selected,
                                                bool command_alignment) const {
    const Rect bounds = local_bounds();
    const ContentLayout layout =
        published_content_layout(bounds, selected, command_alignment);
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    record.face = PartGeometry::from_bounds(bounds);
    if (layout.image.has_value()) {
        record.icon = PartGeometry::from_bounds(*layout.image);
    }
    if (layout.text.has_value()) {
        record.label = PartGeometry::from_bounds(*layout.text);
    }
    // A cue part publishes the face it marks. Where the ring is stroked
    // relative to it is a theme recipe parameter until the engine (T2)
    // owns cue geometry.
    record.focus = PartGeometry::from_bounds(bounds);
    return record;
}

AnatomyStateSet ButtonBase::button_anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    // Hot and pressed are independent; the theme applies the surface
    // precedence of design language §8.4 rule 1.
    if (pressed_visual()) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::pressed);
    }
    if (hovered_) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::hot);
    }
    if (expanded_state_.has_value()) {
        if (*expanded_state_) {
            states.insert(AnatomyState::open);
        } else {
            states.insert(AnatomyState::closed);
        }
    }
    return states;
}

AnatomyStateSet ButtonBase::anatomy_states() const {
    return button_anatomy_states();
}

ButtonBase::Parts ButtonBase::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), button_base_part_fields, published.parts(), record);
    return record;
}

ButtonBase::States ButtonBase::states() const {
    States record;
    read_states(button_base_state_fields, anatomy_states(), record);
    return record;
}

void ButtonBase::paint_themed_button(Painter& painter, Rect bounds,
                                     ControlVisualRole role,
                                     bool default_cue,
                                     bool command_alignment,
                                     bool selected,
                                     std::optional<AuthoredButtonColours> authored) const {
    const ControlVisualContext context = visual_context(
        hovered_, pressed_visual(), selected, focus_cue_visible(), default_cue);
    const ControlVisualRecipe& recipe = resolve_visual_recipe(role, context);
    const Rect visual_bounds{
        bounds.x + recipe.visual_offset.x,
        bounds.y + recipe.visual_offset.y,
        bounds.width,
        bounds.height,
    };
    if (style_override_ && !authored) {
        authored = AuthoredButtonColours{(*style_override_).face, (*style_override_).text};
    }
    const bool engine_drawn = !visual_recipes_override_ && !connection_topology_;
    // The face, the cues and the content are parts the theme's engine draws,
    // so a theme's `::part(face)`, `::part(label)` and `::part(focus)` rules
    // select them; the states give the recipe context above.
    AnatomyStateSet states;
    if (hovered_) states.insert(AnatomyState::hot);
    if (pressed_visual()) states.insert(AnatomyState::pressed);
    if (selected) states.insert(AnatomyState::selected);
    if (focus_cue_visible()) states.insert(AnatomyState::focus_visible);
    if (default_cue) states.insert(AnatomyState::preselect);
    if (engine_drawn) {
        PartPaint face;
        face.family = role;
        face.role = PartRole::face;
        face.bounds = visual_bounds;
        face.states = states;
        face.key = PartKey{"face", {}, {}};
        if (authored) {
            face.authored_background = (*authored).face;
            face.authored_foreground = (*authored).text;
        }
        paint_part(painter, *this, face);
        // The default ring first, then the focus ring over it, in the
        // theme's own cue colours.
        PartPaint cue;
        cue.family = role;
        cue.bounds = visual_bounds;
        cue.states = states;
        cue.role = PartRole::preselect;
        paint_part(painter, *this, cue);
        cue.role = PartRole::focus;
        cue.key = PartKey{"focus", {}, {}};
        paint_part(painter, *this, cue);
    } else {
        paint_connected_surface_material(
            painter, visual_bounds, recipe.material, connection_topology_);
        // Connection shapes the physical stock only. Focus/default ownership is
        // still per control, so a joined segment's cues are square rather
        // than implying that the whole joined instrument owns one focus
        // identity. The default ring first, then the focus ring over it.
        PartPaint cue;
        cue.family = role;
        cue.bounds = visual_bounds;
        cue.states = states;
        if (connection_topology_) cue.corner_radius = 0.0;
        cue.role = PartRole::preselect;
        paint_part(painter, *this, cue);
        cue.role = PartRole::focus;
        paint_part(painter, *this, cue);
    }
    const Point offset = context.surface == ControlSurfaceState::pressed
        ? recipe.pressed_content_offset : Point{};
    // Authored text stands while the button is enabled; a disabled button
    // keeps the theme's disabled ink (T4-1). Per-control recipes give their
    // own text colour the same way.
    ButtonContentInk ink;
    ink.family = role;
    ink.states = states;
    if (authored) {
        ink.authored = (*authored).text;
    } else if (!engine_drawn) {
        ink.authored = recipe.text;
    }
    const std::string display = display_text();
    paint_button_content(painter, visual_bounds, display, ink, offset, selected,
                         command_alignment);
}

const ControlVisualRecipe& ButtonBase::resolve_visual_recipe(
    ControlVisualRole role, ControlVisualContext context) const noexcept {
    if (visual_recipes_override_ && !context.high_contrast) {
        return (*visual_recipes_override_).resolve(context.surface);
    }
    return effective_theme().resolve(role, context);
}

Insets ButtonBase::resolved_visual_outsets(
    ControlVisualRole role, ControlVisualContext context) const noexcept {
    const ControlVisualRecipe& recipe = resolve_visual_recipe(role, context);
    Insets result = connected_surface_visual_outsets(
        recipe.material, connection_topology_);
    double cue_extent = 0.0;
    if (context.focused && recipe.authored_focus_outline &&
        recipe.focus_width > 0.0) {
        cue_extent = std::max(0.0, recipe.focus_offset + recipe.focus_width);
    }
    result.left = std::max(
        0.0, std::max(result.left, cue_extent) - recipe.visual_offset.x);
    result.top = std::max(
        0.0, std::max(result.top, cue_extent) - recipe.visual_offset.y);
    result.right = std::max(
        0.0, std::max(result.right, cue_extent) + recipe.visual_offset.x);
    result.bottom = std::max(
        0.0, std::max(result.bottom, cue_extent) + recipe.visual_offset.y);
    return result;
}

void ButtonBase::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    paint_themed_button(painter, bounds, ControlVisualRole::button, false);
}

Insets ButtonBase::visual_outsets() const noexcept {
    const ControlVisualContext context = visual_context(
        hovered_, pressed_visual(), false, focus_cue_visible(), false);
    return resolved_visual_outsets(ControlVisualRole::button, context);
}

void ButtonBase::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::enter || event.action == PointerAction::leave) {
        const bool next = event.action == PointerAction::enter;
        if (hovered_ != next) {
            hovered_ = next;
            invalidate(Dirty::style | Dirty::paint);
        }
    } else if (event.button == PointerButton::primary &&
               event.action == PointerAction::down) {
        pointer_engaged_ = true;
        pointer_pressed_ = true;
        invalidate(Dirty::paint);
        event.handled = true;
    } else if (event.action == PointerAction::move && pointer_engaged_) {
        const bool inside = absolute_bounds().contains(event.position);
        if (pointer_pressed_ != inside) {
            pointer_pressed_ = inside;
            invalidate(Dirty::paint);
        }
        event.handled = true;
    } else if (event.button == PointerButton::primary &&
               event.action == PointerAction::up && pointer_engaged_) {
        pointer_engaged_ = false;
        pointer_pressed_ = false;
        invalidate(Dirty::paint);
        event.handled = true;
    }
}

void ButtonBase::on_key(KeyEvent& event) {
    const bool activation_key = event.physical_key == PhysicalKey::space ||
                                event.physical_key == PhysicalKey::enter;
    if (!activation_key) {
        return;
    }
    if (event.action == KeyAction::down && !event.repeat && !keyboard_pressed_) {
        keyboard_key_ = event.physical_key;
        keyboard_pressed_ = true;
        invalidate(Dirty::paint);
        event.handled = true;
    } else if (event.action == KeyAction::up && keyboard_pressed_ &&
               keyboard_key_ == event.physical_key) {
        keyboard_key_ = 0;
        keyboard_pressed_ = false;
        invalidate(Dirty::paint);
        event.handled = true;
        on_activate();
    }
}

bool ButtonBase::mnemonic_matches(char32_t character) const noexcept {
    return use_mnemonic_ && is_mnemonic(character, text_);
}

bool ButtonBase::process_mnemonic_self(char32_t character) {
    if (!mnemonic_matches(character)) return false;
    // WinForms treats a recognized mnemonic as owned even when validation
    // prevents the resulting command from firing.
    static_cast<void>(perform_click());
    return true;
}

void ButtonBase::on_focus_changed(bool focused) {
    focused_ = focused;
    if (!focused) {
        keyboard_key_ = 0;
        keyboard_pressed_ = false;
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void ButtonBase::on_activate() {
    clicked_.emit(*this);
}

SemanticDescriptor ButtonBase::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::button;
    descriptor.name = accessible_name().empty() ? display_text() : accessible_name();
    descriptor.description = accessible_description();
    if (use_mnemonic_) descriptor.access_key = mnemonic_access_key(text_);
    descriptor.actions = {SemanticAction::focus, SemanticAction::press};
    if (visual_status() == ControlVisualStatus::pending) {
        descriptor.states |= SemanticState::busy;
    } else if (visual_status() == ControlVisualStatus::invalid) {
        descriptor.states |= SemanticState::invalid;
    }
    if (expanded_state_) {
        if (*expanded_state_) descriptor.states |= SemanticState::expanded;
        descriptor.actions.push_back(*expanded_state_ ? SemanticAction::collapse
                                                      : SemanticAction::expand);
    }
    descriptor.exposed = true;
    return descriptor;
}

bool ButtonBase::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::press) {
        return perform_click();
    }
    if (expanded_state_ &&
        ((action == SemanticAction::expand && !*expanded_state_) ||
         (action == SemanticAction::collapse && *expanded_state_))) {
        on_activate();
        return true;
    }
    return Control::on_semantic_action(action, value);
}

namespace {

// The events ButtonBase adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 3> button_base_interactions{{
    {.name = "expanded_changed", .payload = "std::optional<bool>", .mutable_payload = false},
    {.name = "clicked", .payload = "ButtonBase", .mutable_payload = true},
    {.name = "text_changed", .payload = "std::string", .mutable_payload = false},
}};

} // namespace

void ButtonBase::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), button_base_interactions.begin(), button_base_interactions.end());
}

} // namespace gui_forms
