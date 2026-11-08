#include "gui_forms/controls/panel/property_grid/property_grid.hpp"

#include "gui_forms/controls/panel/color_value_editor/color_value_editor.hpp"
#include "gui_forms/controls/panel/flags_value_editor/flags_value_editor.hpp"
#include "property_grid_utilities.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace gui_forms {

using detail::CompoundFieldSpec;
using detail::compound_field_value;
using detail::compound_fields;
using detail::replace_compound_field;

struct PropertyGrid::Impl final {
    explicit Impl(PropertyGrid& public_owner)
        : owner(public_owner),
          converters(PropertyValueConverterRegistry::create_default()),
          editors(PropertyEditorRegistry::create_default()) {}

    enum class PathSegmentKind : std::uint8_t {
        compound_field,
        object_member,
        collection_index,
    };

    struct PathSegment final {
        PathSegmentKind kind{PathSegmentKind::compound_field};
        std::string name;
        std::size_t index{};
    };

    struct EditTarget final {
        std::string property_name;
        std::vector<PathSegment> path;
        bool writable{true};
        PropertyDescriptor descriptor;
    };

    struct InstalledEditor final {
        PropertyEditorBinding binding;
        SubscriptionToken commit;
        SubscriptionToken failure;
    };
    using DescriptorMap = std::map<std::string, PropertyDescriptor>;
    using StringMap = std::map<std::string, std::string>;
    using EditTargetMap = std::map<std::string, EditTarget>;
    using ValueMap = std::map<std::string, BindingValue>;
    using InstalledEditorMap = std::map<std::string, InstalledEditor>;

    struct DescriptorLess final {
        PropertySort sort{PropertySort::categorized};
        bool operator()(const PropertyDescriptor& left,
                        const PropertyDescriptor& right) const {
            if (sort == PropertySort::alphabetical) {
                return registered_name(left.name) <
                       registered_name(right.name);
            }
            const std::string left_category =
                registered_name(left.category);
            const std::string right_category =
                registered_name(right.category);
            if (left_category != right_category) {
                return left_category < right_category;
            }
            return registered_name(left.name) <
                   registered_name(right.name);
        }
    };

    struct PropertyRefresh final {
        Impl* implementation{};
        std::string canonical;
        void operator()() const {
            if (!(*implementation).committing) {
                (*implementation).refresh_one(canonical, true);
            }
        }
    };

    struct CustomEditorCommit final {
        Impl* implementation{};
        Control::WeakPtr owner_lifetime;
        std::string path;
        void operator()(BindingValue proposed) const {
            const Control::Ptr retained = owner_lifetime.lock();
            if (!retained || !(*retained).is_alive()) return;
            static_cast<void>((*implementation).set_value(
                path, std::move(proposed), false));
        }
    };

    struct CustomEditorFailure final {
        Impl* implementation{};
        Control::WeakPtr owner_lifetime;
        std::string path;
        void operator()(const PropertyEditorInputError& error) const {
            const Control::Ptr retained = owner_lifetime.lock();
            if (!retained || !(*retained).is_alive()) return;
            (*implementation).report_error(
                path, error.attempted_value, error.message);
        }
    };

    struct ListCommit final {
        Impl* implementation{};
        void operator()(const PropertyValueChange& change) const {
            (*implementation).commit_row(change);
        }
    };

    struct ListExpansion final {
        Impl* implementation{};
        void operator()(const PropertyRowExpansionChange& change) const {
            const StringMap::iterator property =
                (*implementation).row_to_property.find(change.row_id);
            if (property == (*implementation).row_to_property.end()) return;
            const std::string canonical =
                registered_name((*property).second);
            if (change.expanded) {
                (*implementation).expanded_properties.insert(canonical);
            } else {
                (*implementation).expanded_properties.erase(canonical);
            }
        }
    };

    struct ListReset final {
        Impl* implementation{};
        void operator()(const PropertyResetRequest& request) const {
            const StringMap::iterator property =
                (*implementation).row_to_property.find(request.row_id);
            if (property == (*implementation).row_to_property.end()) return;
            static_cast<void>((*implementation).set_value(
                (*property).second, BindingValue{}, true));
        }
    };

    [[nodiscard]] static bool expandable_value(const BindingValue& value) {
        if (!compound_fields(binding_value_kind(value)).empty()) return true;
        if (const gui_forms::PropertyObjectValue* object = std::get_if<PropertyObjectValue>(&value)) {
            return *object && !(*object).members().empty();
        }
        if (const gui_forms::PropertyCollectionValue* collection =
                std::get_if<PropertyCollectionValue>(&value)) {
            return *collection && !property_collection_items(*collection).empty();
        }
        return false;
    }

    [[nodiscard]] static std::string append_member_path(
        std::string_view parent, std::string_view member) {
        return std::string(parent) + "." + std::string(member);
    }

    [[nodiscard]] static std::string append_index_path(
        std::string_view parent, std::size_t index) {
        return std::string(parent) + "[" + std::to_string(index) + "]";
    }

    [[nodiscard]] static std::optional<BindingValue> value_at(
        BindingValue value, std::span<const PathSegment> path) {
        for (const PathSegment& segment : path) {
            if (segment.kind == PathSegmentKind::compound_field) {
                const std::optional<BindingValue> child = compound_field_value(value, segment.name);
                if (!child) return {};
                value = *child;
                continue;
            }
            if (segment.kind == PathSegmentKind::object_member) {
                const gui_forms::PropertyObjectValue* object = std::get_if<PropertyObjectValue>(&value);
                if (!object || !*object) return {};
                const std::string wanted = registered_name(segment.name);
                const PropertyObjectMember* member = nullptr;
                for (const PropertyObjectMember& candidate :
                     (*object).members()) {
                    if (registered_name(candidate.name) == wanted) {
                        member = &candidate;
                        break;
                    }
                }
                if (member == nullptr) return {};
                value = (*member).value;
                continue;
            }
            const gui_forms::PropertyCollectionValue* collection = std::get_if<PropertyCollectionValue>(&value);
            if (!collection || !*collection) return {};
            const std::span<const BindingValue> items = property_collection_items(*collection);
            if (segment.index >= items.size()) return {};
            value = items[segment.index];
        }
        return value;
    }

    [[nodiscard]] static std::optional<BindingValue> replace_at(
        const BindingValue& value, std::span<const PathSegment> path,
        const BindingValue& replacement) {
        if (path.empty()) {
            return valid_property_value_tree(replacement)
                ? std::optional<BindingValue>{replacement}
                : std::optional<BindingValue>{};
        }
        const PathSegment& segment = path.front();
        const std::span<const PathSegment> remainder = path.subspan(1U);
        if (segment.kind == PathSegmentKind::compound_field) {
            if (!remainder.empty()) return {};
            return replace_compound_field(value, segment.name, replacement);
        }
        if (segment.kind == PathSegmentKind::object_member) {
            const gui_forms::PropertyObjectValue* object = std::get_if<PropertyObjectValue>(&value);
            if (!object || !*object) return {};
            std::vector<PropertyObjectMember> members(
                (*object).members().begin(), (*object).members().end());
            const std::string wanted = registered_name(segment.name);
            std::vector<PropertyObjectMember>::iterator member = members.begin();
            while (member != members.end() &&
                   registered_name((*member).name) != wanted) {
                ++member;
            }
            if (member == members.end() || !(*member).writable) return {};
            const std::optional<BindingValue> child = replace_at((*member).value, remainder, replacement);
            if (!child) return {};
            (*member).value = *child;
            return BindingValue{make_property_object(
                std::string((*object).type_name()), std::move(members))};
        }
        const gui_forms::PropertyCollectionValue* collection = std::get_if<PropertyCollectionValue>(&value);
        if (!collection || !*collection) return {};
        const std::span<const BindingValue> current = property_collection_items(*collection);
        if (segment.index >= current.size()) return {};
        std::vector<BindingValue> items(current.begin(), current.end());
        const std::optional<BindingValue> child = replace_at(items[segment.index], remainder, replacement);
        if (!child) return {};
        items[segment.index] = *child;
        return BindingValue{make_property_collection(
            std::string((*collection).item_type_name()), (*collection).item_kind(),
            std::move(items))};
    }

    [[nodiscard]] static bool same_shape(const BindingValue& left,
                                         const BindingValue& right) {
        if (binding_value_kind(left) != binding_value_kind(right)) return false;
        const gui_forms::PropertyObjectValue* left_object = std::get_if<PropertyObjectValue>(&left);
        const gui_forms::PropertyObjectValue* right_object = std::get_if<PropertyObjectValue>(&right);
        if (left_object || right_object) {
            if (!left_object || !right_object || !*left_object || !*right_object ||
                (*left_object).type_name() != (*right_object).type_name() ||
                (*left_object).members().size() != (*right_object).members().size()) {
                return false;
            }
            for (std::size_t index = 0U;
                 index < (*left_object).members().size(); ++index) {
                const PropertyObjectMember& left_member =
                    (*left_object).members()[index];
                const PropertyObjectMember& right_member =
                    (*right_object).members()[index];
                if (registered_name(left_member.name) !=
                        registered_name(right_member.name) ||
                    left_member.description != right_member.description ||
                    left_member.writable != right_member.writable ||
                    left_member.declared_kind != right_member.declared_kind ||
                    left_member.nullable != right_member.nullable ||
                    left_member.enumeration != right_member.enumeration ||
                    left_member.standard_values != right_member.standard_values ||
                    left_member.standard_values_exclusive !=
                        right_member.standard_values_exclusive ||
                    left_member.converter_name != right_member.converter_name ||
                    left_member.editor_name != right_member.editor_name ||
                    !same_shape(left_member.value, right_member.value)) {
                    return false;
                }
            }
            return true;
        }
        const gui_forms::PropertyCollectionValue* left_collection =
            std::get_if<PropertyCollectionValue>(&left);
        const gui_forms::PropertyCollectionValue* right_collection =
            std::get_if<PropertyCollectionValue>(&right);
        if (left_collection || right_collection) {
            if (!left_collection || !right_collection || !*left_collection ||
                !*right_collection ||
                (*left_collection).item_type_name() !=
                    (*right_collection).item_type_name() ||
                (*left_collection).item_kind() != (*right_collection).item_kind()) {
                return false;
            }
            const std::span<const BindingValue> left_items = property_collection_items(*left_collection);
            const std::span<const BindingValue> right_items = property_collection_items(*right_collection);
            if (left_items.size() != right_items.size()) return false;
            for (std::size_t index = 0U; index < left_items.size(); ++index) {
                if (!same_shape(left_items[index], right_items[index])) return false;
            }
        }
        return true;
    }

    [[nodiscard]] static bool compatible_descriptor(
        const PropertyDescriptor& left,
        const PropertyDescriptor& right) noexcept {
        const bool enum_equal = (!left.enumeration && !right.enumeration) ||
            (left.enumeration && right.enumeration &&
             *left.enumeration == *right.enumeration);
        return left.kind == right.kind && left.nullable == right.nullable &&
            enum_equal && left.standard_values == right.standard_values &&
            left.standard_values_exclusive ==
                right.standard_values_exclusive &&
            left.converter_name == right.converter_name &&
            left.editor_name == right.editor_name &&
            left.readable == right.readable && left.browsable == right.browsable;
    }

    // A property path or category as stable ID segments: its registered
    // name in CSS spelling ("accessible-description", "settings.threshold"),
    // with any other character an application's category may hold (a space,
    // a bracket) as a single '-' between letters and digits.
    [[nodiscard]] static std::string id_segments(std::string_view name) {
        const std::string spelled = css_spelling(registered_name(name));
        std::string segments;
        segments.reserve(spelled.size());
        bool dash_pending = false;
        for (const char character : spelled) {
            const bool alphanumeric = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
            if (alphanumeric) {
                if (dash_pending && !segments.empty() && segments.back() != '.') segments += '-';
                dash_pending = false;
                segments += character;
            } else if (character == '.') {
                dash_pending = false;
                if (!segments.empty() && segments.back() != '.') segments += '.';
            } else {
                dash_pending = true;
            }
        }
        while (!segments.empty() && segments.back() == '.') segments.pop_back();
        return segments.empty() ? std::string("unnamed") : segments;
    }

    [[nodiscard]] std::string row_id(std::string_view property_name) const {
        return std::string(owner.stable_id().value()) + ".property." + id_segments(property_name);
    }

    [[nodiscard]] std::string group_id(std::string_view category) const {
        return std::string(owner.stable_id().value()) + ".category." + id_segments(category);
    }

    [[nodiscard]] std::string display_value(
        const BindingValue& value, const PropertyDescriptor& descriptor,
        bool) const {
        // An enumeration shows its choice names' display spelling, as its
        // choice list does.
        const bool enumerated = std::holds_alternative<PropertyEnumValue>(value);
        if (converters) {
            const std::string formatted = (*converters).format(value, descriptor);
            return enumerated ? display_label(formatted) : formatted;
        }
        if (const bool* boolean = std::get_if<bool>(&value)) {
            return *boolean ? "True" : "False";
        }
        if (const gui_forms::ImageId* image = std::get_if<ImageId>(&value)) {
            return std::to_string((*image).value);
        }
        if (const gui_forms::PropertyEnumValue* enumerated = std::get_if<PropertyEnumValue>(&value)) {
            return display_label((*enumerated).name);
        }
        return binding_value_to_string(value);
    }

    [[nodiscard]] static PropertyEditorKind editor_kind(
        const PropertyDescriptor& descriptor) noexcept {
        if (!descriptor.writable) return PropertyEditorKind::read_only;
        if (descriptor.nullable &&
            !descriptor.standard_values_exclusive) {
            return PropertyEditorKind::text;
        }
        switch (descriptor.kind) {
        case BindingValueKind::boolean:
            return PropertyEditorKind::boolean;
        case BindingValueKind::signed_integer:
        case BindingValueKind::unsigned_integer:
        case BindingValueKind::number:
        case BindingValueKind::text:
            return PropertyEditorKind::text;
        case BindingValueKind::enumeration:
            return descriptor.enumeration && !(*descriptor.enumeration).flags
                ? PropertyEditorKind::choice : PropertyEditorKind::text;
        case BindingValueKind::null:
        case BindingValueKind::point:
        case BindingValueKind::size:
        case BindingValueKind::rectangle:
        case BindingValueKind::insets:
        case BindingValueKind::color:
        case BindingValueKind::font:
        case BindingValueKind::image:
        case BindingValueKind::object:
        case BindingValueKind::collection:
            return PropertyEditorKind::read_only;
        }
        return PropertyEditorKind::read_only;
    }

    [[nodiscard]] static PropertyEditorKind editor_kind(
        BindingValueKind kind, bool writable) noexcept {
        if (!writable) return PropertyEditorKind::read_only;
        switch (kind) {
        case BindingValueKind::boolean:
            return PropertyEditorKind::boolean;
        case BindingValueKind::signed_integer:
        case BindingValueKind::unsigned_integer:
        case BindingValueKind::number:
        case BindingValueKind::text:
            return PropertyEditorKind::text;
        case BindingValueKind::null:
        case BindingValueKind::point:
        case BindingValueKind::size:
        case BindingValueKind::rectangle:
        case BindingValueKind::insets:
        case BindingValueKind::color:
        case BindingValueKind::font:
        case BindingValueKind::image:
        case BindingValueKind::enumeration:
        case BindingValueKind::object:
        case BindingValueKind::collection:
            return PropertyEditorKind::read_only;
        }
        return PropertyEditorKind::read_only;
    }

    [[nodiscard]] std::string description_for(
        const PropertyDescriptor& descriptor,
        PropertyValueOrigin origin) const {
        std::string result = descriptor.description;
        if (!result.empty()) result += " · ";
        result += "Origin: ";
        result += property_value_origin_name(origin);
        if (descriptor.resettable) result += " · Reset available";
        return result;
    }

    void clear_subscriptions() noexcept {
        property_subscriptions.clear();
    }

    [[nodiscard]] Control::Ptr target() const noexcept {
        if (selected.empty()) return {};
        const Control::Ptr result = selected.front().lock();
        return result && (*result).is_alive() ? result : Control::Ptr{};
    }

    [[nodiscard]] std::vector<Control::Ptr> targets() const {
        std::vector<Control::Ptr> result;
        result.reserve(selected.size());
        for (const Control::WeakPtr& candidate : selected) {
            Control::Ptr retained = candidate.lock();
            if (!retained || !(*retained).is_alive()) return {};
            result.push_back(std::move(retained));
        }
        return result;
    }

    void append_projected_rows(
        std::vector<PropertyRowSpec>& rows,
        const PropertyDescriptor& descriptor,
        const BindingValue& value,
        std::string path,
        std::string display_name,
        std::string description,
        std::string parent_id,
        std::uint8_t depth,
        bool writable,
        std::vector<PathSegment> segments,
        bool top_level = false,
        std::optional<PropertyEditorKind> authored_editor = {},
        std::vector<std::string> authored_choices = {},
        std::string owner_property_name = {}) {
        if (owner_property_name.empty()) owner_property_name = descriptor.name;
        const std::string canonical = registered_name(path);
        const std::string id = row_id(path);
        const bool expandable = expandable_value(value);
        const bool expanded = expanded_properties.contains(canonical);
        const bool resettable = top_level && descriptor.resettable;
        const Control::Ptr object = target();
        const bool reset_enabled = resettable && object &&
            (*object).should_serialize_property(descriptor.name);
        PropertyEditorKind editor = authored_editor.value_or(
            editor_kind(descriptor));
        if (!writable && editor != PropertyEditorKind::read_only) {
            editor = PropertyEditorKind::read_only;
        }
        if (authored_choices.empty() && descriptor.enumeration &&
            !(*descriptor.enumeration).flags) {
            authored_choices.reserve((*descriptor.enumeration).choices.size());
            for (const PropertyEnumChoice& choice :
                 (*descriptor.enumeration).choices) {
                authored_choices.push_back(display_label(choice.name));
            }
        }
        if (authored_choices.empty() &&
            descriptor.standard_values_exclusive) {
            authored_choices.reserve(descriptor.standard_values.size());
            for (const BindingValue& standard : descriptor.standard_values) {
                authored_choices.push_back(display_value(
                    standard, descriptor, top_level));
            }
            editor = writable ? PropertyEditorKind::choice
                              : PropertyEditorKind::read_only;
        }
        rows.emplace_back(
            id, std::move(display_name),
            display_value(value, descriptor, top_level),
            std::move(description), editor, std::move(authored_choices),
            std::string{}, writable || expandable, false,
            std::move(parent_id), depth, expandable, expanded,
            resettable, reset_enabled);
        property_to_row.insert_or_assign(canonical, id);
        row_to_property.insert_or_assign(id, path);
        edit_targets.insert_or_assign(
            canonical, EditTarget{owner_property_name, segments, writable,
                                  descriptor});
        values.insert_or_assign(canonical, value);

        if (!expandable || depth >= maximum_property_value_depth) return;
        for (const CompoundFieldSpec& field :
             compound_fields(binding_value_kind(value))) {
            const std::optional<BindingValue> field_value = compound_field_value(value, field.name);
            if (!field_value) continue;
            std::vector<PathSegment> child_segments = segments;
            child_segments.push_back(
                {PathSegmentKind::compound_field, std::string(field.name), 0U});
            const std::string child_path = append_member_path(path, field.name);
            PropertyDescriptor child_descriptor;
            child_descriptor.name = std::string(field.name);
            child_descriptor.kind = field.kind;
            child_descriptor.category = descriptor.category;
            child_descriptor.description =
                display_spelling(field.name) + " component of " + display_spelling(path);
            child_descriptor.writable = writable;
            append_projected_rows(
                rows, child_descriptor, *field_value, child_path,
                display_spelling(field.name),
                child_descriptor.description,
                id, static_cast<std::uint8_t>(depth + 1U), writable,
                std::move(child_segments), false, field.editor, field.choices,
                owner_property_name);
        }
        if (const gui_forms::PropertyObjectValue* nested_object =
                std::get_if<PropertyObjectValue>(&value);
            nested_object && *nested_object) {
            for (const PropertyObjectMember& member : (*nested_object).members()) {
                std::vector<PathSegment> child_segments = segments;
                child_segments.push_back(
                    {PathSegmentKind::object_member, member.name, 0U});
                const std::string child_path = append_member_path(path, member.name);
                PropertyDescriptor member_descriptor;
                member_descriptor.name = member.name;
                member_descriptor.kind = member.declared_kind.value_or(
                    binding_value_kind(member.value));
                member_descriptor.category = descriptor.category;
                member_descriptor.description = member.description;
                member_descriptor.writable = writable && member.writable;
                member_descriptor.nullable = member.nullable;
                member_descriptor.enumeration = member.enumeration;
                member_descriptor.standard_values = member.standard_values;
                member_descriptor.standard_values_exclusive =
                    member.standard_values_exclusive;
                member_descriptor.converter_name = member.converter_name;
                member_descriptor.editor_name = member.editor_name;
                append_projected_rows(
                    rows, member_descriptor, member.value, child_path,
                    display_spelling(member.name),
                    member.description.empty()
                        ? display_spelling(member.name) + " member of " +
                              std::string((*nested_object).type_name())
                        : member.description,
                    id, static_cast<std::uint8_t>(depth + 1U),
                    writable && member.writable, std::move(child_segments),
                    false, {}, {}, owner_property_name);
            }
        }
        if (const gui_forms::PropertyCollectionValue* collection =
                std::get_if<PropertyCollectionValue>(&value);
            collection && *collection) {
            const std::span<const BindingValue> items = property_collection_items(*collection);
            for (std::size_t index = 0U; index < items.size(); ++index) {
                std::vector<PathSegment> child_segments = segments;
                child_segments.push_back(
                    {PathSegmentKind::collection_index, {}, index});
                const std::string child_path = append_index_path(path, index);
                PropertyDescriptor item_descriptor;
                item_descriptor.name = "[" + std::to_string(index) + "]";
                item_descriptor.kind = (*collection).item_kind();
                item_descriptor.category = descriptor.category;
                item_descriptor.description =
                    "Item " + std::to_string(index) + " of " +
                    std::string((*collection).item_type_name());
                item_descriptor.writable = writable;
                append_projected_rows(
                    rows, item_descriptor, items[index], child_path,
                    "[" + std::to_string(index) + "]",
                    item_descriptor.description,
                    id, static_cast<std::uint8_t>(depth + 1U), writable,
                    std::move(child_segments), false, {}, {},
                    owner_property_name);
            }
        }
    }

    void rebuild() {
        if (!list) return;
        ++projection_revision;
        clear_subscriptions();
        installed_editors.clear();
        descriptors.clear();
        property_to_row.clear();
        row_to_property.clear();
        edit_targets.clear();
        values.clear();
        last_error.reset();

        const Control::Ptr object = target();
        if (!object) {
            selected.clear();
            (*list).set_groups({});
            (*list).accessible_name = "Properties";
            return;
        }

        std::vector<PropertyDescriptor> visible;
        for (PropertyDescriptor descriptor : (*object).property_descriptors()) {
            if (!descriptor.browsable || !descriptor.readable) continue;
            bool common = true;
            for (const Control::Ptr& peer : targets()) {
                if (peer.get() == object.get()) continue;
                const std::optional<PropertyDescriptor> peer_descriptor = (*peer).property_descriptor(
                    descriptor.name);
                const std::optional<BindingValue> left_value = (*object).property_value(descriptor.name);
                const std::optional<BindingValue> right_value = (*peer).property_value(descriptor.name);
                if (!peer_descriptor ||
                    !compatible_descriptor(descriptor, *peer_descriptor) ||
                    !left_value || !right_value ||
                    !same_shape(*left_value, *right_value)) {
                    common = false;
                    break;
                }
                descriptor.writable = descriptor.writable &&
                    (*peer_descriptor).writable;
            }
            if (!common) continue;
            visible.push_back(std::move(descriptor));
        }
        std::sort(visible.begin(), visible.end(), DescriptorLess{sort});

        std::map<std::string, PropertyGroupSpec> categorized;
        PropertyGroupSpec alphabetical;
        alphabetical.stable_id = group_id("properties");
        alphabetical.title = "PROPERTIES";
        for (const PropertyDescriptor& descriptor : visible) {
            const std::optional<BindingValue> value = (*object).property_value(descriptor.name);
            if (!value) continue;
            const std::string canonical = registered_name(descriptor.name);
            const PropertyValueOrigin origin =
                (*object).property_value_origin(descriptor.name);
            std::vector<PropertyRowSpec> projected_rows;
            append_projected_rows(
                projected_rows, descriptor, *value, descriptor.name,
                display_spelling(descriptor.name),
                description_for(descriptor, origin), {}, 0U,
                descriptor.writable, {}, true);
            if (sort == PropertySort::alphabetical) {
                alphabetical.rows.insert(alphabetical.rows.end(),
                    std::make_move_iterator(projected_rows.begin()),
                    std::make_move_iterator(projected_rows.end()));
            } else {
                const std::string category = descriptor.category.empty()
                    ? std::string("Misc") : descriptor.category;
                std::pair<
                    std::map<std::string, PropertyGroupSpec>::iterator, bool>
                    insertion = categorized.try_emplace(category);
                std::map<std::string, PropertyGroupSpec>::iterator position =
                    insertion.first;
                const bool inserted = insertion.second;
                if (inserted) {
                    (*position).second.stable_id = group_id(category);
                    (*position).second.title = category;
                }
                (*position).second.rows.insert((*position).second.rows.end(),
                    std::make_move_iterator(projected_rows.begin()),
                    std::make_move_iterator(projected_rows.end()));
            }
            descriptors.emplace(canonical, descriptor);
        }

        std::vector<PropertyGroupSpec> groups;
        if (sort == PropertySort::alphabetical) {
            if (!alphabetical.rows.empty()) {
                groups.push_back(std::move(alphabetical));
            }
        } else {
            groups.reserve(categorized.size());
            for (std::pair<const std::string, PropertyGroupSpec>& category_entry :
                 categorized) {
                PropertyGroupSpec& group = category_entry.second;
                groups.push_back(std::move(group));
            }
        }
        (*list).set_groups(std::move(groups));
        (*list).accessible_name = std::string((*object).stable_id().value()) + " properties";

        install_custom_editors();

        for (const std::pair<const std::string, PropertyDescriptor>&
                 descriptor_entry : descriptors) {
            const std::string& canonical = descriptor_entry.first;
            const PropertyDescriptor& descriptor = descriptor_entry.second;
            if (!descriptor.change_notifications) continue;
            SubscriptionToken subscription = (*object).subscribe_property_changed(
                descriptor.name, owner, PropertyRefresh{this, canonical});
            if (subscription.connected()) {
                property_subscriptions.push_back(std::move(subscription));
            }
        }
    }

    void install_custom_editors() {
        if (!list || !editors) return;
        for (const std::pair<const std::string, EditTarget>& target_entry :
             edit_targets) {
            const std::string& canonical = target_entry.first;
            const EditTarget& target_spec = target_entry.second;
            if (!target_spec.writable) continue;
            const ValueMap::iterator value = values.find(canonical);
            const StringMap::iterator row = property_to_row.find(canonical);
            if (value == values.end() || row == property_to_row.end()) {
                continue;
            }
            PropertyEditorRequest request;
            request.stable_id = (*row).second + ".custom-editor";
            request.property_path = row_to_property.at((*row).second);
            request.descriptor = target_spec.descriptor;
            request.value = (*value).second;
            request.writable = target_spec.writable;
            request.top_level = target_spec.path.empty();
            try {
                std::optional<PropertyEditorBinding> binding = (*editors).create(request);
                if (!binding) continue;
                const Control::WeakPtr owner_lifetime = owner.weak_from_this();
                SubscriptionToken committed = (*binding).connect_committed(
                    owner, CustomEditorCommit{
                        this, owner_lifetime, request.property_path});
                if (!committed.connected()) {
                    throw std::invalid_argument(
                        "Property editor factory returned no commit subscription");
                }
                SubscriptionToken failure;
                if ((*binding).connect_failed) {
                    failure = (*binding).connect_failed(
                        owner, CustomEditorFailure{
                            this, owner_lifetime, request.property_path});
                    if (!failure.connected()) {
                        throw std::invalid_argument(
                            "Property editor factory returned no failure subscription");
                    }
                }
                if (!(*list).replace_editor((*row).second, (*binding).control)) {
                    throw std::logic_error(
                        "Property editor row disappeared during installation");
                }
                installed_editors.insert_or_assign(
                    canonical,
                    InstalledEditor{std::move(*binding), std::move(committed),
                                    std::move(failure)});
            } catch (const std::exception& error) {
                report_error(request.property_path, "<editor>", error.what());
            }
        }
    }

    void refresh_one(std::string_view canonical_name, bool publish) {
        const Control::Ptr object = target();
        if (!object) {
            rebuild();
            return;
        }
        const std::string canonical = registered_name(canonical_name);
        const DescriptorMap::iterator descriptor = descriptors.find(canonical);
        const StringMap::iterator row = property_to_row.find(canonical);
        if (descriptor == descriptors.end() || row == property_to_row.end()) {
            rebuild();
            return;
        }
        const std::optional<BindingValue> current = (*object).property_value((*descriptor).second.name);
        if (!current) return;
        const BindingValue previous = values.contains(canonical)
            ? values.at(canonical) : *current;
        const PropertyValueOrigin origin =
            (*object).property_value_origin((*descriptor).second.name);
        const std::string authored_name = (*descriptor).second.name;
        if (!same_shape(previous, *current)) {
            rebuild();
        } else {
            for (const std::pair<const std::string, EditTarget>& target_entry :
                 edit_targets) {
                const std::string& path_name = target_entry.first;
                const EditTarget& target_spec = target_entry.second;
                if (registered_name(target_spec.property_name) != canonical) {
                    continue;
                }
                const std::optional<BindingValue> path_value = value_at(*current, target_spec.path);
                const StringMap::iterator path_row =
                    property_to_row.find(path_name);
                if (!path_value || path_row == property_to_row.end()) {
                    rebuild();
                    break;
                }
                values.insert_or_assign(path_name, *path_value);
                static_cast<void>((*list).set_value(
                    (*path_row).second,
                    display_value(*path_value, target_spec.descriptor,
                                  target_spec.path.empty())));
                if (const InstalledEditorMap::iterator installed =
                        installed_editors.find(path_name);
                    installed != installed_editors.end()) {
                    (*installed).second.binding.synchronize(*path_value);
                }
                static_cast<void>((*list).set_validation((*path_row).second, {}));
            }
            if (const StringMap::iterator top_row =
                    property_to_row.find(canonical);
                top_row != property_to_row.end()) {
                static_cast<void>((*list).set_description(
                    (*top_row).second,
                    description_for((*descriptor).second, origin)));
                if ((*descriptor).second.resettable) {
                    static_cast<void>((*list).set_reset_enabled(
                        (*top_row).second,
                        (*object).should_serialize_property(
                            (*descriptor).second.name)));
                }
            }
        }
        if (publish && previous != *current) {
            PropertyGridValueChange change{
                authored_name, previous, *current, origin, false};
            owner.publish_change(owner.property_value_changed_, change);
        }
    }

    void report_error(std::string property_name, std::string attempted,
                      std::string message) {
        // The error names the registered path, whichever spelling the caller
        // used (owner decision 8).
        last_error = PropertyGridEditError{
            registered_name(property_name), std::move(attempted), std::move(message)};
        const std::string canonical =
            registered_name((*last_error).property_name);
        if (const StringMap::iterator row = property_to_row.find(canonical);
            row != property_to_row.end()) {
            static_cast<void>((*list).set_validation((*row).second,
                                                   (*last_error).message));
        }
        // A copy: a handler that selects another object resets last_error,
        // which ends the error's lifetime while later handlers still read it.
        const PropertyGridEditError failure = *last_error;
        owner.edit_failed_.emit(failure);
    }

    bool set_value(std::string_view property_name, BindingValue value,
                   bool reset, bool structural = false) {
        const Control::Ptr object = target();
        const std::vector<Control::Ptr> objects = targets();
        if (!object || objects.empty()) return false;
        const std::string canonical = registered_name(property_name);
        const EditTargetMap::iterator edit_target = edit_targets.find(canonical);
        if (edit_target == edit_targets.end()) {
            report_error(std::string(property_name),
                         binding_value_to_string(value),
                         "Property is not available for editing");
            return false;
        }
        const std::string property_canonical = registered_name(
            (*edit_target).second.property_name);
        const DescriptorMap::iterator descriptor =
            descriptors.find(property_canonical);
        if (descriptor == descriptors.end()) return false;
        const EditTarget target_spec = (*edit_target).second;
        const PropertyDescriptor descriptor_spec = (*descriptor).second;
        const std::uint64_t expected_projection = projection_revision;
        const std::string authored_path = registered_name(property_name);
        if (!descriptor_spec.writable || !target_spec.writable ||
            (reset && !target_spec.path.empty())) {
            report_error(authored_path, binding_value_to_string(value),
                         reset ? "Only the owning property can be reset"
                               : "Property is not available for editing");
            return false;
        }
        struct OwnerEdit final {
            Control::Ptr object;
            PropertyDescriptor descriptor;
            BindingValue previous_property;
            BindingValue previous_value;
            BindingValue next_property;
        };
        std::vector<OwnerEdit> edits;
        edits.reserve(objects.size());
        try {
            for (const Control::Ptr& candidate : objects) {
                const std::optional<PropertyDescriptor> candidate_descriptor = (*candidate).property_descriptor(
                    descriptor_spec.name);
                const std::optional<BindingValue> previous_property = (*candidate).property_value(
                    descriptor_spec.name);
                if (!candidate_descriptor || !(*candidate_descriptor).writable ||
                    !previous_property ||
                    !compatible_descriptor(descriptor_spec,
                                           *candidate_descriptor)) {
                    throw std::invalid_argument(
                        "Every selected owner must expose the same writable property schema");
                }
                const std::optional<BindingValue> previous_value = value_at(
                    *previous_property, target_spec.path);
                if (!previous_value) {
                    throw std::invalid_argument(
                        "Every selected owner must expose the same property path");
                }
                BindingValue next_property = *previous_property;
                if (reset) {
                    if (!(*candidate_descriptor).resettable) {
                        throw std::invalid_argument(
                            "Every selected owner must expose the same reset contract");
                    }
                } else {
                    const std::optional<BindingValue> converted = convert_property_value(
                        value, target_spec.descriptor);
                    const std::optional<BindingValue> next = converted
                        ? (target_spec.path.empty()
                               ? std::optional<BindingValue>{*converted}
                               : replace_at(*previous_property,
                                            target_spec.path, *converted))
                        : std::optional<BindingValue>{};
                    if (!next) {
                        throw std::invalid_argument(
                            "Property path value cannot convert to its declared kind");
                    }
                    const std::optional<BindingValue> owner_normalized = convert_property_value(
                        *next, *candidate_descriptor);
                    if (!owner_normalized) {
                        throw std::invalid_argument(
                            "Property owner rejected the preflight value schema");
                    }
                    next_property = *owner_normalized;
                }
                edits.push_back({candidate, *candidate_descriptor,
                                 *previous_property, *previous_value,
                                 std::move(next_property)});
            }
        } catch (const std::exception& error) {
            report_error(authored_path,
                         reset ? std::string("<reset>")
                               : binding_value_to_string(value),
                         error.what());
            return false;
        }
        const BindingValue previous_value = edits.front().previous_value;
        committing = true;
        struct CommitReset final {
            bool& flag;
            ~CommitReset() { flag = false; }
        } commit_reset{committing};
        try {
            std::size_t applied = 0U;
            try {
                for (; applied < edits.size(); ++applied) {
                    if (reset) {
                        if (!(*edits[applied].object).reset_property(
                                edits[applied].descriptor.name)) {
                            throw std::invalid_argument(
                                "A selected owner rejected its reset contract");
                        }
                    } else {
                        (*edits[applied].object).set_property_value(
                            edits[applied].descriptor.name,
                            edits[applied].next_property);
                    }
                }
            } catch (...) {
                bool rollback_failed = false;
                const std::size_t rollback_count =
                    std::min(applied + 1U, edits.size());
                for (std::size_t index = rollback_count; index > 0U; --index) {
                    try {
                        (*edits[index - 1U].object).set_property_value(
                            edits[index - 1U].descriptor.name,
                            edits[index - 1U].previous_property);
                    } catch (...) {
                        rollback_failed = true;
                    }
                }
                if (rollback_failed) {
                    throw std::runtime_error(
                        "Atomic property commit failed and a hostile setter also rejected rollback");
                }
                throw;
            }
            if (!owner.is_alive() || !(*object).is_alive()) return true;
            if (projection_revision != expected_projection ||
                target().get() != object.get()) {
                return true;
            }
            const std::optional<BindingValue> current_property = (*object).property_value(
                descriptor_spec.name);
            if (!current_property) return false;
            const std::optional<BindingValue> current_value = value_at(*current_property,
                                                target_spec.path);
            if (!current_value) return false;
            if (structural) rebuild();
            else refresh_one(property_canonical, false);
            last_error.reset();
            if (*current_value != previous_value || reset) {
                const PropertyValueOrigin origin =
                    (*object).property_value_origin(descriptor_spec.name);
                PropertyGridValueChange change{
                    authored_path, previous_value, *current_value,
                    origin, reset};
                owner.publish_change(owner.property_value_changed_, change);
            }
            return true;
        } catch (const std::exception& error) {
            if (!owner.is_alive()) return false;
            if (projection_revision != expected_projection ||
                target().get() != object.get()) {
                return false;
            }
            if ((*object).is_alive()) {
                refresh_one(property_canonical, false);
            }
            report_error(authored_path,
                         reset ? std::string("<reset>")
                               : binding_value_to_string(value),
                         error.what());
            return false;
        }
    }

    [[nodiscard]] std::optional<PropertyCollectionValue> collection_at_path(
        std::string_view property_name) {
        const Control::Ptr object = target();
        if (!object) return {};
        const std::string canonical = registered_name(property_name);
        const EditTargetMap::iterator target_entry =
            edit_targets.find(canonical);
        if (target_entry == edit_targets.end()) return {};
        const std::optional<BindingValue> property = (*object).property_value(
            (*target_entry).second.property_name);
        if (!property) return {};
        const std::optional<BindingValue> current = value_at(*property, (*target_entry).second.path);
        if (!current) return {};
        const gui_forms::PropertyCollectionValue* collection = std::get_if<PropertyCollectionValue>(&*current);
        return collection && *collection
            ? std::optional<PropertyCollectionValue>{*collection}
            : std::optional<PropertyCollectionValue>{};
    }

    bool insert_collection_item(std::string_view property_name,
                                std::size_t index, BindingValue value) {
        const std::optional<PropertyCollectionValue> collection = collection_at_path(property_name);
        if (!collection) {
            report_error(std::string(property_name), binding_value_to_string(value),
                         "Property path is not a collection");
            return false;
        }
        const std::span<const BindingValue> current = property_collection_items(*collection);
        if (index > current.size() ||
            current.size() >= maximum_property_collection_items) {
            report_error(std::string(property_name), binding_value_to_string(value),
                         "Collection insertion index or capacity is invalid");
            return false;
        }
        try {
            std::vector<BindingValue> items(current.begin(), current.end());
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(index),
                         std::move(value));
            return set_value(
                property_name,
                BindingValue{make_property_collection(
                    std::string((*collection).item_type_name()),
                    (*collection).item_kind(), std::move(items))},
                false, true);
        } catch (const std::exception& error) {
            report_error(std::string(property_name), "<insert>", error.what());
            return false;
        }
    }

    bool remove_collection_item(std::string_view property_name,
                                std::size_t index) {
        const std::optional<PropertyCollectionValue> collection = collection_at_path(property_name);
        if (!collection) {
            report_error(std::string(property_name), "<remove>",
                         "Property path is not a collection");
            return false;
        }
        const std::span<const BindingValue> current = property_collection_items(*collection);
        if (index >= current.size()) {
            report_error(std::string(property_name), "<remove>",
                         "Collection removal index is invalid");
            return false;
        }
        std::vector<BindingValue> items(current.begin(), current.end());
        items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
        return set_value(
            property_name,
            BindingValue{make_property_collection(
                std::string((*collection).item_type_name()),
                (*collection).item_kind(), std::move(items))},
            false, true);
    }

    bool move_collection_item(std::string_view property_name,
                              std::size_t from, std::size_t to) {
        const std::optional<PropertyCollectionValue> collection = collection_at_path(property_name);
        if (!collection) {
            report_error(std::string(property_name), "<move>",
                         "Property path is not a collection");
            return false;
        }
        const std::span<const BindingValue> current = property_collection_items(*collection);
        if (from >= current.size() || to >= current.size()) {
            report_error(std::string(property_name), "<move>",
                         "Collection move index is invalid");
            return false;
        }
        if (from == to) return true;
        std::vector<BindingValue> items(current.begin(), current.end());
        BindingValue moving = std::move(items[from]);
        items.erase(items.begin() + static_cast<std::ptrdiff_t>(from));
        items.insert(items.begin() + static_cast<std::ptrdiff_t>(to),
                     std::move(moving));
        return set_value(
            property_name,
            BindingValue{make_property_collection(
                std::string((*collection).item_type_name()),
                (*collection).item_kind(), std::move(items))},
            false, true);
    }

    void commit_row(const PropertyValueChange& change) {
        const StringMap::iterator property =
            row_to_property.find(change.row_id);
        if (property == row_to_property.end()) return;
        const std::string canonical = registered_name((*property).second);
        const EditTargetMap::iterator target_spec =
            edit_targets.find(canonical);
        const ValueMap::iterator current = values.find(canonical);
        if (target_spec == edit_targets.end() || current == values.end()) return;
        const PropertyDescriptor& effective = (*target_spec).second.descriptor;
        const std::optional<BindingValue> converted = converters
            ? (*converters).parse(change.current_value, (*current).second, effective)
            : convert_property_value(BindingValue{change.current_value},
                                     effective);
        if (!converted) {
            report_error((*property).second, change.current_value,
                         "Property text cannot convert through its declared converter");
            refresh_one(registered_name(
                (*target_spec).second.property_name), false);
            return;
        }
        static_cast<void>(set_value((*property).second, *converted, false));
    }

    PropertyGrid& owner;
    std::shared_ptr<PropertyList> list;
    std::shared_ptr<PropertyValueConverterRegistry> converters;
    std::shared_ptr<PropertyEditorRegistry> editors;
    std::vector<Control::WeakPtr> selected;
    PropertySort sort{PropertySort::categorized};
    DescriptorMap descriptors;
    StringMap property_to_row;
    StringMap row_to_property;
    EditTargetMap edit_targets;
    ValueMap values;
    std::unordered_set<std::string> expanded_properties;
    std::vector<SubscriptionToken> property_subscriptions;
    InstalledEditorMap installed_editors;
    SubscriptionToken list_commit;
    SubscriptionToken list_expansion;
    SubscriptionToken list_reset;
    std::optional<PropertyGridEditError> last_error;
    std::uint64_t projection_revision{};
    bool committing{};
};

namespace {

std::shared_ptr<const PropertyEnumDescriptor> property_sort_enum() {
    static const std::shared_ptr<const PropertyEnumDescriptor> value =
        std::make_shared<const PropertyEnumDescriptor>(PropertyEnumDescriptor{
            "property_sort", {{"categorized", 0}, {"alphabetical", 1}}, false});
    return value;
}

} // namespace

PropertyGrid::PropertyGrid(StableId stable_id)
    : Panel(std::move(stable_id)), impl_(std::make_unique<Impl>(*this)) {
    set_background(Color::rgba(244, 247, 251));
    border_style = BorderStyle::line;
    set_paint_plane(PaintPlane::control);

}

PropertyGrid::~PropertyGrid() = default;

void PropertyGrid::initialize_control_tree() {
    require_mutable();
    if ((*impl_).list) return;
    (*impl_).list = make_control<PropertyList>(
        StableId(std::string(stable_id().value()) + ".list"));
    (*(*impl_).list).label_width = 112.0;
    add_child((*impl_).list);
    (*impl_).list_commit = (*(*impl_).list).value_committed().subscribe(
        *this, Impl::ListCommit{impl_.get()});
    (*impl_).list_expansion = (*(*impl_).list).row_expansion_changed().subscribe(
        *this, Impl::ListExpansion{impl_.get()});
    (*impl_).list_reset = (*(*impl_).list).reset_requested().subscribe(
        *this, Impl::ListReset{impl_.get()});
}

Control::Ptr PropertyGrid::selected_object() const noexcept {
    return (*impl_).target();
}

void PropertyGrid::set_selected_object(Control::Ptr object) {
    set_selected_objects(object ? std::vector<Control::Ptr>{std::move(object)}
                                : std::vector<Control::Ptr>{});
}

std::vector<Control::Ptr> PropertyGrid::selected_objects() const {
    return (*impl_).targets();
}

void PropertyGrid::set_selected_objects(std::vector<Control::Ptr> objects) {
    require_mutable();
    if (!(*impl_).list) initialize_control_tree();
    std::unordered_set<Control*> identities;
    for (const Control::Ptr& object : objects) {
        if (!object || !(*object).is_alive() ||
            !identities.insert(object.get()).second) {
            throw std::invalid_argument(
                "PropertyGrid selection requires unique live controls");
        }
    }
    const std::vector<Control::Ptr> previous = (*impl_).targets();
    if (previous == objects) {
        refresh_properties();
        return;
    }
    (*impl_).selected.clear();
    (*impl_).selected.reserve(objects.size());
    for (const Control::Ptr& object : objects) {
        (*impl_).selected.push_back(object);
    }
    (*impl_).rebuild();
    publish_change(selected_object_changed_,
                   objects.empty() ? Control::Ptr{} : objects.front());
}

PropertySort PropertyGrid::read_view(const decltype(property_sort)&) const {
    return (*impl_).sort;
}

void PropertyGrid::write_view(const decltype(property_sort)&, PropertySort incoming) {
    require_mutable();
    if (incoming != PropertySort::categorized &&
        incoming != PropertySort::alphabetical) {
        throw std::invalid_argument("PropertyGrid sort mode is invalid");
    }
    if ((*impl_).sort == incoming) return;
    (*impl_).sort = incoming;
    if (!(*impl_).list) initialize_control_tree();
    (*impl_).rebuild();
}

void PropertyGrid::refresh_properties() {
    require_mutable();
    if (!(*impl_).list) initialize_control_tree();
    (*impl_).rebuild();
}

std::shared_ptr<PropertyList> PropertyGrid::property_list() const noexcept {
    return (*impl_).list;
}

std::shared_ptr<PropertyValueConverterRegistry> PropertyGrid::read_view(const decltype(converter_registry)&) const {
    return (*impl_).converters;
}

void PropertyGrid::write_view(const decltype(converter_registry)&, std::shared_ptr<PropertyValueConverterRegistry> incoming) {
    require_mutable();
    if (!incoming) {
        throw std::invalid_argument("PropertyGrid requires a converter registry");
    }
    if ((*impl_).converters == incoming) return;
    (*impl_).converters = std::move(incoming);
    if (!(*impl_).list) initialize_control_tree();
    (*impl_).rebuild();
}

std::shared_ptr<PropertyEditorRegistry> PropertyGrid::read_view(const decltype(editor_registry)&) const {
    return (*impl_).editors;
}

void PropertyGrid::write_view(const decltype(editor_registry)&, std::shared_ptr<PropertyEditorRegistry> incoming) {
    require_mutable();
    if (!incoming) {
        throw std::invalid_argument("PropertyGrid requires an editor registry");
    }
    if ((*impl_).editors == incoming) return;
    (*impl_).editors = std::move(incoming);
    if (!(*impl_).list) initialize_control_tree();
    (*impl_).rebuild();
}

Control::Ptr PropertyGrid::editor(std::string_view property_name) const {
    if (!(*impl_).list) return {};
    const Impl::StringMap::iterator found = (*impl_).property_to_row.find(
        registered_name(property_name));
    return found == (*impl_).property_to_row.end()
        ? Control::Ptr{} : (*(*impl_).list).editor((*found).second);
}

std::shared_ptr<Button> PropertyGrid::reset_button(
    std::string_view property_name) const {
    if (!(*impl_).list) return {};
    const Impl::StringMap::iterator found = (*impl_).property_to_row.find(
        registered_name(property_name));
    return found == (*impl_).property_to_row.end()
        ? std::shared_ptr<Button>{}
        : (*(*impl_).list).reset_button((*found).second);
}

std::optional<PropertyDescriptor> PropertyGrid::selected_descriptor(
    std::string_view property_name) const {
    const std::string canonical = registered_name(property_name);
    const Impl::EditTargetMap::iterator target =
        (*impl_).edit_targets.find(canonical);
    return target == (*impl_).edit_targets.end()
        ? std::optional<PropertyDescriptor>{}
        : std::optional<PropertyDescriptor>{(*target).second.descriptor};
}

std::optional<PropertyValueOrigin> PropertyGrid::selected_origin(
    std::string_view property_name) const {
    const Control::Ptr selected = selected_object();
    if (!selected) return {};
    const Impl::EditTargetMap::iterator target = (*impl_).edit_targets.find(
        registered_name(property_name));
    return target != (*impl_).edit_targets.end()
        ? std::optional<PropertyValueOrigin>{
              (*selected).property_value_origin((*target).second.property_name)}
        : std::optional<PropertyValueOrigin>{};
}

bool PropertyGrid::set_property_expanded(std::string_view property_name,
                                         bool expanded) {
    require_mutable();
    if (!(*impl_).list) initialize_control_tree();
    const std::string canonical = registered_name(property_name);
    const Impl::StringMap::iterator found =
        (*impl_).property_to_row.find(canonical);
    const Impl::EditTargetMap::iterator target =
        (*impl_).edit_targets.find(canonical);
    if (found == (*impl_).property_to_row.end() ||
        target == (*impl_).edit_targets.end()) {
        return false;
    }
    return (*(*impl_).list).set_row_expanded((*found).second, expanded);
}

std::optional<bool> PropertyGrid::property_expanded(
    std::string_view property_name) const {
    if (!(*impl_).list) return {};
    const std::string canonical = registered_name(property_name);
    const Impl::StringMap::iterator found =
        (*impl_).property_to_row.find(canonical);
    return found == (*impl_).property_to_row.end()
        ? std::optional<bool>{}
        : (*(*impl_).list).row_expanded((*found).second);
}

bool PropertyGrid::try_set_property_value(std::string_view property_name,
                                          BindingValue value) {
    require_mutable();
    return (*impl_).set_value(property_name, std::move(value), false);
}

bool PropertyGrid::try_set_property_text(std::string_view property_name,
                                         std::string_view text) {
    require_mutable();
    if (!(*impl_).list) initialize_control_tree();
    const std::string canonical = registered_name(property_name);
    const Impl::EditTargetMap::iterator target =
        (*impl_).edit_targets.find(canonical);
    const Impl::ValueMap::iterator current = (*impl_).values.find(canonical);
    if (target == (*impl_).edit_targets.end() || current == (*impl_).values.end()) {
        (*impl_).report_error(std::string(property_name), std::string(text),
                            "Property is not available for text editing");
        return false;
    }
    const std::optional<BindingValue> parsed = (*(*impl_).converters).parse(
        text, (*current).second, (*target).second.descriptor);
    if (!parsed) {
        (*impl_).report_error(std::string(property_name), std::string(text),
                            "Property converter rejected the submitted text");
        return false;
    }
    return (*impl_).set_value(property_name, *parsed, false);
}

bool PropertyGrid::activate_property_editor(std::string_view property_name) {
    require_mutable();
    const Control::Ptr retained = editor(property_name);
    return retained && (*retained).is_alive() && (*retained).enabled.read() &&
        (*retained).on_semantic_action(SemanticAction::press, {});
}

bool PropertyGrid::reset_property(std::string_view property_name) {
    require_mutable();
    return (*impl_).set_value(property_name, BindingValue{}, true);
}

bool PropertyGrid::insert_collection_item(std::string_view property_name,
                                          std::size_t index,
                                          BindingValue value) {
    require_mutable();
    return (*impl_).insert_collection_item(property_name, index,
                                         std::move(value));
}

bool PropertyGrid::remove_collection_item(std::string_view property_name,
                                          std::size_t index) {
    require_mutable();
    return (*impl_).remove_collection_item(property_name, index);
}

bool PropertyGrid::move_collection_item(std::string_view property_name,
                                        std::size_t from, std::size_t to) {
    require_mutable();
    return (*impl_).move_collection_item(property_name, from, to);
}

std::optional<PropertyGridEditError> PropertyGrid::last_error() const {
    return (*impl_).last_error;
}

Size PropertyGrid::measure(Size available) {
    return available;
}

void PropertyGrid::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    if ((*impl_).list) {
        set_child_layout((*impl_).list,
            {0.0, 0.0, std::max(0.0, final_bounds.width),
             std::max(0.0, final_bounds.height)});
    }
}

SemanticDescriptor PropertyGrid::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.exposed = false;
    descriptor.include_descendants = true;
    return descriptor;
}

void PropertyGrid::on_dispose() noexcept {
    (*impl_).clear_subscriptions();
    (*impl_).installed_editors.clear();
    (*impl_).list_commit.disconnect();
    (*impl_).list_expansion.disconnect();
    (*impl_).list_reset.disconnect();
    (*impl_).selected.clear();
    (*impl_).list.reset();
    Control::on_dispose();
}

namespace {

// Its rows, modules and toolbars are child controls with their own anatomies.
constexpr ControlAnatomy property_grid_anatomy{
    .control_id = "property_grid",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& PropertyGrid::anatomy() const noexcept {
    return property_grid_anatomy;
}

namespace {

// The events PropertyGrid adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 3> property_grid_interactions{{
    {.name = "selected_object_changed", .payload = "Control::Ptr", .mutable_payload = false},
    {.name = "property_value_changed", .payload = "PropertyGridValueChange", .mutable_payload = false},
    {.name = "edit_failed", .payload = "PropertyGridEditError", .mutable_payload = false},
}};

} // namespace

void PropertyGrid::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), property_grid_interactions.begin(), property_grid_interactions.end());
}

// property_codemod begin PropertyGrid (tools/property_families/property_grid.json; generated, do not edit)

namespace {

PropertyDescriptor property_grid_property_sort_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "property_sort";
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.category = "Behavior";
    descriptor.description = "Properties grouped by category, or alphabetical.";
    descriptor.default_value = BindingValue{PropertyEnumValue{"property_sort", "categorized", 0}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint;
    descriptor.bindable = false;
    descriptor.enumeration = property_sort_enum();
    return descriptor;
}

const PropertyDescriptor& describe_property_grid_property_sort() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        property_grid_property_sort_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

PropertyType PropertyValueTraits<PropertySort>::type() {
    return PropertyType{BindingValueKind::enumeration, false, property_sort_enum()};
}

BindingValue PropertyValueTraits<PropertySort>::to_binding(PropertySort value) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = property_sort_enum();
    const std::optional<BindingValue> converted =
        convert_property_value(BindingValue{static_cast<std::int64_t>(value)}, descriptor);
    if (!converted) throw std::logic_error("GUI.Forms enumeration value is outside its property schema");
    return *converted;
}

PropertySort PropertyValueTraits<PropertySort>::from_binding(const BindingValue& value) {
    const PropertyEnumValue& choice = std::get<PropertyEnumValue>(value);
    return static_cast<PropertySort>(choice.value);
}

const std::array<PropertyEntry, PropertyGrid::property_grid_property_count>& PropertyGrid::property_grid_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 1U> own{{
        {"property_sort", &describe_property_grid_property_sort,
         &read_cell_entry<PropertyGrid, decltype(&PropertyGrid::property_sort), &PropertyGrid::property_sort>,
         &write_cell_entry<PropertyGrid, decltype(&PropertyGrid::property_sort), &PropertyGrid::property_sort>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "PropertyGrid's property table names a property twice");
    static const std::array<PropertyEntry, property_grid_property_count> entries =
        join_property_tables(Panel::panel_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> PropertyGrid::property_table() const noexcept {
    const std::array<PropertyEntry, property_grid_property_count>& entries = property_grid_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end PropertyGrid

} // namespace gui_forms
