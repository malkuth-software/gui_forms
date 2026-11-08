#include "gui_forms/controls/panel/combo_box/combo_box.hpp"

#include "drop_down_layer.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "../input_control_utilities.hpp"
#include "gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_layer.hpp"
#include "gui_forms/detail/bound_member_function.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/detail/weak_member_callback.hpp"
#include "gui_forms/text.hpp"
#include "../panel_anatomy.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {
using namespace input_control_detail;

namespace {

// The ComboBoxStyle and ComboBoxFilter enumerations, numbered as declared.
// Static storage; any thread may read them.
std::shared_ptr<const PropertyEnumDescriptor> combo_box_style_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "combo_box_style", {{"drop_down_list", 0}, {"drop_down", 1}}, false});
    return value;
}

std::shared_ptr<const PropertyEnumDescriptor> combo_box_filter_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "combo_box_filter", {{"none", 0}, {"prefix", 1}, {"substring", 2}}, false});
    return value;
}

} // namespace

ComboBox::ComboBox(StableId stable_id) : Panel(std::move(stable_id)) {
    auto_complete_custom_source_ = std::make_shared<AutoCompleteStringCollection>();
    list_items_source_ = std::make_shared<AutoCompleteStringCollection>();
    set_paint_plane(PaintPlane::control);
    border_style = BorderStyle::sunken;
    set_focusable(true);
    cursor = CursorKind::hand;
    PropertyDescriptor items_descriptor;
    items_descriptor.name = "items";
    items_descriptor.kind = BindingValueKind::collection;
    items_descriptor.category = "Data";
    items_descriptor.description =
        "Ordered text items presented by the drop-down.";
    items_descriptor.default_value = BindingValue{make_property_collection(
        "String", BindingValueKind::text, {})};
    items_descriptor.invalidation_effects =
        Dirty::measure | Dirty::paint | Dirty::semantics;
    items_descriptor.serialization_visibility =
        PropertySerializationVisibility::content;
    items_descriptor.bindable = false;
    gui_forms::PropertyRegistration items_registration;
    items_registration.descriptor = std::move(items_descriptor);
    items_registration.get = detail::BoundMemberFunction<
        BindingValue (ComboBox::*)() const>(
            *this, &ComboBox::items_property_value);
    items_registration.set = detail::BoundMemberFunction<
        void (ComboBox::*)(const BindingValue&)>(
            *this, &ComboBox::set_items_property);
    items_registration.connect_changed =
        detail::EventChangeConnector<>(items_changed_);
    items_registration.reset = detail::BoundMemberFunction<
        void (ComboBox::*)()>(*this, &ComboBox::reset_items_property);
    items_registration.should_serialize = detail::BoundMemberFunction<
        bool (ComboBox::*)() const noexcept>(
            *this, &ComboBox::should_serialize_items_property);
    define_bindable_property(std::move(items_registration));

    gui_forms::PropertyRegistration index_registration;
    index_registration.descriptor = {
        "selected_index", BindingValueKind::signed_integer, "Behavior",
        "Zero-based selected item index, or -1 when no item is selected.",
        BindingValue{std::int64_t{-1}}, Dirty::paint | Dirty::semantics};
    index_registration.get = detail::BoundMemberFunction<
        BindingValue (ComboBox::*)() const>(
            *this, &ComboBox::selected_index_property_value);
    index_registration.set = detail::BoundMemberFunction<
        void (ComboBox::*)(const BindingValue&)>(
            *this, &ComboBox::set_selected_index_property);
    index_registration.connect_changed =
        detail::EventChangeConnector<std::optional<std::size_t>>(
            selected_index_changed_);
    define_bindable_property(std::move(index_registration));

    gui_forms::PropertyRegistration text_registration;
    text_registration.descriptor = {
        "text", BindingValueKind::text, "Appearance",
        "Text of the field: the editor's in the drop_down style, otherwise the selected item's.",
        BindingValue{std::string{}}, Dirty::paint | Dirty::semantics};
    text_registration.get = detail::BoundMemberFunction<
        BindingValue (ComboBox::*)() const>(
            *this, &ComboBox::text_property_value);
    text_registration.set = detail::BoundMemberFunction<
        void (ComboBox::*)(const BindingValue&)>(
            *this, &ComboBox::set_text_property);
    text_registration.connect_changed =
        detail::EventChangeConnector<const std::string&>(text_changed_);
    text_registration.reset = detail::BoundMemberFunction<
        void (ComboBox::*)()>(*this, &ComboBox::reset_text_property);
    text_registration.should_serialize = detail::BoundMemberFunction<
        bool (ComboBox::*)() const noexcept>(
            *this, &ComboBox::should_serialize_text_property);
    define_bindable_property(std::move(text_registration));

    // What a form document sets besides the items (stage FD6.3). None
    // raises a change event.
    PropertyDescriptor placeholder;
    placeholder.name = "placeholder_text";
    placeholder.kind = BindingValueKind::text;
    placeholder.category = "Appearance";
    placeholder.description = "The hint the field shows while no item is selected and no text is typed.";
    placeholder.default_value = BindingValue{std::string{}};
    placeholder.invalidation_effects = Dirty::paint | Dirty::semantics;
    placeholder.bindable = false;
    define_bindable_property({
        std::move(placeholder),
        detail::BindingMemberGetter<ComboBox, std::string>(*this, &ComboBox::placeholder_),
        detail::DirectPropertySetter<ComboBox, std::string>(*this, &ComboBox::set_placeholder_text),
        {}, {}, {}});

    PropertyDescriptor style_descriptor;
    style_descriptor.name = "drop_down_style";
    style_descriptor.kind = BindingValueKind::enumeration;
    style_descriptor.category = "Appearance";
    style_descriptor.description = "Whether the field takes text (drop_down) or shows the selected item (drop_down_list).";
    style_descriptor.default_value = BindingValue{PropertyEnumValue{"combo_box_style", "drop_down_list", 0}};
    style_descriptor.invalidation_effects = Dirty::measure | Dirty::paint | Dirty::semantics;
    style_descriptor.bindable = false;
    style_descriptor.enumeration = combo_box_style_enum();
    define_bindable_property({
        std::move(style_descriptor),
        detail::EnumerationPropertyGetter<ComboBox, ComboBoxStyle>(
            *this, &ComboBox::drop_down_style, combo_box_style_enum()),
        detail::EnumerationPropertySetter<ComboBox, ComboBoxStyle>(*this, &ComboBox::set_drop_down_style),
        {}, {}, {}});

    PropertyDescriptor filter_descriptor;
    filter_descriptor.name = "drop_down_filter";
    filter_descriptor.kind = BindingValueKind::enumeration;
    filter_descriptor.category = "Behavior";
    filter_descriptor.description = "Which items a drop_down field's list shows as the user types.";
    filter_descriptor.default_value = BindingValue{PropertyEnumValue{"combo_box_filter", "none", 0}};
    filter_descriptor.invalidation_effects = Dirty::paint | Dirty::semantics;
    filter_descriptor.bindable = false;
    filter_descriptor.enumeration = combo_box_filter_enum();
    define_bindable_property({
        std::move(filter_descriptor),
        detail::EnumerationPropertyGetter<ComboBox, ComboBoxFilter>(
            *this, &ComboBox::drop_down_filter, combo_box_filter_enum()),
        detail::EnumerationPropertySetter<ComboBox, ComboBoxFilter>(*this, &ComboBox::set_drop_down_filter),
        {}, {}, {}});
}

BindingValue ComboBox::items_property_value() const {
    std::vector<BindingValue> values;
    values.reserve(items_.size());
    for (const std::string& item : items_) values.emplace_back(item);
    return BindingValue{make_property_collection(
        "String", BindingValueKind::text, std::move(values))};
}

void ComboBox::set_items_property(const BindingValue& value) {
    const PropertyCollectionValue* collection =
        std::get_if<PropertyCollectionValue>(&value);
    if (collection == nullptr || !*collection ||
        (*collection).item_kind() != BindingValueKind::text) {
        throw std::invalid_argument(
            "ComboBox.Items requires a homogeneous text collection");
    }
    std::vector<std::string> items;
    const std::span<const BindingValue> values =
        property_collection_items(*collection);
    items.reserve(values.size());
    for (const BindingValue& item : values) {
        items.push_back(std::get<std::string>(item));
    }
    set_items(std::move(items));
}

void ComboBox::reset_items_property() {
    set_items({});
}

bool ComboBox::should_serialize_items_property() const noexcept {
    return !items_.empty();
}

BindingValue ComboBox::selected_index_property_value() const {
    return BindingValue{selected_index_
        ? static_cast<std::int64_t>(*selected_index_)
        : std::int64_t{-1}};
}

void ComboBox::set_selected_index_property(const BindingValue& value) {
    const std::optional<BindingValue> converted = convert_binding_value(
        value, BindingValueKind::signed_integer);
    if (!converted) {
        throw std::invalid_argument(
            "ComboBox.SelectedIndex binding requires an integer");
    }
    const std::int64_t index = std::get<std::int64_t>(*converted);
    if (index == -1) {
        set_selected_index(std::nullopt);
        return;
    }
    if (index < 0) {
        throw std::out_of_range(
            "ComboBox.SelectedIndex binding must be -1 or non-negative");
    }
    set_selected_index(static_cast<std::size_t>(index));
}

BindingValue ComboBox::text_property_value() const {
    return BindingValue{std::string(text())};
}

void ComboBox::set_text_property(const BindingValue& value) {
    const std::optional<BindingValue> converted = convert_binding_value(
        value, BindingValueKind::text);
    if (!converted) {
        throw std::invalid_argument("ComboBox.Text binding requires text");
    }
    set_text(std::get<std::string>(*converted));
}

void ComboBox::reset_text_property() {
    if (editor_) {
        set_text(std::string{});
        return;
    }
    set_selected_index(std::nullopt);
}

bool ComboBox::should_serialize_text_property() const noexcept {
    if (editor_) return !(*editor_).text().empty();
    return selected_index_.has_value();
}

ComboBox::~ComboBox() {
    editor_text_.disconnect();
    editor_focus_.disconnect();
}

std::string_view ComboBox::text() const noexcept {
    if (editor_) return (*editor_).text();
    return selected_text();
}

void ComboBox::set_text(std::string text) {
    require_mutable();
    if (!validate_utf8(text).valid()) {
        throw std::invalid_argument("ComboBox text must be valid UTF-8");
    }
    if (editor_) {
        // The editor raises text_changed, through which the selection
        // follows the text; the list is not filtered by it.
        programmatic_text_ = true;
        (*editor_).set_text(std::move(text));
        programmatic_text_ = false;
        return;
    }
    const std::vector<std::string>::iterator found =
        std::find(items_.begin(), items_.end(), text);
    set_selected_index(found == items_.end()
        ? std::optional<std::size_t>{}
        : std::optional<std::size_t>{static_cast<std::size_t>(
              std::distance(items_.begin(), found))});
}

void ComboBox::set_drop_down_style(ComboBoxStyle style) {
    require_mutable();
    if (drop_down_style_ == style) return;
    close_drop_down();
    drop_down_style_ = style;
    if (style == ComboBoxStyle::drop_down) {
        create_editor();
    } else {
        remove_editor();
    }
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
}

void ComboBox::create_editor() {
    const bool had_focus = focused_;
    const std::string prefix(stable_id().value());
    editor_ = make_control<TextBox>(StableId(prefix + ".editor"));
    (*editor_).border_style = BorderStyle::none;
    (*editor_).set_font(font_);
    (*editor_).set_text(std::string(selected_text()));
    (*editor_).set_placeholder_text(placeholder_);
    editor_text_ = (*editor_).text_changed().subscribe(*this,
        Delegate<const std::string&>::bind<ComboBox, &ComboBox::editor_text_changed>(*this));
    editor_focus_ = (*editor_).focus_observed().subscribe(*this,
        Delegate<bool>::bind<ComboBox, &ComboBox::editor_focus_changed>(*this));
    add_child(editor_);
    configure_editor_auto_complete();
    // With an edit field the combo box is a typed-text field: its tooltip
    // shows as the field's hint on focus.
    set_tool_tip_presentation(ToolTipPresentation::field_hint);
    // The editor takes the focus for the field.
    set_focusable(false);
    Window* const host = window();
    if (had_focus && host != nullptr) static_cast<void>((*host).request_focus(editor_));
}

void ComboBox::remove_editor() {
    if (!editor_) return;
    const bool had_focus = focused_;
    editor_text_.disconnect();
    editor_focus_.disconnect();
    (*editor_).close_auto_complete();
    static_cast<void>(remove_child((*editor_).runtime_id()));
    editor_.reset();
    focused_ = false;
    set_focusable(true);
    set_tool_tip_presentation(ToolTipPresentation::bubble);
    Window* const host = window();
    if (had_focus && host != nullptr) static_cast<void>((*host).request_focus(shared_from_this()));
}

void ComboBox::configure_editor_auto_complete() {
    if (!editor_) return;
    TextBox& editor = *editor_;
    // The items reach the editor as a collection of their own, rebuilt with
    // them, so the editor's source is always custom_source or a host's.
    const bool list_items = auto_complete_source_ == AutoCompleteSource::list_items;
    editor.set_auto_complete_custom_source(list_items ? list_items_source_ : auto_complete_custom_source_);
    editor.set_auto_complete_source(list_items ? AutoCompleteSource::custom_source : auto_complete_source_);
    // A filtered list shows the matches itself; two lists would compete
    // for the same keys.
    const bool filtering = drop_down_filter_ != ComboBoxFilter::none;
    editor.set_auto_complete_mode(filtering ? AutoCompleteMode::none : auto_complete_mode_);
    editor.set_auto_complete_anchor(shared_from_this());
}

void ComboBox::set_drop_down_filter(ComboBoxFilter filter) {
    require_mutable();
    if (drop_down_filter_ == filter) return;
    close_drop_down();
    drop_down_filter_ = filter;
    configure_editor_auto_complete();
}

void ComboBox::compute_rows(std::string_view pattern) {
    rows_.clear();
    row_matches_.clear();
    filter_pattern_ = std::string(pattern);
    const bool filtering = editor_ && drop_down_filter_ != ComboBoxFilter::none && !pattern.empty();
    rows_.reserve(items_.size());
    if (!filtering) {
        for (std::size_t index = 0U; index < items_.size(); ++index) rows_.push_back(index);
        return;
    }
    const TextMatchMode mode = drop_down_filter_ == ComboBoxFilter::prefix
        ? TextMatchMode::prefix : TextMatchMode::substring;
    row_matches_.reserve(items_.size());
    for (std::size_t index = 0U; index < items_.size(); ++index) {
        const std::optional<std::size_t> found = find_text_match(items_[index], pattern, mode, false);
        if (!found) continue;
        rows_.push_back(index);
        row_matches_.push_back(TextMatchRange{.offset = *found, .length = pattern.size()});
    }
}

std::optional<std::size_t> ComboBox::row_of_item(std::optional<std::size_t> item) const noexcept {
    if (!item) return std::nullopt;
    for (std::size_t row = 0U; row < rows_.size(); ++row) {
        if (rows_[row] == *item) return row;
    }
    return std::nullopt;
}

double ComboBox::drop_down_height(std::size_t rows) const {
    const Size client = (*window()).client_size();
    const std::size_t shown = std::min(maximum_drop_down_items_, rows);
    const double height = std::min(std::max(0.0, client.height - 8.0),
        static_cast<double>(shown) * 26.0 * effective_text_scale() + 4.0);
    return height;
}

void ComboBox::show_rows() {
    if (!overlay_list_) return;
    DropDownList& list = static_cast<DropDownList&>(*overlay_list_);
    std::vector<std::string> texts;
    texts.reserve(rows_.size());
    for (const std::size_t item : rows_) texts.push_back(items_[item]);
    synchronizing_list_ = true;
    list.set_items(std::move(texts));
    if (!row_matches_.empty()) {
        // Filtered rows keep their items' identities.
        const std::string prefix = std::string(list.stable_id().value()) + ".item.";
        std::vector<std::string> identities;
        identities.reserve(rows_.size());
        for (const std::size_t item : rows_) identities.push_back(prefix + std::to_string(item));
        list.set_item_stable_ids(std::move(identities));
    }
    list.set_matches(row_matches_);
    const std::optional<std::size_t> row = row_of_item(selected_index_);
    if (row) {
        list.select_index(*row);
    } else {
        list.clear_selection();
    }
    synchronizing_list_ = false;
    list.synchronize_scroll();
    if (editor_ && overlay_layer_ && window() != nullptr) {
        AnchoredOverlayLayer& layer = static_cast<AnchoredOverlayLayer&>(*overlay_layer_);
        AnchoredOverlayPlacement placement = layer.placement();
        placement.preferred_size.height = drop_down_height(rows_.size());
        layer.set_placement(placement);
    }
}

void ComboBox::refresh_open_rows(std::string_view pattern) {
    if (!overlay_list_) return;
    compute_rows(pattern);
    if (rows_.empty()) {
        close_drop_down();
        return;
    }
    show_rows();
}

bool ComboBox::step_row(std::uint32_t key) {
    if (rows_.empty()) return false;
    const std::optional<std::size_t> row = row_of_item(selected_index_);
    if (key != PhysicalKey::down && key != PhysicalKey::up) return false;
    // From no selected row either key goes to the first row; otherwise one
    // row on, stopping at the ends.
    std::size_t next = 0U;
    if (row && key == PhysicalKey::down) next = std::min(*row + 1U, rows_.size() - 1U);
    if (row && key == PhysicalKey::up && *row > 0U) next = *row - 1U;
    set_selected_index(rows_[next]);
    return true;
}

void ComboBox::rebuild_list_items_source() {
    AutoCompleteStringCollection& source = *list_items_source_;
    source.clear();
    std::vector<std::string> copy(items_.begin(), items_.end());
    source.add_range(std::move(copy));
}

void ComboBox::set_auto_complete_mode(AutoCompleteMode mode) {
    require_mutable();
    if (auto_complete_mode_ == mode) return;
    auto_complete_mode_ = mode;
    configure_editor_auto_complete();
}

void ComboBox::set_auto_complete_source(AutoCompleteSource source) {
    require_mutable();
    if (auto_complete_source_ == source) return;
    auto_complete_source_ = source;
    configure_editor_auto_complete();
}

void ComboBox::set_auto_complete_custom_source(std::shared_ptr<AutoCompleteStringCollection> source) {
    require_mutable();
    if (!source) {
        throw std::invalid_argument("ComboBox auto-complete custom source may not be null");
    }
    if (auto_complete_custom_source_ == source) return;
    auto_complete_custom_source_ = std::move(source);
    configure_editor_auto_complete();
}

void ComboBox::editor_text_changed(const std::string& text) {
    publish_change(text_changed_, text);
    if (!is_alive() || synchronizing_text_) return;
    match_editor_text();
    if (!is_alive() || programmatic_text_ || drop_down_filter_ == ComboBoxFilter::none) return;
    // Typing searches: the list opens on the matching items, narrows and
    // widens with the text, and closes when nothing matches.
    if (dropped_down_) {
        refresh_open_rows(text);
    } else {
        open_drop_down(text);
    }
}

void ComboBox::editor_focus_changed(bool focused) {
    focused_ = focused;
    // The drop_down style's list holds no focus: it closes with the field's.
    if (!focused && dropped_down_) close_drop_down();
    invalidate(Dirty::paint | Dirty::semantics);
}

void ComboBox::match_editor_text() {
    if (!editor_) return;
    const std::string_view typed = (*editor_).text();
    std::optional<std::size_t> found{};
    for (std::size_t index = 0U; index < items_.size(); ++index) {
        // Equal ignoring ASCII case: as long as the text, and a prefix match.
        const bool same_length = items_[index].size() == typed.size();
        if (same_length && find_text_match(items_[index], typed, TextMatchMode::prefix, false)) {
            found = index;
            break;
        }
    }
    if (selected_index_ == found) return;
    selected_index_ = found;
    if (overlay_list_) {
        // Moving the list's row must not show the item's text over the
        // text the user is typing.
        synchronizing_list_ = true;
        const std::optional<std::size_t> row = row_of_item(found);
        if (row) {
            (*overlay_list_).select_index(*row);
        } else {
            (*overlay_list_).clear_selection();
        }
        synchronizing_list_ = false;
    }
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(selected_index_changed_, selected_index_);
}

void ComboBox::overlay_selection_changed(const ListSelectionChange& change) {
    if (synchronizing_list_ || !change.active_index || *change.active_index >= rows_.size()) return;
    set_selected_index(rows_[*change.active_index]);
}

void ComboBox::on_anchored_dismissal(OverlayDismissReason) {
    close_drop_down();
}

bool ComboBox::step_selection(std::uint32_t key) {
    if (items_.empty()) return false;
    std::size_t next = selected_index_.value_or(0U);
    if (key == PhysicalKey::up) {
        next = next == 0U ? 0U : next - 1U;
    } else if (key == PhysicalKey::down) {
        next = std::min(next + 1U, items_.size() - 1U);
    } else if (key == PhysicalKey::home) {
        next = 0U;
    } else if (key == PhysicalKey::end) {
        next = items_.size() - 1U;
    } else {
        return false;
    }
    set_selected_index(next);
    return true;
}

void ComboBox::set_items(std::vector<std::string> items) {
    require_mutable();
    for (const std::string& item : items) {
        if (!validate_utf8(item).valid()) {
            throw std::invalid_argument("ComboBox items must be valid UTF-8");
        }
    }
    if (items_ == items) return;
    items_ = std::move(items);
    rebuild_list_items_source();
    if (editor_) {
        // The selection follows the text; an open list keeps its filter.
        // The pattern is a copy: compute_rows replaces filter_pattern_.
        const std::string pattern = filter_pattern_;
        refresh_open_rows(pattern);
        if (!is_alive()) return;
        match_editor_text();
        if (!is_alive()) return;
        invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
        publish_change(items_changed_);
        return;
    }
    if (selected_index_ && *selected_index_ >= items_.size()) {
        selected_index_.reset();
        publish_change(selected_index_changed_, selected_index_);
        if (!is_alive()) return;
        publish_change(text_changed_, std::string{});
        if (!is_alive()) return;
    }
    refresh_open_rows({});
    if (!is_alive()) return;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    publish_change(items_changed_);
}

void ComboBox::add_item(std::string item) {
    require_mutable();
    if (!validate_utf8(item).valid()) {
        throw std::invalid_argument("ComboBox item must be valid UTF-8");
    }
    items_.push_back(std::move(item));
    rebuild_list_items_source();
    // A copy: compute_rows replaces filter_pattern_.
    const std::string pattern = filter_pattern_;
    refresh_open_rows(pattern);
    if (!is_alive()) return;
    if (editor_ && !selected_index_) match_editor_text();
    if (!is_alive()) return;
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
    publish_change(items_changed_);
}

void ComboBox::set_selected_index(std::optional<std::size_t> index) {
    require_mutable();
    if (index && *index >= items_.size()) {
        throw std::out_of_range("ComboBox selection index is outside the collection");
    }
    if (editor_ && index) {
        // The editor shows the chosen item; the selection is set below
        // rather than matched from the text.
        synchronizing_text_ = true;
        (*editor_).set_text(items_[*index]);
        synchronizing_text_ = false;
        if (!is_alive()) return;
    }
    if (selected_index_ == index) return;
    selected_index_ = index;
    if (overlay_list_) {
        synchronizing_list_ = true;
        const std::optional<std::size_t> row = row_of_item(index);
        if (row) (*overlay_list_).select_index(*row);
        else (*overlay_list_).clear_selection();
        synchronizing_list_ = false;
    }
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(selected_index_changed_, selected_index_);
    if (editor_ || !is_alive()) return;
    publish_change(text_changed_, std::string(selected_text()));
}

std::string_view ComboBox::selected_text() const noexcept {
    return selected_index_ ? std::string_view(items_[*selected_index_])
                           : std::string_view{};
}

void ComboBox::set_placeholder_text(std::string text) {
    require_mutable();
    if (!validate_utf8(text).valid()) {
        throw std::invalid_argument("ComboBox placeholder must be valid UTF-8");
    }
    if (placeholder_ == text) return;
    placeholder_ = std::move(text);
    // The edit field shows the placeholder itself, so it wins over the
    // combo box's field hint there too.
    if (editor_) (*editor_).set_placeholder_text(placeholder_);
    invalidate(Dirty::paint | Dirty::semantics);
}

void ComboBox::set_maximum_drop_down_items(std::size_t count) {
    require_mutable();
    if (count == 0U || count > 64U) {
        throw std::invalid_argument("ComboBox drop-down row count must be between 1 and 64");
    }
    if (maximum_drop_down_items_ == count) return;
    maximum_drop_down_items_ = count;
    if (dropped_down_) {
        const std::string pattern = filter_pattern_;
        close_drop_down();
        open_drop_down(pattern);
    }
    invalidate(Dirty::semantics);
}

void ComboBox::set_drop_down_width(double width) {
    require_mutable();
    if (!std::isfinite(width) || width < 0.0 || width > 4096.0) {
        throw std::out_of_range(
            "ComboBox drop-down width must be finite and between 0 and 4096");
    }
    if (drop_down_width_ == width) return;
    drop_down_width_ = width;
    invalidate(Dirty::paint | Dirty::semantics);
}

void ComboBox::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) {
        throw std::invalid_argument("ComboBox font specification is invalid");
    }
    set_font_from_code(true);
    if (font_ == font) return;
    font_ = font;
    if (editor_) (*editor_).set_font(font);
    if (overlay_list_) (*overlay_list_).set_font(font);
    invalidate(Dirty::measure | Dirty::paint | Dirty::semantics);
}

void ComboBox::set_dropped_down(bool dropped_down) {
    require_mutable();
    if (dropped_down == dropped_down_) return;
    // Opened by the button, a key or the program, the list shows every item.
    if (dropped_down) open_drop_down({});
    else close_drop_down();
}

void ComboBox::open_drop_down(std::string_view pattern) {
    if (dropped_down_ || !attached() || window() == nullptr || items_.empty()) return;
    compute_rows(pattern);
    if (rows_.empty()) return;
    const Control::Ptr owner = (*window()).root();
    if (!owner) return;
    const Rect combo = absolute_bounds();
    const Size client = (*window()).client_size();
    const double overlay_height = drop_down_height(rows_.size());
    const double requested_width = drop_down_width_ > 0.0
        ? drop_down_width_ : combo.width;
    const double overlay_width = std::min(requested_width, client.width);
    const double overlay_x = std::clamp(combo.x, 0.0,
        std::max(0.0, client.width - overlay_width));
    const double overlay_y = combo.y + combo.height + overlay_height <= client.height
        ? combo.y + combo.height : std::max(0.0, combo.y - overlay_height);

    if (editor_) {
        open_field_list(requested_width, overlay_height);
        return;
    }
    const std::string prefix(stable_id().value());
    const std::weak_ptr<ComboBox> weak =
        std::static_pointer_cast<ComboBox>(shared_from_this());
    std::shared_ptr<gui_forms::DropDownLayer> layer = make_control<DropDownLayer>(StableId(prefix + ".overlay.layer"));
    (*layer).set_requested_bounds({0.0, 0.0, client.width, client.height});
    std::shared_ptr<gui_forms::ListBox> list = make_control<DropDownList>(StableId(prefix + ".overlay.list"));
    (*list).set_paint_plane(PaintPlane::overlay);
    (*list).set_items(items_);
    (*list).set_font(font_);
    (*list).set_requested_bounds(
        {overlay_x, overlay_y, overlay_width, overlay_height});
    if (selected_index_) {
        (*list).select_index(*selected_index_);
    }
    (*layer).add_child(list);
    OverlayToken overlay_token = (*window()).open_overlay(shared_from_this(), layer);

    overlay_layer_ = layer;
    overlay_list_ = list;
    overlay_token_ = std::move(overlay_token);
    overlay_selection_ = (*list).selection_changed().subscribe(
        *this, detail::WeakMemberCallback<
            void (ComboBox::*)(const ListSelectionChange&)>(
                weak, &ComboBox::overlay_selection_changed));
    overlay_activation_ = (*list).item_activated().subscribe(
        *this, detail::WeakMemberCallback<
            void (ComboBox::*)(std::size_t)>(
                weak, &ComboBox::commit_overlay_selection));
    overlay_dismissal_ = (*layer).dismissed().subscribe(
        *this, detail::WeakMemberCallback<void (ComboBox::*)()>(
            weak, &ComboBox::close_drop_down));
    if (Event<>* closed = overlay_token_.closed_event()) {
        overlay_revocation_ = (*closed).subscribe(
            *this, detail::WeakMemberCallback<void (ComboBox::*)()>(
                weak, &ComboBox::on_overlay_revoked));
    }
    overlay_scope_ = (*window()).begin_focus_scope(layer, list).value;
    dropped_down_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(drop_down_changed_, true);
}

// The drop_down style: the list opens under the field without a focus
// scope, so typing goes on in the editor, which drives the list through
// on_key_preview. The anchored layer passes presses on the field through to
// it. The rows are already computed.
void ComboBox::open_field_list(double width, double height) {
    const std::string prefix(stable_id().value());
    const std::weak_ptr<ComboBox> weak =
        std::static_pointer_cast<ComboBox>(shared_from_this());
    (*editor_).close_auto_complete();
    AnchoredOverlayPlacement placement{};
    placement.preferred_size = Size{width, height};
    std::shared_ptr<AnchoredOverlayLayer> anchored =
        make_control<AnchoredOverlayLayer>(StableId(prefix + ".overlay.layer"), shared_from_this(), placement);
    std::shared_ptr<gui_forms::ListBox> field_list = make_control<DropDownList>(StableId(prefix + ".overlay.list"));
    (*field_list).set_focusable(false);
    (*field_list).set_paint_plane(PaintPlane::overlay);
    (*field_list).set_font(font_);
    (*anchored).set_content(field_list);
    OverlayToken field_token = (*window()).open_overlay(shared_from_this(), anchored);
    overlay_layer_ = anchored;
    overlay_list_ = field_list;
    overlay_token_ = std::move(field_token);
    show_rows();
    overlay_selection_ = (*field_list).selection_changed().subscribe(
        *this, detail::WeakMemberCallback<
            void (ComboBox::*)(const ListSelectionChange&)>(
                weak, &ComboBox::overlay_selection_changed));
    overlay_activation_ = (*field_list).item_activated().subscribe(
        *this, detail::WeakMemberCallback<
            void (ComboBox::*)(std::size_t)>(
                weak, &ComboBox::commit_overlay_selection));
    overlay_dismissal_ = (*anchored).dismiss_requested().subscribe(
        *this, detail::WeakMemberCallback<void (ComboBox::*)(OverlayDismissReason)>(
            weak, &ComboBox::on_anchored_dismissal));
    if (Event<>* closed = overlay_token_.closed_event()) {
        overlay_revocation_ = (*closed).subscribe(
            *this, detail::WeakMemberCallback<void (ComboBox::*)()>(
                weak, &ComboBox::on_overlay_revoked));
    }
    dropped_down_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    publish_change(drop_down_changed_, true);
}

void ComboBox::clear_rows() noexcept {
    rows_.clear();
    row_matches_.clear();
    filter_pattern_.clear();
}

void ComboBox::close_drop_down() {
    if (!dropped_down_ && !overlay_layer_) return;
    closing_overlay_ = true;
    overlay_selection_.disconnect();
    overlay_activation_.disconnect();
    overlay_dismissal_.disconnect();
    if (window() != nullptr && overlay_scope_ != 0U) {
        static_cast<void>((*window()).end_focus_scope(FocusScopeId{overlay_scope_}));
    }
    overlay_scope_ = 0U;
    overlay_token_.disconnect();
    overlay_revocation_.disconnect();
    overlay_list_.reset();
    overlay_layer_.reset();
    const bool changed = dropped_down_;
    dropped_down_ = false;
    closing_overlay_ = false;
    clear_rows();
    invalidate(Dirty::paint | Dirty::semantics);
    if (changed) publish_change(drop_down_changed_, false);
}

void ComboBox::on_overlay_revoked() {
    if (closing_overlay_) return;
    overlay_selection_.disconnect();
    overlay_activation_.disconnect();
    overlay_dismissal_.disconnect();
    overlay_revocation_.disconnect();
    if (window() != nullptr && overlay_scope_ != 0U) {
        static_cast<void>((*window()).end_focus_scope(FocusScopeId{overlay_scope_},
                                                     FocusScopeCloseReason::owner_unavailable));
    }
    overlay_scope_ = 0U;
    overlay_list_.reset();
    overlay_layer_.reset();
    const bool changed = dropped_down_;
    dropped_down_ = false;
    clear_rows();
    invalidate(Dirty::paint | Dirty::semantics);
    if (changed) publish_change(drop_down_changed_, false);
}

void ComboBox::commit_overlay_selection(std::size_t row) {
    if (row >= rows_.size()) return;
    set_selected_index(rows_[row]);
    close_drop_down();
}

// The well, the drop-down button, its divider and arrow, and the focus ring
// are parts the theme's engine draws, in the control's anatomy states (hot
// under the pointer, open while the list is down). A background or style
// override supplies the well's ground, and a style override the text and
// the arrow's ink, as authored colours (T4-1); a disabled combo box keeps
// the theme's disabled text. The ring shows while the field has focus or
// its list is open.
void ComboBox::on_paint(Painter& painter, Rect) {
    const Rect bounds = local_bounds();
    const AnatomyStateSet states = anatomy_states();
    std::optional<Color> ground{};
    std::optional<Color> ink{};
    if (has_background_override() || has_style_override()) ground = background();
    if (has_style_override()) ink = style().text;
    PartPaint well;
    well.family = ControlVisualRole::editor;
    well.role = PartRole::well;
    well.bounds = bounds;
    well.states = states;
    well.authored_background = ground;
    well.key = PartKey{"well", {}, {}};
    paint_part(painter, *this, well);
    const double button_width = std::min(24.0, bounds.width);
    PartPaint drop_down;
    drop_down.family = ControlVisualRole::choice;
    drop_down.role = PartRole::actuator;
    drop_down.bounds = {std::max(0.0, bounds.width - button_width), 1.0,
                        std::max(0.0, button_width - 1.0), std::max(0.0, bounds.height - 2.0)};
    drop_down.states = states;
    drop_down.key = PartKey{"drop_down", {}, {}};
    paint_part(painter, *this, drop_down);
    PartPaint divider;
    divider.family = ControlVisualRole::choice;
    divider.role = PartRole::divider;
    divider.bounds = {bounds.width - button_width - 0.5, 1.0, 1.0, std::max(0.0, bounds.height - 2.0)};
    divider.states = states;
    divider.key = PartKey{"divider", {}, {}};
    paint_part(painter, *this, divider);
    // The 7 x 4 arrow, which flips and moves when the list is open. The
    // arrow mark draws half its square's side in rows, so the square is 8
    // units, placed to put the rows where the published disclosure is.
    const double center_x = bounds.width - button_width * 0.5;
    const double center_y = bounds.height * 0.5 + (dropped_down_ ? 2.0 : -1.0);
    const double square_top = dropped_down_ ? std::floor(center_y) - 3.0 : std::floor(center_y) - 4.0;
    PartPaint disclosure;
    disclosure.family = ControlVisualRole::choice;
    disclosure.role = PartRole::disclosure;
    disclosure.mark = dropped_down_ ? PartMark::arrow_up : PartMark::arrow_down;
    disclosure.bounds = {std::floor(center_x) - 4.0, square_top, 8.0, 8.0};
    disclosure.states = states;
    disclosure.authored_foreground = ink;
    disclosure.key = PartKey{"disclosure", {}, {}};
    paint_part(painter, *this, disclosure);
    // The drop_down style's editor shows its own text; otherwise the field
    // shows the selected item, or the placeholder without one.
    std::string_view text{};
    if (!editor_) text = selected_index_ ? selected_text() : std::string_view(placeholder_);
    const ControlVisualContext context = part_visual_context(*this, states);
    const ControlVisualRecipe& editor_recipe = effective_theme().resolve(ControlVisualRole::editor, context);
    Color text_ink = selected_index_ ? editor_recipe.text : editor_recipe.muted_text;
    if (ink && effectively_enabled() && selected_index_) text_ink = *ink;
    // The content part centres its line box on its bounds, so the bounds are
    // that box on the field's baseline, ending left of the button.
    const FontSpec font = text_font(font_);
    const Size measured = painter.measure_text_utf8(text, font);
    const double box_height = std::max(measured.height, font.size * 1.2);
    const double box_ascent = std::max(font.size, box_height * 0.82);
    const double baseline = std::max(font.size, (bounds.height + font.size) * 0.5 - 1.0);
    PartPaint content;
    content.family = ControlVisualRole::editor;
    content.role = PartRole::content;
    content.states = states;
    content.text = text;
    content.font = font;
    content.bounds = {7.0, baseline - box_ascent, std::max(0.0, bounds.width - button_width - 10.0), box_height};
    content.authored_foreground = text_ink;
    content.key = PartKey{"content", {}, {}};
    paint_part(painter, *this, content);
    if (focused_ || dropped_down_) {
        PartPaint ring;
        ring.family = ControlVisualRole::editor;
        ring.role = PartRole::focus;
        ring.bounds = bounds;
        ring.states = states;
        ring.states.insert(AnatomyState::focus_visible);
        paint_part(painter, *this, ring);
    }
}

void ComboBox::arrange(Rect final_bounds) {
    if (!editor_) {
        Panel::arrange(final_bounds);
        return;
    }
    arrange_self(final_bounds);
    // The editor fills the field left of the drop-down button, inside the
    // focus ring.
    const double button_width = std::min(24.0, final_bounds.width);
    const Rect field{2.0, 2.0, std::max(0.0, final_bounds.width - button_width - 2.0),
                     std::max(0.0, final_bounds.height - 4.0)};
    set_child_layout(editor_, field);
}

void ComboBox::on_pointer(PointerEvent& event) {
    if (!eligible_for_input()) return;
    if (event.action == PointerAction::down && event.button == PointerButton::primary) {
        Window* const host = window();
        if (editor_ && host != nullptr) static_cast<void>((*host).request_focus(editor_));
        set_dropped_down(!dropped_down_);
        event.handled = true;
    } else if (event.action == PointerAction::up &&
               event.button == PointerButton::primary) {
        event.handled = true;
    }
}

void ComboBox::on_key(KeyEvent& event) {
    if (!focused_ || !enabled.read() || event.action != KeyAction::down) return;
    if (event.physical_key == PhysicalKey::escape && dropped_down_) {
        close_drop_down();
        event.handled = true;
        return;
    }
    if (event.physical_key == PhysicalKey::f4 ||
        (event.physical_key == PhysicalKey::down && includes(event.modifiers, Modifier::alt)) ||
        event.physical_key == PhysicalKey::space) {
        set_dropped_down(!dropped_down_);
        event.handled = true;
        return;
    }
    if (!dropped_down_ && step_selection(event.physical_key)) {
        event.handled = true;
    }
}

void ComboBox::on_key_preview(KeyEvent& event) {
    // The drop_down style: the editor has the focus, and this sees its keys
    // first. Keys the editor's own suggestion list needs are left to it.
    if (!editor_ || !enabled.read() || event.action != KeyAction::down) return;
    const std::uint32_t key = event.physical_key;
    const bool alt = includes(event.modifiers, Modifier::alt);
    if (key == PhysicalKey::f4 || (key == PhysicalKey::down && alt)) {
        // Opening the list closes the editor's suggestions.
        set_dropped_down(!dropped_down_);
        event.handled = true;
        return;
    }
    if ((*editor_).auto_complete_open()) return;
    if (event.modifiers != Modifier::none) return;
    if (key == PhysicalKey::up || key == PhysicalKey::down) {
        // Up and Down choose the previous or next item: of the open list's
        // rows, or of all the items while it is closed.
        if (dropped_down_) {
            static_cast<void>(step_row(key));
        } else {
            static_cast<void>(step_selection(key));
        }
        event.handled = true;
        return;
    }
    if (dropped_down_ && (key == PhysicalKey::enter || key == PhysicalKey::escape)) {
        close_drop_down();
        event.handled = true;
    }
}

void ComboBox::on_focus_changed(bool focused) {
    // In the drop_down style the editor's focus is the field's
    // (editor_focus_changed); this control is not focusable then.
    if (editor_) return;
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

SemanticDescriptor ComboBox::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::combo_box;
    descriptor.name = accessible_name.read();
    descriptor.value = std::string(text());
    descriptor.description = accessible_description.read().empty()
        ? placeholder_ : accessible_description.read();
    if (dropped_down_) descriptor.states |= SemanticState::expanded;
    descriptor.actions = {SemanticAction::focus,
        dropped_down_ ? SemanticAction::collapse : SemanticAction::expand};
    if (editor_) {
        // The editor is part of the field, published through it, as
        // DomainUpDown's is.
        descriptor.actions.push_back(SemanticAction::set_value);
        descriptor.include_descendants = false;
    }
    descriptor.exposed = true;
    return descriptor;
}

bool ComboBox::on_semantic_action(SemanticAction action, std::string_view value) {
    if (action == SemanticAction::expand) {
        set_dropped_down(true);
        return true;
    }
    if (action == SemanticAction::collapse) {
        set_dropped_down(false);
        return true;
    }
    if (editor_ && action == SemanticAction::set_value) {
        if (!validate_utf8(value).valid()) return false;
        set_text(std::string(value));
        return true;
    }
    Window* const host = window();
    if (editor_ && action == SemanticAction::focus && host != nullptr) {
        const bool focused = (*host).request_focus(editor_);
        return focused;
    }
    return Panel::on_semantic_action(action, value);
}

void ComboBox::on_detached_from_window() noexcept {
    overlay_selection_.disconnect();
    overlay_activation_.disconnect();
    overlay_dismissal_.disconnect();
    overlay_revocation_.disconnect();
    overlay_scope_ = 0U;
    overlay_token_.disconnect();
    overlay_list_.reset();
    overlay_layer_.reset();
    dropped_down_ = false;
    focused_ = false;
    clear_rows();
}


namespace {

constexpr std::array<PartField<ComboBox::Parts>, 7> combo_box_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &ComboBox::Parts::face},
    {.declaration = {.name = "edge", .role = PartRole::edge, .composed_from = {}},
     .member = &ComboBox::Parts::edge},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &ComboBox::Parts::well},
    {.declaration = {.name = "content", .role = PartRole::content, .composed_from = {}},
     .member = &ComboBox::Parts::content},
    {.declaration = {.name = "divider", .role = PartRole::divider, .composed_from = {}},
     .member = &ComboBox::Parts::divider},
    {.declaration = {.name = "drop_down", .role = PartRole::actuator, .composed_from = "button"},
     .member = &ComboBox::Parts::drop_down},
    {.declaration = {.name = "disclosure", .role = PartRole::disclosure, .composed_from = {}},
     .member = &ComboBox::Parts::disclosure},
}};

constexpr std::array<StateField<ComboBox::States>, 10> combo_box_state_fields{{
    {.state = AnatomyState::normal, .member = &ComboBox::States::normal},
    {.state = AnatomyState::hot, .member = &ComboBox::States::hot},
    {.state = AnatomyState::pressed, .member = &ComboBox::States::pressed},
    {.state = AnatomyState::captured, .member = &ComboBox::States::captured},
    {.state = AnatomyState::enabled, .member = &ComboBox::States::enabled},
    {.state = AnatomyState::disabled, .member = &ComboBox::States::disabled},
    {.state = AnatomyState::focused, .member = &ComboBox::States::focused},
    {.state = AnatomyState::focus_visible, .member = &ComboBox::States::focus_visible},
    {.state = AnatomyState::open, .member = &ComboBox::States::open},
    {.state = AnatomyState::closed, .member = &ComboBox::States::closed},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 7>
    combo_box_parts = joined_declarations(scrollable_detail::parts,
                                         part_declarations(combo_box_extra_part_fields));
constexpr std::array<AnatomyState, 10> combo_box_states =
    state_declarations(combo_box_state_fields);

constexpr ControlAnatomy combo_box_anatomy{
    .control_id = "combo_box",
    .parts = combo_box_parts,
    .states = combo_box_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& ComboBox::anatomy() const noexcept {
    return combo_box_anatomy;
}

ComboBox::Parts ComboBox::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), combo_box_extra_part_fields, published.parts(), record);
    return record;
}

ComboBox::States ComboBox::states() const {
    States record;
    read_states(combo_box_state_fields, anatomy_states(), record);
    return record;
}

void ComboBox::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect bounds = local_bounds();
    const bool themed = !has_background_override() && !has_style_override();
    const Panel::Parts panel = panel_parts();
    Parts record;
    record.scrollable = panel.scrollable;
    if (!themed) {
        record.face = panel.face;
        record.edge = panel.edge;
    }
    record.well = PartGeometry::from_bounds(bounds);
    const double button_width = std::min(24.0, bounds.width);
    const Rect button{std::max(0.0, bounds.width - button_width), 1.0,
                      std::max(0.0, button_width - 1.0),
                      std::max(0.0, bounds.height - 2.0)};
    const Rect content{5.0, 2.0, std::max(0.0, bounds.width - button_width - 8.0),
                       std::max(0.0, bounds.height - 4.0)};
    // The one-unit divider stroked on the button's left edge.
    const Rect divider{bounds.width - button_width - 0.5, 1.0, 1.0,
                       std::max(0.0, bounds.height - 2.0)};
    // The 7 x 4 arrow, which flips and moves when the list is open.
    const double center_x = bounds.width - button_width * 0.5;
    const double center_y = bounds.height * 0.5 + (dropped_down_ ? 2.0 : -1.0);
    const double arrow_top = dropped_down_ ? std::floor(center_y) - 2.0
                                           : std::floor(center_y) - 1.0;
    const Rect arrow{std::floor(center_x) - 3.0, arrow_top, 7.0, 4.0};
    record.content = PartGeometry::from_bounds(content);
    record.divider = PartGeometry::from_bounds(divider);
    record.drop_down = PartGeometry::from_bounds(button);
    record.disclosure = PartGeometry::from_bounds(arrow);
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), combo_box_extra_part_fields, record, geometry);
}

AnatomyStateSet ComboBox::anatomy_states() const {
    AnatomyStateSet states = ScrollableControl::anatomy_states();
    if (dropped_down_) {
        states.insert(AnatomyState::open);
    } else {
        states.insert(AnatomyState::closed);
    }
    return states;
}

namespace {

// The events ComboBox adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 4> combo_box_interactions{{
    {.name = "selected_index_changed", .payload = "std::optional<std::size_t>", .mutable_payload = false},
    {.name = "drop_down_changed", .payload = "bool", .mutable_payload = false},
    {.name = "items_changed", .payload = "", .mutable_payload = false},
    {.name = "text_changed", .payload = "std::string", .mutable_payload = false},
}};

} // namespace

void ComboBox::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), combo_box_interactions.begin(), combo_box_interactions.end());
}

} // namespace gui_forms
