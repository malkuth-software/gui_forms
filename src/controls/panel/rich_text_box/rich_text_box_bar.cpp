#include "gui_forms/controls/panel/rich_text_box/rich_text_box.hpp"

#include "../text_editing/text_editing_engine.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms {

namespace {

// Each button's label and the name its semantic node and stable ID carry.
struct BarButtonText final {
    std::string_view label;
    std::string_view name;
    std::string_view id;
};

constexpr std::array<BarButtonText, rich_text_bar_button_count> bar_button_texts{{
    {.label = "B", .name = "Bold", .id = "bold"},
    {.label = "I", .name = "Italic", .id = "italic"},
    {.label = "Heading", .name = "Heading", .id = "heading"},
    {.label = "\xE2\x80\xA2 List", .name = "Bulleted list", .id = "bulleted-list"},
    {.label = "1. List", .name = "Numbered list", .id = "numbered-list"},
    {.label = "Link", .name = "Link", .id = "link"},
    {.label = "Code", .name = "Code", .id = "code"},
}};

// The heading menu's choices: body text and the first three levels.
constexpr std::array<std::string_view, 4> heading_choices{"Body", "Heading 1", "Heading 2", "Heading 3"};

constexpr double bar_padding = 3.0;
constexpr double button_inset = 7.0;
constexpr double arrow_width = 12.0;

[[nodiscard]] RichTextBarButton button_of(std::size_t index) noexcept {
    const RichTextBarButton button = static_cast<RichTextBarButton>(index);
    return button;
}

} // namespace

double RichTextBox::bar_height() const noexcept {
    if (!control_bar_visible_) return 0.0;
    const FontSpec font = text_font(font_);
    const double height = std::max(24.0, std::round(font.size * 1.4) + 2.0 * bar_padding + 4.0);
    return height;
}

std::array<Rect, rich_text_bar_button_count> RichTextBox::bar_buttons() const {
    std::array<Rect, rich_text_bar_button_count> buttons{};
    if (!control_bar_visible_) return buttons;
    const FontSpec font = text_font(font_);
    const double height = bar_height() - 2.0 * bar_padding;
    double x = bar_padding + 1.0;
    for (std::size_t index = 0U; index < rich_text_bar_button_count; ++index) {
        const std::string_view label = bar_button_texts[index].label;
        double width = resolve_text_layout_utf8(label, font).logical_size.width + 2.0 * button_inset;
        if (button_of(index) == RichTextBarButton::heading) width += arrow_width;
        width = std::max(width, height);
        buttons[index] = {x, bar_padding, width, height};
        x += width + 2.0;
    }
    return buttons;
}

std::optional<std::size_t> RichTextBox::bar_button_at(Point local) const {
    const std::array<Rect, rich_text_bar_button_count> buttons = bar_buttons();
    const Rect bar{0.0, 0.0, local_bounds().width, bar_height()};
    if (!bar.contains(local)) return std::nullopt;
    for (std::size_t index = 0U; index < buttons.size(); ++index) {
        if (buttons[index].contains(local)) {
            const std::optional<std::size_t> found = index;
            return found;
        }
    }
    return std::nullopt;
}

RichTextFormatState RichTextBox::bar_button_state(RichTextBarButton button, const RichTextFormat& format) const {
    switch (button) {
    case RichTextBarButton::bold:
        return format.bold;
    case RichTextBarButton::italic:
        return format.italic;
    case RichTextBarButton::heading:
        return format.heading;
    case RichTextBarButton::bulleted_list:
        return format.bulleted;
    case RichTextBarButton::numbered_list:
        return format.numbered;
    case RichTextBarButton::link:
        return format.link;
    case RichTextBarButton::code:
        return format.code;
    }
    // A value outside the enumeration names no format.
    return RichTextFormatState::off;
}

std::size_t RichTextBox::realized_item_count() const {
    const std::size_t count = control_bar_visible_ ? rich_text_bar_button_count : 0U;
    return count;
}

std::string RichTextBox::realized_item_stable_id(std::size_t item) const {
    const std::string id = std::string(stable_id().value()) + ".bar." + std::string(bar_button_texts[item].id);
    return id;
}

AnatomyStateSet RichTextBox::realized_item_anatomy_states(std::size_t item) const {
    AnatomyStateSet states{};
    const RichTextFormat format = selection_format();
    if (hot_button_ && *hot_button_ == item) states.insert(AnatomyState::hot);
    if (pressed_button_ && *pressed_button_ == item) states.insert(AnatomyState::pressed);
    const RichTextFormatState state = bar_button_state(button_of(item), format);
    if (state == RichTextFormatState::on) states.insert(AnatomyState::checked);
    if (state == RichTextFormatState::mixed) states.insert(AnatomyState::indeterminate);
    if (read_only() || !enabled.read()) states.insert(AnatomyState::disabled);
    if (heading_menu_open_ && button_of(item) == RichTextBarButton::heading) states.insert(AnatomyState::open);
    return states;
}

void RichTextBox::paint_bar(Painter& painter) const {
    if (!control_bar_visible_) return;
    const Rect bounds = local_bounds();
    const double height = bar_height();
    PartPaint strip;
    strip.family = ControlVisualRole::panel;
    strip.role = PartRole::face;
    strip.bounds = {1.0, 1.0, std::max(0.0, bounds.width - 2.0), height - 1.0};
    paint_part(painter, *this, strip);
    strip.role = PartRole::divider;
    strip.bounds = {1.0, height - 1.0, std::max(0.0, bounds.width - 2.0), 1.0};
    paint_part(painter, *this, strip);
    const std::array<Rect, rich_text_bar_button_count> buttons = bar_buttons();
    const FontSpec font = text_font(font_);
    painter.save();
    painter.clip_rect({0.0, 0.0, bounds.width, height});
    for (std::size_t index = 0U; index < buttons.size(); ++index) {
        const Rect button = buttons[index];
        const AnatomyStateSet states = realized_item_anatomy_states(index);
        PartPaint part;
        part.family = ControlVisualRole::menu_item;
        part.states = states;
        part.role = PartRole::face;
        part.bounds = button;
        paint_part(painter, *this, part);
        if (states.contains(AnatomyState::indeterminate)) {
            // Mixed: the checked face at half strength over the button's own,
            // between off and on.
            PartPaint mixed = part;
            mixed.states.insert(AnatomyState::checked);
            painter.begin_group({.bounds = button, .opacity = 0.5});
            paint_part(painter, *this, mixed);
            painter.end_group();
        }
        part.role = PartRole::label;
        part.font = font;
        const RichTextBarButton kind = button_of(index);
        if (kind == RichTextBarButton::bold) part.font.weight = 700U;
        if (kind == RichTextBarButton::italic) part.font.italic = true;
        part.alignment = PartTextAlignment::center;
        const double arrow = kind == RichTextBarButton::heading ? arrow_width : 0.0;
        part.bounds = {button.x, button.y, button.width - arrow, button.height};
        part.text = bar_button_texts[index].label;
        paint_part(painter, *this, part);
        part.text = {};
        if (kind == RichTextBarButton::heading) {
            part.role = PartRole::glyph;
            part.mark = PartMark::arrow_down;
            part.bounds = bar_item_parts(index).button_glyph.bounds;
            paint_part(painter, *this, part);
        }
    }
    painter.restore();
}

RichTextBox::ItemParts RichTextBox::bar_item_parts(std::size_t item) const {
    const std::array<Rect, rich_text_bar_button_count> buttons = bar_buttons();
    const Rect button = buttons[item];
    const bool heading = button_of(item) == RichTextBarButton::heading;
    const double arrow = heading ? arrow_width : 0.0;
    ItemParts record{};
    record.button = PartGeometry::from_bounds(button);
    record.button_label = PartGeometry::from_bounds({button.x, button.y, button.width - arrow, button.height});
    if (heading) {
        const double side = 8.0;
        record.button_glyph = PartGeometry::from_bounds({button.x + button.width - button_inset - side * 0.5 - 2.0,
                                                         button.y + (button.height - side) * 0.5, side, side});
    }
    return record;
}

// -- input ------------------------------------------------------------------------

bool RichTextBox::bar_pointer(PointerEvent& event, Point local) {
    const std::optional<std::size_t> under = bar_button_at(local);
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        if (hot_button_ != under) {
            hot_button_ = under;
            invalidate(Dirty::paint);
        }
        if (pressed_button_) {
            event.handled = true;
            return true;
        }
        const bool over_bar = local.y < bar_height();
        if (over_bar && cursor.read() != CursorKind::arrow) cursor = CursorKind::arrow;
        return over_bar;
    }
    if (event.action == PointerAction::leave) {
        if (hot_button_) {
            hot_button_.reset();
            invalidate(Dirty::paint);
        }
        return false;
    }
    if (event.action == PointerAction::down && event.button == PointerButton::primary) {
        if (local.y >= bar_height()) return false;
        if (window() != nullptr) static_cast<void>((*window()).request_focus(shared_from_this()));
        event.handled = true;
        if (!under || read_only()) return true;
        if (button_of(*under) == RichTextBarButton::heading) {
            open_heading_menu();
            return true;
        }
        pressed_button_ = under;
        set_pointer_capture(true);
        invalidate(Dirty::paint);
        return true;
    }
    if (event.action == PointerAction::up && event.button == PointerButton::primary && pressed_button_) {
        const std::size_t pressed = *pressed_button_;
        pressed_button_.reset();
        set_pointer_capture(false);
        invalidate(Dirty::paint);
        event.handled = true;
        if (under && *under == pressed) invoke_bar_button(button_of(pressed));
        return true;
    }
    return false;
}

void RichTextBox::invoke_bar_button(RichTextBarButton button) {
    switch (button) {
    case RichTextBarButton::bold:
        static_cast<void>(toggle_bold());
        return;
    case RichTextBarButton::italic:
        static_cast<void>(toggle_italic());
        return;
    case RichTextBarButton::heading:
        open_heading_menu();
        return;
    case RichTextBarButton::bulleted_list:
        static_cast<void>(toggle_bulleted_list());
        return;
    case RichTextBarButton::numbered_list:
        static_cast<void>(toggle_numbered_list());
        return;
    case RichTextBarButton::link:
        static_cast<void>(insert_link());
        return;
    case RichTextBarButton::code:
        static_cast<void>(toggle_code());
        return;
    }
    // Not a button of the bar: the press is ignored.
    return;
}

void RichTextBox::open_heading_menu() {
    if (!heading_menu_ || window() == nullptr || read_only()) return;
    const RichTextFormat format = selection_format();
    // Mixed lines check no choice; a level past the menu's (4 to 6) neither.
    const std::optional<std::size_t> level = format.heading == RichTextFormatState::mixed
        ? std::nullopt : std::optional<std::size_t>(format.heading_level);
    const std::string prefix = std::string(stable_id().value()) + ".heading.";
    std::vector<MenuItemSpec> items{};
    items.reserve(heading_choices.size());
    for (std::size_t index = 0U; index < heading_choices.size(); ++index) {
        std::string identity = prefix + std::to_string(index);
        if (!heading_commands_[index]) {
            heading_commands_[index] = std::make_shared<Command>(identity, std::string(heading_choices[index]));
        }
        Command& command = *heading_commands_[index];
        command.set_checked(level.has_value() && index == *level);
        items.emplace_back(std::move(identity), MenuItemKind::radio, heading_commands_[index],
                           std::string(heading_choices[index]));
    }
    (*heading_menu_).set_items(std::move(items));
    const std::array<Rect, rich_text_bar_button_count> buttons = bar_buttons();
    const Rect button = buttons[static_cast<std::size_t>(RichTextBarButton::heading)];
    const Rect absolute = absolute_bounds();
    (*heading_menu_).set_preferred_width(160.0);
    (*heading_menu_).show(shared_from_this(), {absolute.x + button.x, absolute.y + button.y + button.height},
                          MenuOpenMode::pointer);
}

void RichTextBox::heading_chosen(const MenuItemInvocation& invocation) {
    const std::string prefix = std::string(stable_id().value()) + ".heading.";
    for (std::size_t index = 0U; index < heading_choices.size(); ++index) {
        if (invocation.item_id == prefix + std::to_string(index)) {
            static_cast<void>(set_heading_level(index));
            return;
        }
    }
}

void RichTextBox::heading_menu_open_changed(bool open) {
    heading_menu_open_ = open;
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- semantics ----------------------------------------------------------------------

std::vector<SemanticNode> RichTextBox::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    const std::size_t count = realized_item_count();
    if (count == 0U) return nodes;
    const Rect absolute = absolute_bounds();
    const std::array<Rect, rich_text_bar_button_count> buttons = bar_buttons();
    const RichTextFormat format = selection_format();
    const bool usable = enabled.read() && !read_only();
    nodes.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const RichTextBarButton button = button_of(index);
        SemanticNode node{};
        node.stable_id = realized_item_stable_id(index);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.name = std::string(bar_button_texts[index].name);
        const Rect bounds = buttons[index];
        node.bounds = {absolute.x + bounds.x, absolute.y + bounds.y, bounds.width, bounds.height};
        node.states = SemanticState::visible;
        if (usable) node.states |= SemanticState::enabled;
        if (button == RichTextBarButton::heading) {
            node.role = SemanticRole::menu_button;
            node.has_popup = true;
            if (heading_menu_open_) node.states |= SemanticState::expanded;
            // The level's choice; "Mixed" when the lines differ, and the
            // level as a heading past the menu's choices.
            if (format.heading == RichTextFormatState::mixed) {
                node.value = "Mixed";
            } else if (format.heading_level < heading_choices.size()) {
                node.value = std::string(heading_choices[format.heading_level]);
            } else {
                node.value = "Heading " + std::to_string(format.heading_level);
            }
            if (usable) node.actions = {SemanticAction::show_menu, SemanticAction::press};
        } else {
            node.role = SemanticRole::button;
            const bool toggles = button != RichTextBarButton::link;
            if (toggles) node.states |= SemanticState::checkable;
            const RichTextFormatState state = bar_button_state(button, format);
            if (toggles && state == RichTextFormatState::on) node.states |= SemanticState::checked;
            if (toggles && state == RichTextFormatState::mixed) node.states |= SemanticState::mixed;
            if (usable) node.actions = {SemanticAction::press};
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool RichTextBox::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view value) {
    const std::size_t count = realized_item_count();
    for (std::size_t index = 0U; index < count; ++index) {
        if (id != realized_item_stable_id(index)) continue;
        if (!enabled.read() || read_only()) return false;
        const RichTextBarButton button = button_of(index);
        const bool menu = button == RichTextBarButton::heading && action == SemanticAction::show_menu;
        if (action != SemanticAction::press && !menu) return false;
        invoke_bar_button(button);
        return true;
    }
    const bool handled = Panel::on_semantic_child_action(id, action, value);
    return handled;
}

} // namespace gui_forms
