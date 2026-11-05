#include "gui_forms/controls/panel/property_list/property_list.hpp"

#include "property_list_utilities.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace gui_forms {

using detail::property_editor_height;
using detail::property_gap;
using detail::property_group_height;
using detail::property_padding;
using detail::property_row_height;
using detail::property_validation_height;
using detail::require_property_text;

struct PropertyList::Impl final {
    struct RowState final {
        std::size_t group_index{};
        std::size_t row_index{};
        Control::Ptr editor;
        std::shared_ptr<Button> reset_button;
        SubscriptionToken value_subscription;
        SubscriptionToken commit_subscription;
        SubscriptionToken cancel_subscription;
        SubscriptionToken focus_subscription;
        SubscriptionToken reset_subscription;
        SubscriptionToken reset_focus_subscription;
        std::string committed_value;
        Rect row_bounds{};
        Rect name_bounds{};
        Rect value_bounds{};
        Rect reset_bounds{};
        Rect validation_bounds{};
    };
    using RowList = std::vector<RowState>;
    using GroupList = std::vector<PropertyGroupSpec>;

    struct TextValueChanged final {
        Impl* implementation{};
        std::string row_id;
        void operator()(const std::string& value) const {
            if ((*implementation).synchronizing) return;
            RowState* row = (*implementation).find_row(row_id);
            if (row == nullptr) return;
            PropertyRowSpec& model = (*implementation).spec(*row);
            const std::string previous = model.value;
            model.value = value;
            (*implementation).owner.publish_change(
                (*implementation).owner.value_changed_,
                PropertyValueChange{row_id, previous, model.value, false});
        }
    };

    struct TextCommitted final {
        Impl* implementation{};
        std::string row_id;
        void operator()(const std::string& value) const {
            RowState* row = (*implementation).find_row(row_id);
            if (row == nullptr) return;
            PropertyRowSpec& model = (*implementation).spec(*row);
            const std::string previous = (*row).committed_value;
            (*row).committed_value = value;
            model.value = value;
            if (model.required && value.empty()) {
                model.validation_message = model.name + " is required";
            }
            (*implementation).recompute_geometry();
            (*implementation).owner.value_committed_.emit(
                {row_id, previous, value, true});
            (*implementation).owner.invalidate(
                Dirty::measure | Dirty::layout | Dirty::paint |
                Dirty::semantics);
        }
    };

    struct TextCancelled final {
        Impl* implementation{};
        std::string row_id;
        void operator()() const {
            RowState* row = (*implementation).find_row(row_id);
            if (row == nullptr) return;
            (*implementation).synchronizing = true;
            const std::shared_ptr<TextBox> editor =
                std::dynamic_pointer_cast<TextBox>((*row).editor);
            if (editor) (*editor).set_text((*row).committed_value);
            (*implementation).spec(*row).value = (*row).committed_value;
            (*implementation).synchronizing = false;
            (*implementation).owner.invalidate(
                Dirty::paint | Dirty::semantics);
        }
    };

    struct ChoiceChanged final {
        Impl* implementation{};
        std::string row_id;
        std::weak_ptr<ComboBox> choice;
        void operator()(std::optional<std::size_t>) const {
            if ((*implementation).synchronizing) return;
            RowState* row = (*implementation).find_row(row_id);
            const std::shared_ptr<ComboBox> editor = choice.lock();
            if (row == nullptr || !editor) return;
            PropertyRowSpec& model = (*implementation).spec(*row);
            const std::string previous = (*row).committed_value;
            model.value = std::string((*editor).selected_text());
            (*row).committed_value = model.value;
            const PropertyValueChange change{
                row_id, previous, model.value, true};
            (*implementation).owner.publish_change(
                (*implementation).owner.value_changed_, change);
            (*implementation).owner.value_committed_.emit(change);
        }
    };

    struct CheckChanged final {
        Impl* implementation{};
        std::string row_id;
        std::weak_ptr<CheckBox> check;
        void operator()(bool checked) const {
            if ((*implementation).synchronizing) return;
            RowState* row = (*implementation).find_row(row_id);
            const std::shared_ptr<CheckBox> editor = check.lock();
            if (row == nullptr || !editor) return;
            PropertyRowSpec& model = (*implementation).spec(*row);
            const std::string previous = (*row).committed_value;
            model.value = checked ? "True" : "False";
            (*row).committed_value = model.value;
            (*editor).text = model.value;
            const PropertyValueChange change{
                row_id, previous, model.value, true};
            (*implementation).owner.publish_change(
                (*implementation).owner.value_changed_, change);
            (*implementation).owner.value_committed_.emit(change);
        }
    };

    struct EnsureVisible final {
        Impl* implementation{};
        std::string row_id;
        void operator()(bool focused) const {
            if (focused) (*implementation).ensure_visible(row_id);
        }
    };

    struct ResetClicked final {
        Impl* implementation{};
        std::string row_id;
        void operator()(ButtonBase&) const {
            (*implementation).owner.reset_requested_.emit({row_id});
        }
    };

    explicit Impl(PropertyList& public_owner) : owner(public_owner) {}

    [[nodiscard]] double scale() const noexcept {
        return owner.effective_text_scale();
    }

    [[nodiscard]] double line_height() const {
        return owner.resolve_text_layout_utf8(
            "Mg", owner.effective_font(font)).logical_size.height / scale();
    }

    [[nodiscard]] double group_extent() const {
        return std::max(property_group_height, line_height() + 8.0) * scale();
    }

    void apply_editor_font(const Control::Ptr& editor) const {
        if (!font_overridden || !editor) return;
        if (const std::shared_ptr<TextBox> text =
                std::dynamic_pointer_cast<TextBox>(editor)) {
            (*text).set_font(font);
        } else if (const std::shared_ptr<NumericUpDown> numeric =
                       std::dynamic_pointer_cast<NumericUpDown>(editor)) {
            const std::shared_ptr<TextBox> text = (*numeric).editor();
            if (text) (*text).set_font(font);
        } else if (const std::shared_ptr<ComboBox> choice =
                       std::dynamic_pointer_cast<ComboBox>(editor)) {
            (*choice).set_font(font);
        } else if (const std::shared_ptr<ButtonBase> button =
                       std::dynamic_pointer_cast<ButtonBase>(editor)) {
            (*button).font = font;
        }
    }

    PropertyRowSpec& spec(RowState& state) {
        return groups[state.group_index].rows[state.row_index];
    }
    const PropertyRowSpec& spec(const RowState& state) const {
        return groups[state.group_index].rows[state.row_index];
    }

    RowState* find_row(std::string_view id) noexcept {
        for (RowState& row : rows) {
            if (spec(row).stable_id == id) return &row;
        }
        return nullptr;
    }
    const RowState* find_row(std::string_view id) const noexcept {
        for (const RowState& row : rows) {
            if (spec(row).stable_id == id) return &row;
        }
        return nullptr;
    }

    [[nodiscard]] bool row_visible(const RowState& state) const noexcept {
        const PropertyRowSpec* current = &spec(state);
        std::size_t remaining = groups[state.group_index].rows.size();
        while (!(*current).parent_id.empty() && remaining-- > 0U) {
            const RowState* parent = find_row((*current).parent_id);
            if (!parent || (*parent).group_index != state.group_index ||
                !spec(*parent).expandable || !spec(*parent).expanded) {
                return false;
            }
            current = &spec(*parent);
        }
        return (*current).parent_id.empty();
    }

    void clear_editors() noexcept {
        for (RowState& row : rows) {
            if (row.editor && (*row.editor).parent().get() == &owner) {
                Control::Ptr removed = owner.remove_child((*row.editor).runtime_id());
                if (removed && (*removed).is_alive()) (*removed).dispose();
            }
            if (row.reset_button && (*row.reset_button).parent().get() == &owner) {
                Control::Ptr removed = owner.remove_child(
                    (*row.reset_button).runtime_id());
                if (removed && (*removed).is_alive()) (*removed).dispose();
            }
        }
        rows.clear();
    }

    void rebuild_editors() {
        clear_editors();
        for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
            for (std::size_t row_index = 0;
                 row_index < groups[group_index].rows.size(); ++row_index) {
                PropertyRowSpec& row = groups[group_index].rows[row_index];
                RowState state;
                state.group_index = group_index;
                state.row_index = row_index;
                state.committed_value = row.value;
                // Read-only and custom rows have no built-in editor.
                if (row.editor == PropertyEditorKind::text) {
                    std::shared_ptr<gui_forms::TextBox> editor = make_control<TextBox>(
                        StableId(row.stable_id + ".editor"), row.value);
                    (*editor).set_font({FontRole::content, 10.0, 400, false});
                    (*editor).accessible_name = row.name;
                    (*editor).accessible_description = row.description;
                    (*editor).enabled = row.enabled;
                    state.editor = editor;
                    owner.add_child(editor);
                } else if (row.editor == PropertyEditorKind::choice) {
                    std::shared_ptr<gui_forms::ComboBox> editor = make_control<ComboBox>(
                        StableId(row.stable_id + ".editor"));
                    (*editor).set_items(row.choices);
                    const std::vector<std::string>::iterator selected =
                        std::find(row.choices.begin(),
                                                    row.choices.end(), row.value);
                    if (selected != row.choices.end()) {
                        (*editor).set_selected_index(static_cast<std::size_t>(
                            std::distance(row.choices.begin(), selected)));
                    }
                    (*editor).set_font({FontRole::content, 10.0, 400, false});
                    (*editor).accessible_name = row.name;
                    (*editor).accessible_description = row.description;
                    (*editor).enabled = row.enabled;
                    state.editor = editor;
                    owner.add_child(editor);
                } else if (row.editor == PropertyEditorKind::boolean) {
                    const bool checked = binding_value_to_bool(
                        BindingValue{row.value}).value_or(false);
                    std::shared_ptr<gui_forms::CheckBox> editor = make_control<CheckBox>(
                        StableId(row.stable_id + ".editor"),
                        checked ? "True" : "False");
                    (*editor).set_checked(checked);
                    (*editor).font = {FontRole::content, 10.0, 400, false};
                    (*editor).accessible_name = row.name;
                    (*editor).accessible_description = row.description;
                    (*editor).enabled = row.enabled;
                    state.editor = editor;
                    owner.add_child(editor);
                }
                if (row.resettable) {
                    std::shared_ptr<gui_forms::Button> reset = make_control<Button>(
                        StableId(row.stable_id + ".reset"), "Reset");
                    (*reset).font = {FontRole::control, 8.5, 600, false};
                    (*reset).enabled = row.enabled && row.reset_enabled;
                    (*reset).accessible_name = "Reset " + row.name;
                    (*reset).accessible_description = "Restore " + row.name + " to its declared default";
                    state.reset_button = reset;
                    owner.add_child(reset);
                }
                apply_editor_font(state.editor);
                apply_editor_font(state.reset_button);
                rows.push_back(std::move(state));
                connect_row(rows.back());
            }
        }
    }

    void connect_row(RowState& state) {
        const std::string row_id = spec(state).stable_id;
        if (const std::shared_ptr<gui_forms::TextBox> text = std::dynamic_pointer_cast<TextBox>(state.editor)) {
            // Enter commits the row (TextCommitted) and Escape reverts it
            // (TextCancelled); neither goes further.
            (*text).set_accepts_enter(true);
            (*text).set_accepts_escape(true);
            state.value_subscription = (*text).text_changed().subscribe(
                owner, TextValueChanged{this, row_id});
            state.commit_subscription = (*text).committed().subscribe(
                owner, TextCommitted{this, row_id});
            state.cancel_subscription = (*text).cancelled().subscribe(
                owner, TextCancelled{this, row_id});
        } else if (const std::shared_ptr<gui_forms::ComboBox> choice =
                       std::dynamic_pointer_cast<ComboBox>(state.editor)) {
            state.value_subscription = (*choice).selected_index_changed().subscribe(
                owner, ChoiceChanged{
                    this, row_id, std::weak_ptr<ComboBox>(choice)});
        } else if (const std::shared_ptr<gui_forms::CheckBox> check =
                       std::dynamic_pointer_cast<CheckBox>(state.editor)) {
            state.value_subscription = (*check).checked_changed().subscribe(
                owner, CheckChanged{
                    this, row_id, std::weak_ptr<CheckBox>(check)});
        }
        if (state.editor) {
            state.focus_subscription = (*state.editor).focus_observed().subscribe(
                owner, EnsureVisible{this, row_id});
        }
        if (state.reset_button) {
            state.reset_subscription = (*state.reset_button).clicked().subscribe(
                owner, ResetClicked{this, row_id});
            state.reset_focus_subscription =
                (*state.reset_button).focus_observed().subscribe(
                owner, EnsureVisible{this, row_id});
        }
    }

    void recompute_geometry() {
        const double width = std::max(0.0, owner.committed_arranged_bounds().width);
        const double viewport = std::max(0.0, owner.committed_arranged_bounds().height);
        const double s = scale();
        const double line = line_height();
        const double row_extra = std::max(row_height, line + 10.0) - property_row_height;
        const double stacked_extra = std::max(21.0, line + 6.0) - 21.0;
        const double padding = property_padding * s;
        const double gap = property_gap * s;
        const double scaled_label_width = label_width * s;
        const double validation_height = std::max(property_validation_height, line + 4.0) * s;
        const bool stacked = width < std::max(230.0 * s,
                                              scaled_label_width + 116.0 * s);
        double y{header_height};
        for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
            y += group_extent();
            if (!groups[group_index].expanded) continue;
            for (RowState& state : rows) {
                if (state.group_index != group_index) continue;
                const PropertyRowSpec& row = spec(state);
                if (!row_visible(state)) {
                    state.row_bounds = {};
                    state.name_bounds = {};
                    state.value_bounds = {};
                    state.reset_bounds = {};
                    state.validation_bounds = {};
                    continue;
                }
                const bool editable = row.editor != PropertyEditorKind::read_only;
                const bool interactive = editable || row.resettable;
                double height = ((interactive ? property_editor_height
                                              : property_row_height) + row_extra) * s;
                if (stacked) height += ((interactive ? 21.0 : 13.0) + stacked_extra) * s;
                if (!row.validation_message.empty()) height += validation_height;
                state.row_bounds = {0.0, y, width, height};
                const double indent = static_cast<double>(row.depth) * 15.0 * s +
                    (row.expandable ? 14.0 * s : 0.0);
                const double reset_width = row.resettable ? 48.0 * s : 0.0;
                const double reset_gap = row.resettable ? 5.0 * s : 0.0;
                if (stacked) {
                    state.name_bounds = {padding + indent, y + 3.0 * s,
                                         std::max(0.0, width - padding * 2.0 -
                                                           indent),
                                         (18.0 + stacked_extra) * s};
                    state.value_bounds = {padding, y + (21.0 + stacked_extra) * s,
                        std::max(0.0, width - padding * 2.0 - reset_width -
                                          reset_gap),
                        ((interactive ? 28.0 : 19.0) + row_extra) * s};
                } else {
                    state.name_bounds = {padding + indent, y + 5.0 * s,
                                         std::max(0.0, scaled_label_width - indent),
                                         (21.0 + row_extra) * s};
                    state.value_bounds = {
                        padding + scaled_label_width + gap, y + 3.0 * s,
                        std::max(0.0, width - padding * 2.0 -
                                          scaled_label_width - gap - reset_width -
                                          reset_gap),
                        ((interactive ? 28.0 : 22.0) + row_extra) * s};
                }
                state.reset_bounds = row.resettable
                    ? Rect{std::max(padding, width - padding - reset_width),
                           state.value_bounds.y, reset_width,
                           state.value_bounds.height}
                    : Rect{};
                state.validation_bounds = {
                    state.value_bounds.x,
                    y + height - validation_height,
                    state.value_bounds.width, validation_height};
                y += height;
            }
        }
        content_height = y;
        scroll_offset = std::clamp(scroll_offset, 0.0,
            std::max(0.0, content_height - viewport));
    }

    void arrange_editors() {
        if (header) {
            owner.set_child_layout(header,
                {0.0, -scroll_offset,
                 std::max(0.0, owner.committed_arranged_bounds().width),
                 header_height});
        }
        for (RowState& state : rows) {
            const bool visible = groups[state.group_index].expanded &&
                row_visible(state);
            if (state.editor) {
                (*state.editor).visible = visible;
            }
            if (state.reset_button) {
                (*state.reset_button).visible = visible;
            }
            if (visible && state.editor) {
                owner.set_child_layout(state.editor,
                    {state.value_bounds.x,
                     state.value_bounds.y - scroll_offset,
                     state.value_bounds.width, state.value_bounds.height});
            }
            if (visible && state.reset_button) {
                owner.set_child_layout(state.reset_button,
                    {state.reset_bounds.x,
                     state.reset_bounds.y - scroll_offset,
                     state.reset_bounds.width, state.reset_bounds.height});
            }
        }
    }

    void ensure_visible(std::string_view row_id) {
        RowState* row = find_row(row_id);
        if (!row) return;
        const double viewport = owner.committed_arranged_bounds().height;
        if ((*row).row_bounds.y < scroll_offset) {
            scroll_offset = (*row).row_bounds.y;
        } else if ((*row).row_bounds.y + (*row).row_bounds.height >
                   scroll_offset + viewport) {
            scroll_offset = (*row).row_bounds.y + (*row).row_bounds.height - viewport;
        }
        scroll_offset = std::clamp(scroll_offset, 0.0,
            std::max(0.0, content_height - viewport));
        arrange_editors();
        owner.invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    }

    std::optional<std::size_t> group_at(Point absolute) const {
        const Rect bounds = owner.absolute_bounds();
        if (!bounds.contains(absolute)) return {};
        const double y_target = absolute.y - bounds.y + scroll_offset;
        const double group_height = group_extent();
        double y{header_height};
        for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
            if (y_target >= y && y_target < y + group_height) {
                return group_index;
            }
            y += group_height;
            if (!groups[group_index].expanded) continue;
            for (const RowState& state : rows) {
                if (state.group_index == group_index) y += state.row_bounds.height;
            }
        }
        return {};
    }

    RowState* disclosure_at(Point absolute) noexcept {
        const Rect bounds = owner.absolute_bounds();
        if (!bounds.contains(absolute)) return nullptr;
        const Point local{absolute.x - bounds.x,
                          absolute.y - bounds.y + scroll_offset};
        for (RowState& state : rows) {
            const PropertyRowSpec& row = spec(state);
            if (!groups[state.group_index].expanded || !row_visible(state) ||
                !row.expandable || !state.row_bounds.contains(local)) {
                continue;
            }
            const double disclosure_right = state.name_bounds.x;
            const double disclosure_left = std::max(
                0.0, disclosure_right - 16.0 * scale());
            if (local.x >= disclosure_left &&
                local.x <= disclosure_right + state.name_bounds.width) {
                return &state;
            }
        }
        return nullptr;
    }

    PropertyList& owner;
    GroupList groups;
    RowList rows;
    Control::Ptr header;
    double header_height{};
    double label_width{76.0};
    FontSpec font{FontRole::content, 9.5, 400, false};
    double row_height{property_row_height};
    bool font_overridden{};
    double scroll_offset{};
    double content_height{};
    bool synchronizing{};
};

PropertyList::PropertyList(StableId stable_id)
    : Panel(std::move(stable_id)), impl_(std::make_unique<Impl>(*this)) {
    set_background(Color::rgba(244, 247, 251));
    set_border_style(BorderStyle::none);
    set_focusable(true);
    set_paint_plane(PaintPlane::control);
}

PropertyList::~PropertyList() = default;

const std::vector<PropertyGroupSpec>& PropertyList::groups() const noexcept {
    return (*impl_).groups;
}

void PropertyList::set_groups(std::vector<PropertyGroupSpec> groups) {
    require_mutable();
    std::unordered_set<std::string> identities;
    for (const gui_forms::PropertyGroupSpec& group : groups) {
        require_property_text(group.stable_id, "group ID");
        require_property_text(group.title, "group title");
        if (!valid_stable_id(group.stable_id) || group.title.empty() ||
            !identities.insert(group.stable_id).second) {
            throw std::invalid_argument(
                "PropertyList groups require unique identities following the StableId segment rule and titles");
        }
        std::unordered_map<std::string, const PropertyRowSpec*> prior_rows;
        for (const gui_forms::PropertyRowSpec& row : group.rows) {
            require_property_text(row.stable_id, "row ID");
            require_property_text(row.name, "row name");
            require_property_text(row.value, "row value");
            require_property_text(row.description, "row description");
            require_property_text(row.validation_message, "validation message");
            if (!valid_stable_id(row.stable_id) || row.name.empty() ||
                !identities.insert(row.stable_id).second) {
                throw std::invalid_argument(
                    "PropertyList rows require unique identities following the StableId segment rule and names");
            }
            if (row.depth > 8U ||
                (row.expanded && !row.expandable) ||
                (row.reset_enabled && !row.resettable)) {
                throw std::invalid_argument(
                    "PropertyList row hierarchy/reset state is inconsistent");
            }
            if (row.parent_id.empty()) {
                if (row.depth != 0U) {
                    throw std::invalid_argument(
                        "PropertyList root rows must have depth zero");
                }
            } else {
                const std::unordered_map<
                    std::string, const PropertyRowSpec*>::iterator parent =
                    prior_rows.find(row.parent_id);
                if (parent == prior_rows.end() ||
                    !(*(*parent).second).expandable ||
                    row.depth != (*(*parent).second).depth + 1U) {
                    throw std::invalid_argument(
                        "PropertyList child rows require an earlier expandable parent at the preceding depth");
                }
            }
            if (row.editor == PropertyEditorKind::choice && row.choices.empty()) {
                throw std::invalid_argument(
                    "PropertyList choice rows require at least one choice");
            }
            for (const std::string& choice : row.choices) {
                require_property_text(choice, "choice");
            }
            if (row.editor == PropertyEditorKind::choice && !row.value.empty() &&
                std::find(row.choices.begin(), row.choices.end(), row.value) ==
                    row.choices.end()) {
                throw std::invalid_argument(
                    "PropertyList choice value must be empty or one declared choice");
            }
            if (row.editor == PropertyEditorKind::boolean &&
                !binding_value_to_bool(BindingValue{row.value})) {
                throw std::invalid_argument(
                    "PropertyList Boolean value must be True, False, 1, or 0");
            }
            prior_rows.emplace(row.stable_id, &row);
        }
    }
    (*impl_).groups = std::move(groups);
    (*impl_).scroll_offset = 0.0;
    (*impl_).rebuild_editors();
    (*impl_).recompute_geometry();
    invalidate(Dirty::measure | Dirty::layout | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
}

bool PropertyList::set_value(std::string_view row_id, std::string value) {
    require_mutable();
    require_property_text(value, "row value");
    Impl::RowState* row = (*impl_).find_row(row_id);
    if (!row) return false;
    PropertyRowSpec& model = (*impl_).spec(*row);
    if (model.editor == PropertyEditorKind::choice && !value.empty() &&
        std::find(model.choices.begin(), model.choices.end(), value) ==
            model.choices.end()) {
        throw std::invalid_argument(
            "PropertyList choice value must be empty or one declared choice");
    }
    if (model.editor == PropertyEditorKind::boolean &&
        !binding_value_to_bool(BindingValue{value})) {
        throw std::invalid_argument(
            "PropertyList Boolean value must be True, False, 1, or 0");
    }
    if (model.value == value && (*row).committed_value == value) return true;
    (*impl_).synchronizing = true;
    model.value = value;
    (*row).committed_value = value;
    if (const std::shared_ptr<gui_forms::TextBox> text = std::dynamic_pointer_cast<TextBox>((*row).editor)) {
        (*text).set_text(value);
    } else if (const std::shared_ptr<gui_forms::ComboBox> choice = std::dynamic_pointer_cast<ComboBox>((*row).editor)) {
        const std::vector<std::string>::iterator found =
            std::find(model.choices.begin(), model.choices.end(), value);
        (*choice).set_selected_index(found == model.choices.end()
            ? std::optional<std::size_t>{}
            : std::optional<std::size_t>{static_cast<std::size_t>(
                std::distance(model.choices.begin(), found))});
    } else if (const std::shared_ptr<gui_forms::CheckBox> check =
                   std::dynamic_pointer_cast<CheckBox>((*row).editor)) {
        const bool checked = binding_value_to_bool(
            BindingValue{value}).value_or(false);
        (*check).set_checked(checked);
        (*check).text = checked ? "True" : "False";
    }
    (*impl_).synchronizing = false;
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

bool PropertyList::set_description(std::string_view row_id,
                                   std::string description) {
    require_mutable();
    require_property_text(description, "row description");
    Impl::RowState* row = (*impl_).find_row(row_id);
    if (!row) return false;
    PropertyRowSpec& model = (*impl_).spec(*row);
    if (model.description == description) return true;
    model.description = std::move(description);
    if ((*row).editor) {
        std::string accessible = model.description;
        if (!model.validation_message.empty()) {
            if (!accessible.empty()) accessible += " · ";
            accessible += "Error: " + model.validation_message;
        }
        (*(*row).editor).accessible_description = std::move(accessible);
    }
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
    return true;
}

bool PropertyList::set_validation(std::string_view row_id, std::string message) {
    require_mutable();
    require_property_text(message, "validation message");
    Impl::RowState* row = (*impl_).find_row(row_id);
    if (!row) return false;
    PropertyRowSpec& model = (*impl_).spec(*row);
    if (model.validation_message == message) return true;
    model.validation_message = std::move(message);
    if ((*row).editor) {
        std::string description = model.description;
        if (!model.validation_message.empty()) {
            if (!description.empty()) description += " · ";
            description += "Error: " + model.validation_message;
        }
        (*(*row).editor).accessible_description = std::move(description);
    }
    (*impl_).recompute_geometry();
    (*impl_).arrange_editors();
    invalidate(Dirty::measure | Dirty::layout | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
    return true;
}

bool PropertyList::set_reset_enabled(std::string_view row_id, bool enabled) {
    require_mutable();
    Impl::RowState* row = (*impl_).find_row(row_id);
    if (!row || !(*impl_).spec(*row).resettable || !(*row).reset_button) return false;
    PropertyRowSpec& model = (*impl_).spec(*row);
    if (model.reset_enabled == enabled) return true;
    model.reset_enabled = enabled;
    (*(*row).reset_button).enabled = model.enabled && enabled;
    invalidate(Dirty::paint | Dirty::semantics | Dirty::accessibility);
    return true;
}

bool PropertyList::set_group_expanded(std::string_view id, bool expanded) {
    require_mutable();
    Impl::GroupList::iterator found = (*impl_).groups.begin();
    while (found != (*impl_).groups.end() && (*found).stable_id != id) {
        ++found;
    }
    if (found == (*impl_).groups.end()) return false;
    if ((*found).expanded == expanded) return true;
    (*found).expanded = expanded;
    (*impl_).recompute_geometry();
    (*impl_).arrange_editors();
    invalidate(Dirty::measure | Dirty::layout | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
    publish_change(group_changed_,
                   PropertyGroupChange{(*found).stable_id, expanded});
    return true;
}

bool PropertyList::set_row_expanded(std::string_view id, bool expanded) {
    require_mutable();
    Impl::RowState* row = (*impl_).find_row(id);
    if (!row || !(*impl_).spec(*row).expandable) return false;
    PropertyRowSpec& model = (*impl_).spec(*row);
    if (model.expanded == expanded) return true;
    model.expanded = expanded;
    (*impl_).recompute_geometry();
    (*impl_).arrange_editors();
    invalidate(Dirty::measure | Dirty::layout | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
    publish_change(row_expansion_changed_,
                   PropertyRowExpansionChange{model.stable_id, expanded});
    return true;
}

std::optional<bool> PropertyList::row_expanded(std::string_view id) const {
    const Impl::RowState* row = (*impl_).find_row(id);
    return row && (*impl_).spec(*row).expandable
        ? std::optional<bool>{(*impl_).spec(*row).expanded}
        : std::optional<bool>{};
}

std::optional<std::string> PropertyList::value(std::string_view id) const {
    const Impl::RowState* row = (*impl_).find_row(id);
    return row ? std::optional<std::string>{(*impl_).spec(*row).value}
               : std::optional<std::string>{};
}

Control::Ptr PropertyList::editor(std::string_view id) const {
    const Impl::RowState* row = (*impl_).find_row(id);
    return row ? (*row).editor : Control::Ptr{};
}

bool PropertyList::replace_editor(std::string_view id, Control::Ptr editor) {
    require_mutable();
    Impl::RowState* row = (*impl_).find_row(id);
    if (!row) return false;
    if (!editor || !(*editor).is_alive() || (*editor).parent() ||
        (*editor).attached_window()) {
        throw std::invalid_argument(
            "PropertyList replacement editor must be an unattached live control");
    }
    PropertyRowSpec& model = (*impl_).spec(*row);
    if (!model.enabled) (*editor).enabled = false;
    if ((*editor).accessible_name.read().empty()) {
        (*editor).accessible_name = model.name;
    }
    if ((*editor).accessible_description.read().empty()) {
        (*editor).accessible_description = model.description;
    }

    // Attach first so an identity/lifecycle failure leaves the existing editor
    // and its subscriptions intact.
    (*impl_).apply_editor_font(editor);
    add_child(editor);
    Control::Ptr previous = (*row).editor;
    (*row).value_subscription.disconnect();
    (*row).commit_subscription.disconnect();
    (*row).cancel_subscription.disconnect();
    (*row).focus_subscription.disconnect();
    (*row).editor = editor;
    model.editor = PropertyEditorKind::custom;
    (*row).focus_subscription = (*editor).focus_observed().subscribe(
        *this, Impl::EnsureVisible{impl_.get(), std::string(id)});
    if (previous && (*previous).parent().get() == this) {
        Control::Ptr removed = remove_child((*previous).runtime_id());
        if (removed && (*removed).is_alive()) (*removed).dispose();
    }
    (*impl_).recompute_geometry();
    (*impl_).arrange_editors();
    invalidate(Dirty::measure | Dirty::layout | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
    return true;
}

std::shared_ptr<Button> PropertyList::reset_button(std::string_view id) const {
    const Impl::RowState* row = (*impl_).find_row(id);
    return row ? (*row).reset_button : std::shared_ptr<Button>{};
}

void PropertyList::set_header_content(Control::Ptr content, double height) {
    require_mutable();
    if (!std::isfinite(height) || height < 0.0 || height > 4096.0 ||
        (!content && height != 0.0)) {
        throw std::invalid_argument(
            "PropertyList header requires content and a 0 through 4096 height");
    }
    if ((*impl_).header == content && (*impl_).header_height == height) return;
    if ((*impl_).header && (*(*impl_).header).parent().get() == this) {
        Control::Ptr previous = remove_child((*(*impl_).header).runtime_id());
        if (previous && (*previous).is_alive()) (*previous).dispose();
    }
    (*impl_).header = std::move(content);
    (*impl_).header_height = height;
    if ((*impl_).header) add_child((*impl_).header);
    (*impl_).recompute_geometry();
    (*impl_).arrange_editors();
    invalidate(Dirty::measure | Dirty::layout | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
}

Control::Ptr PropertyList::header_content() const noexcept {
    return (*impl_).header;
}

double PropertyList::header_height() const noexcept { return (*impl_).header_height; }

void PropertyList::set_header_height(double height) {
    require_mutable();
    if (!std::isfinite(height) || height < 0.0 || height > 4096.0 ||
        (!(*impl_).header && height != 0.0)) {
        throw std::invalid_argument("PropertyList header height must be 0 through 4096");
    }
    if ((*impl_).header_height == height) return;
    (*impl_).header_height = height;
    (*impl_).recompute_geometry();
    (*impl_).arrange_editors();
    invalidate(Dirty::measure | Dirty::layout | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
}

double PropertyList::label_width() const noexcept { return (*impl_).label_width; }

FontSpec PropertyList::font() const noexcept { return (*impl_).font; }

void PropertyList::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) {
        throw std::invalid_argument("PropertyList font specification is invalid");
    }
    set_font_from_code(true);
    if ((*impl_).font_overridden && (*impl_).font == font) return;
    (*impl_).font = font;
    (*impl_).font_overridden = true;
    for (const Impl::RowState& row : (*impl_).rows) {
        (*impl_).apply_editor_font(row.editor);
        (*impl_).apply_editor_font(row.reset_button);
    }
    (*impl_).recompute_geometry();
    (*impl_).arrange_editors();
    invalidate(Dirty::measure | Dirty::layout | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

double PropertyList::row_height() const noexcept { return (*impl_).row_height; }

void PropertyList::set_row_height(double height) {
    require_mutable();
    if (!std::isfinite(height) || height <= 0.0 || height > 4096.0) {
        throw std::invalid_argument("PropertyList row height must be positive and at most 4096");
    }
    if ((*impl_).row_height == height) return;
    (*impl_).row_height = height;
    (*impl_).recompute_geometry();
    (*impl_).arrange_editors();
    invalidate(Dirty::measure | Dirty::layout | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

void PropertyList::set_label_width(double width) {
    require_mutable();
    if (!std::isfinite(width) || width < 40.0 || width > 320.0) {
        throw std::invalid_argument("PropertyList label width must be 40 through 320");
    }
    if ((*impl_).label_width == width) return;
    (*impl_).label_width = width;
    (*impl_).recompute_geometry();
    (*impl_).arrange_editors();
    invalidate(Dirty::measure | Dirty::layout | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
}

double PropertyList::scroll_offset() const noexcept { return (*impl_).scroll_offset; }

void PropertyList::set_scroll_offset(double offset) {
    require_mutable();
    if (!std::isfinite(offset)) {
        throw std::invalid_argument("PropertyList scroll offset must be finite");
    }
    const double maximum = std::max(0.0, (*impl_).content_height -
                                          committed_arranged_bounds().height);
    const double next = std::clamp(offset, 0.0, maximum);
    if (next == (*impl_).scroll_offset) return;
    (*impl_).scroll_offset = next;
    (*impl_).arrange_editors();
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

double PropertyList::content_height() const noexcept { return (*impl_).content_height; }

Size PropertyList::measure(Size available) {
    return {available.width, std::min(available.height,
        std::max((*impl_).group_extent(),
                 (*impl_).content_height))};
}

void PropertyList::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    (*impl_).recompute_geometry();
    (*impl_).arrange_editors();
}

// The face, each group's header band, rule, expander and title, each row's
// expander, name, read-only value, validation message and separator rule,
// and the scroll thumb are parts the theme's engine draws. A disabled row's
// parts are disabled and a validation message is invalid. A background
// override supplies the face's ground (T4-1).
void PropertyList::on_paint(Painter& painter, Rect) {
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    const double s = effective_text_scale();
    const double group_height = (*impl_).group_extent();
    FontSpec group_font = effective_font((*impl_).font_overridden
        ? (*impl_).font : FontSpec{FontRole::control, 8.5, 700, false, 0.24});
    group_font.weight = std::max<std::uint16_t>(group_font.weight, 700U);
    const FontSpec name_font = effective_font((*impl_).font_overridden
        ? (*impl_).font : FontSpec{FontRole::content, 9.5, 600, false});
    const FontSpec value_font = text_font((*impl_).font);
    const FontSpec validation_font = effective_font((*impl_).font_overridden
        ? (*impl_).font : FontSpec{FontRole::content, 8.5, 600, false});
    AnatomyStateSet states = anatomy_states();
    states.erase(AnatomyState::hot);
    // An expander is a square of this side centred where its glyph sat.
    const double expander_side = 10.0 * s;
    PartPaint face;
    face.family = ControlVisualRole::panel;
    face.role = PartRole::face;
    face.bounds = bounds;
    face.states = states;
    if (has_background_override()) face.authored_background = background();
    face.key = PartKey{"face", {}, {}};
    paint_part(painter, *this, face);
    double y = (*impl_).header_height - (*impl_).scroll_offset;
    for (std::size_t group_index = 0;
         group_index < (*impl_).groups.size(); ++group_index) {
        const PropertyGroupSpec& group = (*impl_).groups[group_index];
        PartPaint header;
        header.family = ControlVisualRole::panel;
        header.role = PartRole::face;
        header.bounds = {0.0, y, bounds.width, group_height};
        header.states = states;
        paint_part(painter, *this, header);
        PartPaint rule;
        rule.family = ControlVisualRole::panel;
        rule.role = PartRole::divider;
        rule.bounds = {0.0, y + group_height - 1.0, bounds.width, 1.0};
        rule.states = states;
        paint_part(painter, *this, rule);
        const double group_baseline = y + std::max(group_font.size,
                                                    group_height * 0.5 +
                                                        group_font.size * 0.35);
        PartPaint group_expander;
        group_expander.family = ControlVisualRole::panel;
        group_expander.role = PartRole::disclosure;
        group_expander.mark = group.expanded ? PartMark::chevron_down : PartMark::chevron_right;
        group_expander.bounds = {6.0 * s, y + (group_height - expander_side) * 0.5,
                                 expander_side, expander_side};
        group_expander.states = states;
        if (group.expanded) group_expander.states.insert(AnatomyState::open);
        paint_part(painter, *this, group_expander);
        PartPaint title;
        title.family = ControlVisualRole::panel;
        title.role = PartRole::caption;
        title.states = states;
        title.text = group.title;
        title.font = group_font;
        paint_text_part_at(painter, *this, title, {22.0 * s, group_baseline},
                           std::max(0.0, bounds.width - 22.0 * s));
        y += group_height;
        if (!group.expanded) continue;
        for (const Impl::RowState& row_state : (*impl_).rows) {
            if (row_state.group_index != group_index) continue;
            const PropertyRowSpec& row = (*impl_).spec(row_state);
            if (!(*impl_).row_visible(row_state)) continue;
            const Rect name{row_state.name_bounds.x,
                            row_state.name_bounds.y - (*impl_).scroll_offset,
                            row_state.name_bounds.width,
                            row_state.name_bounds.height};
            const Rect value_bounds{row_state.value_bounds.x,
                                    row_state.value_bounds.y - (*impl_).scroll_offset,
                                    row_state.value_bounds.width,
                                    row_state.value_bounds.height};
            AnatomyStateSet row_states = states;
            if (!row.enabled) row_states.insert(AnatomyState::disabled);
            const double name_baseline = name.y + std::max(15.0 * s, name_font.size);
            if (row.expandable) {
                PartPaint row_expander;
                row_expander.family = ControlVisualRole::editor;
                row_expander.role = PartRole::disclosure;
                row_expander.mark = row.expanded ? PartMark::chevron_down : PartMark::chevron_right;
                row_expander.bounds = {std::max(2.0 * s, name.x - 13.0 * s),
                                       name_baseline - name_font.size * 0.35 - expander_side * 0.5,
                                       expander_side, expander_side};
                row_expander.states = row_states;
                if (row.expanded) row_expander.states.insert(AnatomyState::open);
                paint_part(painter, *this, row_expander);
            }
            PartPaint name_part;
            name_part.family = ControlVisualRole::editor;
            name_part.role = PartRole::label;
            name_part.states = row_states;
            name_part.text = row.name;
            name_part.font = name_font;
            paint_text_part_at(painter, *this, name_part, {name.x, name_baseline}, name.width);
            if (row.editor == PropertyEditorKind::read_only) {
                PartPaint value;
                value.family = ControlVisualRole::editor;
                value.role = PartRole::content;
                value.states = row_states;
                value.text = row.value;
                value.font = value_font;
                paint_text_part_at(painter, *this, value,
                                   {value_bounds.x,
                                    value_bounds.y + std::max(15.0 * s, value_font.size)},
                                   value_bounds.width);
            }
            if (!row.validation_message.empty()) {
                const Rect validation{row_state.validation_bounds.x,
                    row_state.validation_bounds.y - (*impl_).scroll_offset,
                    row_state.validation_bounds.width,
                    row_state.validation_bounds.height};
                PartPaint message;
                message.family = ControlVisualRole::editor;
                message.role = PartRole::content;
                message.states = row_states;
                message.states.insert(AnatomyState::invalid);
                // PartPaint::text borrows: the line is held here, past the paint.
                const std::string line = "! " + row.validation_message;
                message.text = line;
                message.font = validation_font;
                paint_text_part_at(painter, *this, message,
                                   {validation.x,
                                    validation.y + std::max(13.0 * s, validation_font.size)},
                                   validation.width);
            }
            PartPaint separator;
            separator.family = ControlVisualRole::editor;
            separator.role = PartRole::divider;
            separator.bounds = {property_padding * s,
                                row_state.row_bounds.y + row_state.row_bounds.height -
                                    (*impl_).scroll_offset - 1.0,
                                std::max(0.0, bounds.width - 2.0 * property_padding * s), 1.0};
            separator.states = states;
            paint_part(painter, *this, separator);
            y += row_state.row_bounds.height;
        }
    }
    if ((*impl_).content_height > bounds.height && bounds.height > 12.0) {
        const double ratio = bounds.height / (*impl_).content_height;
        const double thumb_height = std::max(18.0, bounds.height * ratio);
        const double maximum = (*impl_).content_height - bounds.height;
        const double thumb_y = maximum <= 0.0 ? 0.0 :
            (bounds.height - thumb_height) * (*impl_).scroll_offset / maximum;
        PartPaint thumb;
        thumb.family = ControlVisualRole::panel;
        thumb.role = PartRole::thumb;
        thumb.bounds = {std::max(0.0, bounds.width - 4.0), thumb_y, 3.0, thumb_height};
        thumb.states = states;
        paint_part(painter, *this, thumb);
    }
}

void PropertyList::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::wheel && event.wheel_delta.y != 0.0) {
        set_scroll_offset((*impl_).scroll_offset +
            (event.wheel_delta.y > 0.0 ? -54.0 : 54.0) *
                effective_text_scale());
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::down &&
        event.button == PointerButton::primary) {
        if (Impl::RowState* row = (*impl_).disclosure_at(event.position)) {
            const PropertyRowSpec& model = (*impl_).spec(*row);
            static_cast<void>(set_row_expanded(model.stable_id,
                                               !model.expanded));
            event.handled = true;
            return;
        }
        const std::optional<std::size_t> group = (*impl_).group_at(event.position);
        if (group) {
            static_cast<void>(set_group_expanded(
                (*impl_).groups[*group].stable_id,
                !(*impl_).groups[*group].expanded));
            event.handled = true;
        }
    }
}

void PropertyList::on_key(KeyEvent& event) {
    if (event.action != KeyAction::down) return;
    double next = (*impl_).scroll_offset;
    if (event.physical_key == PhysicalKey::home) next = 0.0;
    else if (event.physical_key == PhysicalKey::end) next = (*impl_).content_height;
    else if (event.physical_key == PhysicalKey::page_up) {
        next -= committed_arranged_bounds().height;
    } else if (event.physical_key == PhysicalKey::page_down) {
        next += committed_arranged_bounds().height;
    } else return;
    set_scroll_offset(next);
    event.handled = true;
}

SemanticDescriptor PropertyList::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::property_grid;
    descriptor.name = accessible_name.read().empty() ? "Properties" : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    descriptor.include_descendants = true;
    return descriptor;
}

std::vector<SemanticNode> PropertyList::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes;
    const Rect absolute = absolute_bounds();
    const double group_height = (*impl_).group_extent();
    double y = (*impl_).header_height - (*impl_).scroll_offset;
    for (std::size_t group_index = 0;
         group_index < (*impl_).groups.size(); ++group_index) {
        const PropertyGroupSpec& group = (*impl_).groups[group_index];
        SemanticNode header;
        header.stable_id = group.stable_id;
        header.runtime_id = virtual_semantic_runtime_id(header.stable_id);
        header.role = SemanticRole::property_group;
        header.name = group.title;
        header.bounds = {absolute.x, absolute.y + y, absolute.width,
                         group_height};
        header.states = SemanticState::visible | SemanticState::focusable |
                        SemanticState::enabled;
        if (group.expanded) header.states |= SemanticState::expanded;
        header.actions = {SemanticAction::focus,
            group.expanded ? SemanticAction::collapse : SemanticAction::expand};
        nodes.push_back(std::move(header));
        y += group_height;
        if (!group.expanded) continue;
        for (const Impl::RowState& row_state : (*impl_).rows) {
            if (row_state.group_index != group_index) continue;
            const PropertyRowSpec& row = (*impl_).spec(row_state);
            if (!(*impl_).row_visible(row_state)) continue;
            if (row.editor == PropertyEditorKind::read_only) {
                SemanticNode node;
                node.stable_id = row.stable_id;
                node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
                node.role = SemanticRole::property_row;
                node.name = row.name;
                node.value = row.value;
                node.description = row.validation_message.empty()
                    ? row.description : row.description + " · Error: " +
                                            row.validation_message;
                node.bounds = {absolute.x,
                    absolute.y + row_state.row_bounds.y - (*impl_).scroll_offset,
                    absolute.width, row_state.row_bounds.height};
                node.states = SemanticState::visible;
                if (row.enabled) node.states |= SemanticState::enabled;
                if (row.expandable) {
                    if (row.expanded) node.states |= SemanticState::expanded;
                    node.actions.push_back(row.expanded
                        ? SemanticAction::collapse : SemanticAction::expand);
                }
                nodes.push_back(std::move(node));
            }
            y += row_state.row_bounds.height;
        }
    }
    return nodes;
}

bool PropertyList::on_semantic_child_action(std::string_view id,
                                            SemanticAction action,
                                            std::string_view) {
    Impl::GroupList::iterator found = (*impl_).groups.begin();
    while (found != (*impl_).groups.end() && (*found).stable_id != id) {
        ++found;
    }
    if (found != (*impl_).groups.end()) {
        if (action == SemanticAction::focus) {
            if (window()) {
                static_cast<void>((*window()).request_focus(shared_from_this()));
            }
            return true;
        }
        if (action == SemanticAction::expand ||
            action == SemanticAction::collapse) {
            return set_group_expanded(id, action == SemanticAction::expand);
        }
        return false;
    }
    const Impl::RowState* row = (*impl_).find_row(id);
    if (row && (*impl_).spec(*row).expandable &&
        (action == SemanticAction::expand ||
         action == SemanticAction::collapse)) {
        return set_row_expanded(id, action == SemanticAction::expand);
    }
    return false;
}

void PropertyList::on_dispose() noexcept {
    // Component::dispose marks the control disposing before entering this
    // callback. Control::on_dispose owns the disposal-safe child detach path.
    (*impl_).rows.clear();
    (*impl_).header.reset();
    Control::on_dispose();
}


namespace {

constexpr std::array<PartField<PropertyList::Parts>, 1> property_list_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &PropertyList::Parts::face},
}};

constexpr std::array<PartField<PropertyList::ItemParts>, 9> property_list_item_part_fields{{
    {.declaration = {.name = "group_header", .role = PartRole::face, .composed_from = {}},
     .member = &PropertyList::ItemParts::group_header},
    {.declaration = {.name = "group_expander", .role = PartRole::disclosure, .composed_from = {}},
     .member = &PropertyList::ItemParts::group_expander},
    {.declaration = {.name = "group_title", .role = PartRole::caption, .composed_from = {}},
     .member = &PropertyList::ItemParts::group_title},
    {.declaration = {.name = "property_row", .role = PartRole::region, .composed_from = {}},
     .member = &PropertyList::ItemParts::property_row},
    {.declaration = {.name = "property_name", .role = PartRole::label, .composed_from = {}},
     .member = &PropertyList::ItemParts::property_name},
    {.declaration = {.name = "property_value", .role = PartRole::content, .composed_from = {}},
     .member = &PropertyList::ItemParts::property_value},
    {.declaration = {.name = "row_expander", .role = PartRole::disclosure, .composed_from = {}},
     .member = &PropertyList::ItemParts::row_expander},
    {.declaration = {.name = "validation", .role = PartRole::validation, .composed_from = {}},
     .member = &PropertyList::ItemParts::validation},
    {.declaration = {.name = "row_divider", .role = PartRole::divider, .composed_from = {}},
     .member = &PropertyList::ItemParts::row_divider},
}};

constexpr std::array<StateField<PropertyList::ItemStates>, 5> property_list_item_state_fields{{
    {.state = AnatomyState::open, .member = &PropertyList::ItemStates::open},
    {.state = AnatomyState::closed, .member = &PropertyList::ItemStates::closed},
    {.state = AnatomyState::invalid, .member = &PropertyList::ItemStates::invalid},
    {.state = AnatomyState::read_only, .member = &PropertyList::ItemStates::read_only},
    {.state = AnatomyState::disabled, .member = &PropertyList::ItemStates::disabled},
}};

constexpr std::array<PartDeclaration, 9> property_list_item_parts =
    part_declarations(property_list_item_part_fields);
constexpr std::array<AnatomyState, 5> property_list_item_states =
    state_declarations(property_list_item_state_fields);
constexpr std::array<CustomStateDeclaration, 1> property_list_item_custom_states{{
    {.name = "group_header"},
}};
constexpr CustomStateIndex group_header_item_state{0U};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 1>
    property_list_parts = joined_declarations(scrollable_detail::parts,
                                         part_declarations(property_list_extra_part_fields));

constexpr std::array<ItemSystemDeclaration, 1> property_list_item_systems{{
    {.name = "item", .parts = property_list_item_parts, .states = property_list_item_states,
     .custom_states = property_list_item_custom_states},
}};

constexpr ControlAnatomy property_list_anatomy{
    .control_id = "property_list",
    .parts = property_list_parts,
    .states = scrollable_detail::states,
    .custom_states = {},
    .item_systems = property_list_item_systems,
};

} // namespace

const ControlAnatomy& PropertyList::anatomy() const noexcept {
    return property_list_anatomy;
}

void PropertyList::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.scrollable = scrollable_parts();

    record.face = PartGeometry::from_bounds(local_bounds());
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), property_list_extra_part_fields, record, geometry);
}

PropertyList::Parts PropertyList::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), property_list_extra_part_fields, published.parts(), record);
    return record;
}

std::size_t PropertyList::realized_item_count() const {
    return item_entries().size();
}

std::string PropertyList::realized_item_stable_id(std::size_t item) const {
    const ItemEntry entry = item_entries()[item];
    if (entry.group) return (*impl_).groups[entry.group_index].stable_id;
    return (*impl_).spec((*impl_).rows[entry.row_index]).stable_id;
}

void PropertyList::publish_realized_item_geometry(std::size_t item,
                                          std::span<PartGeometry> geometry) const {
    ItemParts record;

    // Group headers and property rows share one item anatomy; the parts of
    // the other kind are absent. Text is published as the slots on_paint
    // draws into; value editors are child controls.
    const ItemEntry entry = item_entries()[item];
    const Rect bounds = local_bounds();
    const double s = effective_text_scale();
    if (entry.group) {
        const double group_height = (*impl_).group_extent();
        const Rect band{0.0, entry.group_y, bounds.width, group_height};
        record.group_header = PartGeometry::from_bounds(Rect::intersection(band, bounds));
        record.group_expander = PartGeometry::from_bounds(Rect::intersection(
            {7.0 * s, entry.group_y, 13.0 * s, group_height}, bounds));
        record.group_title = PartGeometry::from_bounds(Rect::intersection(
            {22.0 * s, entry.group_y, std::max(0.0, bounds.width - 26.0 * s), group_height},
            bounds));
    } else {
        const Impl::RowState& state = (*impl_).rows[entry.row_index];
        const PropertyRowSpec& row = (*impl_).spec(state);
        const double offset = (*impl_).scroll_offset;
        const Rect band{state.row_bounds.x, state.row_bounds.y - offset,
                        state.row_bounds.width, state.row_bounds.height};
        const Rect name{state.name_bounds.x, state.name_bounds.y - offset,
                        state.name_bounds.width, state.name_bounds.height};
        const Rect value{state.value_bounds.x, state.value_bounds.y - offset,
                         state.value_bounds.width, state.value_bounds.height};
        record.property_row = PartGeometry::from_bounds(Rect::intersection(band, bounds));
        record.property_name = PartGeometry::from_bounds(Rect::intersection(name, bounds));
        record.property_value = PartGeometry::from_bounds(Rect::intersection(value, bounds));
        if (row.expandable) {
            record.row_expander = PartGeometry::from_bounds(Rect::intersection(
                {std::max(2.0 * s, name.x - 13.0 * s), name.y, 11.0 * s, name.height}, bounds));
        }
        if (!row.validation_message.empty()) {
            const Rect validation{state.validation_bounds.x, state.validation_bounds.y - offset,
                                  state.validation_bounds.width, state.validation_bounds.height};
            record.validation = PartGeometry::from_bounds(Rect::intersection(validation, bounds));
        }
        // The one-unit rule along the row's bottom, inset by the padding.
        const double padding = property_padding * s;
        const Rect divider{padding, band.bottom() - 1.5,
                           std::max(0.0, bounds.width - padding * 2.0), 1.0};
        record.row_divider = PartGeometry::from_bounds(Rect::intersection(divider, bounds));
    }
    write_item_parts(anatomy().item_systems.front(), property_list_item_part_fields, record, geometry);
}

AnatomyStateSet PropertyList::realized_item_anatomy_states(std::size_t item) const {
    AnatomyStateSet states;
    const ItemEntry entry = item_entries()[item];
    if (entry.group) {
        states.insert(group_header_item_state);
        const bool expanded = (*impl_).groups[entry.group_index].expanded;
        states.insert(expanded ? AnatomyState::open : AnatomyState::closed);
    } else {
        const PropertyRowSpec& row = (*impl_).spec((*impl_).rows[entry.row_index]);
        if (row.expandable) {
            states.insert(row.expanded ? AnatomyState::open : AnatomyState::closed);
        }
        if (!row.validation_message.empty()) states.insert(AnatomyState::invalid);
        if (row.editor == PropertyEditorKind::read_only) states.insert(AnatomyState::read_only);
        if (!row.enabled) states.insert(AnatomyState::disabled);
    }
    return states;
}

std::vector<PropertyList::Item> PropertyList::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records;
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record;
        record.index = item;
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems.front(), property_list_item_part_fields, published.parts(), record.parts);
        const AnatomyStateSet item_states = realized_item_anatomy_states(item);
        read_states(property_list_item_state_fields, item_states, record.states);
        record.states.group_header = item_states.contains(group_header_item_state);
        records.push_back(record);
    }
    return records;
}


std::vector<PropertyList::ItemEntry> PropertyList::item_entries() const {
    // The walk on_paint makes: each group header, then its visible rows
    // while the group is expanded.
    std::vector<ItemEntry> entries;
    double y = (*impl_).header_height - (*impl_).scroll_offset;
    for (std::size_t group_index = 0; group_index < (*impl_).groups.size(); ++group_index) {
        ItemEntry header;
        header.group = true;
        header.group_index = group_index;
        header.group_y = y;
        entries.push_back(header);
        y += (*impl_).group_extent();
        if (!(*impl_).groups[group_index].expanded) continue;
        for (std::size_t row_index = 0; row_index < (*impl_).rows.size(); ++row_index) {
            const Impl::RowState& state = (*impl_).rows[row_index];
            if (state.group_index != group_index || !(*impl_).row_visible(state)) continue;
            ItemEntry row;
            row.group_index = group_index;
            row.row_index = row_index;
            entries.push_back(row);
        }
    }
    return entries;
}

namespace {

// The events PropertyList adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 5> property_list_interactions{{
    {.name = "value_changed", .payload = "PropertyValueChange", .mutable_payload = false},
    {.name = "value_committed", .payload = "PropertyValueChange", .mutable_payload = false},
    {.name = "group_changed", .payload = "PropertyGroupChange", .mutable_payload = false},
    {.name = "row_expansion_changed", .payload = "PropertyRowExpansionChange", .mutable_payload = false},
    {.name = "reset_requested", .payload = "PropertyResetRequest", .mutable_payload = false},
}};

} // namespace

void PropertyList::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), property_list_interactions.begin(), property_list_interactions.end());
}

} // namespace gui_forms
