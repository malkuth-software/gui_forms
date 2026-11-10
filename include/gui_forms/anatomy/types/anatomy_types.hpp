#pragma once

#include "gui_forms/schema/types/schema_types.hpp"
#include "gui_forms/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace gui_forms {

// Control anatomy: the named parts and states a control type publishes
// (design language §8.1 and §8.4; stage T1). Part and state names are a
// versioned public contract. Within a major version names are only added,
// never renamed or removed. Names are stored in snake_case and the C++
// enumerators below match them one to one; theme and form documents write
// them in kebab-case (see registered_name in gui_forms/schema). Names never
// reuse a C++ keyword: the default-action cue of §8.1 and §8.4 is named
// `preselect`.

// Shared part roles. A cue a theme binds to (role, state) reaches every part
// with that role in every control (§8.2).
enum class PartRole : std::uint8_t {
    region,
    shadow,
    face,
    edge,
    keyline,
    perimeter,
    sheen,
    seam,
    divider,
    caption,
    cap,
    well,
    track,
    fill,
    thumb,
    actuator,
    indicator,
    activity,
    glyph,
    icon,
    label,
    content,
    caret,
    disclosure,
    grip,
    chip,
    focus,
    preselect,
    selection,
    validation,
    drop,
    // A content picture drawn exactly as given, never tinted, dimmed or
    // resized by a theme (T2 GIVEN H1-1), such as the docking stamp's
    // snapshot. Disabled greys a control's chrome (its labels' text colour
    // and its highlighting: edge, well, plates), never its content; an icon
    // is chrome, an image is content.
    image,
    // The instrument roles (W-5; owner ruling 2 of the 0.5 closure: the
    // vocabulary only, before the instrument controls). `plot` is the field
    // a reading is drawn in; `graticule` the ruled scale lines over it;
    // `trace` the drawn reading. The built-in recipe paints them as a well,
    // a divider and an indicator, so a control moving to them draws the
    // same until a theme binds them.
    plot,
    graticule,
    trace,
    // The dial roles (row 2.5; owner option (a), 2026-11-09), appended.
    // `arc` is a stroked arc of the circle inscribed in the part's bounds,
    // from PartPaint::arc's start through its sweep: a gauge's track, its
    // value and its alarm segments. `needle` is a pointer from the centre
    // of the part's bounds out to the inscribed circle at
    // PartPaint::arc's start: a gauge's or a dial's pointer. `beak` is a
    // callout's pointer, a triangle in the part's bounds whose tip lies at
    // the middle of the edge its arrow mark names (towards the target),
    // drawn in the face's colours. `scrim` dims what lies behind an
    // in-window modal layer, over the part's bounds.
    arc,
    needle,
    beak,
    scrim,
    count,
};

// The §8.4 state set. Display scale and text scale are scalar engine
// inputs (EngineInput) rather than states and are not listed here.
enum class AnatomyState : std::uint8_t {
    // interaction
    normal,
    hot,
    pressed,
    captured,
    // focus and default
    focused,
    focus_visible,
    preselect,
    // value
    selected,
    checked,
    indeterminate,
    current,
    active_tool,
    // availability
    enabled,
    disabled,
    unavailable,
    read_only,
    // validity
    invalid,
    pending,
    // disclosure
    open,
    closed,
    expanding,
    collapsing,
    interrupted,
    // drag
    drop_accepted,
    drop_rejected,
    // participation (§8.3)
    key,
    secondary_participating,
    drag_proximate,
    deactivated,
    // environment
    high_contrast,
    reduced_motion,
    count,
};

// The engine inputs (design language §9.6 and §12.7; T2 GIVEN B1-1, B2-1
// and B4-2): the scalar values the theme engine sets for each part it
// draws, which a primitive declares it reads and a theme's expressions read
// by position. Nothing but the engine writes them, and nothing in a theme
// writes back to the application. Besides these, the engine reads the
// numeric values an application publishes by name (Window::publish_value),
// which are not vocabulary: each theme names the ones it reads. There is no
// density input (B4-2).
enum class EngineInput : std::uint8_t {
    // The pointer's position from the part's top-left corner, in logical
    // units (B2-1). Published only while the part is hot; otherwise it reads
    // as not published.
    pointer_x,
    pointer_y,
    // Seconds since the part's published states last changed.
    time_in_state,
    // Device pixels per logical unit, the window's display scale.
    display_scale,
    // The presentation's text scale.
    text_scale,
    count,
};

inline constexpr std::size_t engine_input_count = static_cast<std::size_t>(EngineInput::count);

inline constexpr std::size_t part_role_count =
    static_cast<std::size_t>(PartRole::count);
inline constexpr std::size_t anatomy_state_count =
    static_cast<std::size_t>(AnatomyState::count);
// Upper bounds that size the fixed tables and bit sets below. A control type
// declaring more is rejected by validate_control_anatomy.
inline constexpr std::size_t maximum_control_parts = 32;
inline constexpr std::size_t maximum_custom_states = 16;
inline constexpr std::size_t maximum_item_systems = 4;
static_assert(anatomy_state_count <= 64,
              "AnatomyStateSet stores standard states in 64 bits");

// Position of a part in its control type's declaration list. Distinct from
// the part's role (shared vocabulary) and from its name (public identity).
struct PartIndex final {
    std::uint8_t value{};

    friend constexpr bool operator==(PartIndex left, PartIndex right) noexcept {
        return left.value == right.value;
    }
};

// Position of an application-defined state in its control type's
// declaration list.
struct CustomStateIndex final {
    std::uint8_t value{};

    friend constexpr bool operator==(CustomStateIndex left,
                                     CustomStateIndex right) noexcept {
        return left.value == right.value;
    }
};

// Position of an item system in its control type's declaration list.
struct ItemSystemIndex final {
    std::uint8_t value{};

    friend constexpr bool operator==(ItemSystemIndex left, ItemSystemIndex right) noexcept {
        return left.value == right.value;
    }
};

// A position in the theme's accent ramp (design language §8.5; owner
// decision RB-22): the ordered colours a theme offers for telling sets of
// items apart, such as a ribbon's contextual tab sets. Index 0 is the ramp's
// first colour; an index past the end of a theme's ramp wraps around it.
// A part or item carries one as data, not as a state.
struct AccentIndex final {
    std::uint8_t value{};

    friend constexpr bool operator==(AccentIndex left, AccentIndex right) noexcept {
        return left.value == right.value;
    }
};

// A bounded set of active standard and custom states.
class AnatomyStateSet final {
public:
    constexpr void insert(AnatomyState state) noexcept {
        standard_bits_ |= standard_bit(state);
    }
    constexpr void erase(AnatomyState state) noexcept {
        standard_bits_ &= ~standard_bit(state);
    }
    [[nodiscard]] constexpr bool contains(AnatomyState state) const noexcept {
        return (standard_bits_ & standard_bit(state)) != 0U;
    }
    constexpr void insert(CustomStateIndex state) noexcept {
        custom_bits_ |= custom_bit(state);
    }
    [[nodiscard]] constexpr bool contains(CustomStateIndex state) const noexcept {
        return (custom_bits_ & custom_bit(state)) != 0U;
    }
    [[nodiscard]] constexpr std::uint64_t standard_bits() const noexcept {
        return standard_bits_;
    }
    [[nodiscard]] constexpr std::uint16_t custom_bits() const noexcept {
        return custom_bits_;
    }

    friend constexpr bool operator==(const AnatomyStateSet& left,
                                     const AnatomyStateSet& right) noexcept {
        return left.standard_bits_ == right.standard_bits_ &&
               left.custom_bits_ == right.custom_bits_;
    }

private:
    [[nodiscard]] static constexpr std::uint64_t standard_bit(
        AnatomyState state) noexcept {
        const std::uint64_t one = 1U;
        return one << static_cast<std::uint64_t>(state);
    }
    [[nodiscard]] static constexpr std::uint16_t custom_bit(
        CustomStateIndex state) noexcept {
        const unsigned int one = 1U;
        const unsigned int shifted = one << static_cast<unsigned int>(state.value);
        return static_cast<std::uint16_t>(shifted);
    }

    std::uint64_t standard_bits_{};
    std::uint16_t custom_bits_{};
};

// One declared part. `name` is the part's stable registered name within its
// control type; documents write it in `::part(name)`. `composed_from` names the
// control id of a simpler system the part reuses (a scroll arrow is a
// "button"), or is empty.
struct PartDeclaration final {
    std::string_view name;
    PartRole role{PartRole::region};
    std::string_view composed_from;
    // The states the part takes on its own, as written in
    // `::part(name):state(state)`: a scroll bar's thumb is pressed while
    // its arrows are not. Empty for a part that only shares its control's
    // (or item's) states.
    std::span<const AnatomyState> states;
};

// One application-defined finite state, as written in `:state(name)`.
struct CustomStateDeclaration final {
    std::string_view name;
};

// Custom states admitted to the vocabulary (design language §8.5; owner
// decisions DK-35 and DK-40, §12.10) for the items that need them: a
// document tab whose content has unsaved changes (`modified`, which the
// theme shows; there is no mark for it) and a tab pinned to the front of
// its strip (`pinned`). They stay custom states, declared by the item
// systems that use them, not standard AnatomyStates: a control lists these
// declarations among its item system's custom states instead of spelling
// its own, so every control and theme means one thing by each name.
inline constexpr CustomStateDeclaration modified_custom_state{.name = "modified"};
inline constexpr CustomStateDeclaration pinned_custom_state{.name = "pinned"};
inline constexpr std::array<CustomStateDeclaration, 2> admitted_custom_states{
    {modified_custom_state, pinned_custom_state}};

// One kind of repeated item a control paints (owner decision 5): a list
// row, a tree node, a details-view column header. `name` is the system's
// registered name in the schema ("item", "column"). Item part names are
// unique within the control, across its own parts and every item system, so
// a document addresses an item part by its name alone.
struct ItemSystemDeclaration final {
    std::string_view name;
    std::span<const PartDeclaration> parts;
    std::span<const AnatomyState> states;
    // Application-defined finite states of one item, as for a control.
    std::span<const CustomStateDeclaration> custom_states;
};

// The anatomy of one control type. Every span refers to static storage
// owned by the control type's translation unit, so a ControlAnatomy may be
// copied and retained for the life of the program.
struct ControlAnatomy final {
    // The control's id in manifests/controls.json, or "control" for the
    // anatomy every control inherits.
    std::string_view control_id;
    std::span<const PartDeclaration> parts;
    std::span<const AnatomyState> states;
    std::span<const CustomStateDeclaration> custom_states;
    // The kinds of repeated item the control paints, empty for a control
    // without items. Most controls with items have one system; the details
    // view adds its column headers as a second.
    std::span<const ItemSystemDeclaration> item_systems;
};

// One declared part as published: its geometry in the control's local
// coordinate space after layout, and its own active states. `present` is
// false when the part does not exist in the control's current configuration
// (a track bar's fill in the classic style); `bounds` is then empty.
// Presence does not depend on transient state: a focus part is present while
// the control is unfocused. `states` holds only states the part's
// declaration lists, and no custom states.
struct PartGeometry final {
    Rect bounds{};
    bool present{};
    AnatomyStateSet states{};
    // The accent the part is tinted with, or none (see AccentIndex).
    std::optional<AccentIndex> accent{};
    // The colour the part shows as its content, or none: a colour swatch's
    // sample (PartPaint::content_color; owner decision W-7).
    std::optional<Color> content_color{};
    // The colours the application authored for the part, or none
    // (PartPaint::authored_background and authored_foreground; owner
    // decision T4-1).
    std::optional<Color> authored_background{};
    std::optional<Color> authored_foreground{};

    // A part that is present exactly when `bounds` is not empty.
    [[nodiscard]] static constexpr PartGeometry from_bounds(Rect bounds) noexcept {
        PartGeometry geometry;
        geometry.bounds = bounds;
        geometry.present = !bounds.empty();
        return geometry;
    }

    friend constexpr bool operator==(const PartGeometry& left,
                                     const PartGeometry& right) noexcept {
        return left.bounds == right.bounds && left.present == right.present &&
               left.states == right.states && left.accent == right.accent &&
               left.content_color == right.content_color &&
               left.authored_background == right.authored_background &&
               left.authored_foreground == right.authored_foreground;
    }
};

// Storage for one control's published part geometry: `count` live records,
// one per declared part, in declaration order.
struct PartGeometryBuffer final {
    std::array<PartGeometry, maximum_control_parts> geometry{};
    std::size_t count{};

    [[nodiscard]] std::span<const PartGeometry> parts() const noexcept {
        const std::span<const PartGeometry> live(geometry.data(), count);
        return live;
    }
};

// Registered (snake_case) public names.
[[nodiscard]] std::string_view part_role_name(PartRole role) noexcept;
[[nodiscard]] std::string_view anatomy_state_name(AnatomyState state) noexcept;
[[nodiscard]] std::string_view engine_input_name(EngineInput input) noexcept;
[[nodiscard]] std::optional<PartRole> part_role_from_name(
    std::string_view name) noexcept;
[[nodiscard]] std::optional<AnatomyState> anatomy_state_from_name(
    std::string_view name) noexcept;

// Lookup by declared name within one declaration list, within a control
// type's own parts, or within one of its item systems.
[[nodiscard]] std::optional<PartIndex> find_declared_part(
    std::span<const PartDeclaration> declared, std::string_view part_name) noexcept;
[[nodiscard]] std::optional<PartIndex> find_part(
    const ControlAnatomy& anatomy, std::string_view part_name) noexcept;
[[nodiscard]] std::optional<ItemSystemIndex> find_item_system(
    const ControlAnatomy& anatomy, std::string_view system_name) noexcept;
[[nodiscard]] std::optional<PartIndex> find_item_part(
    const ItemSystemDeclaration& system, std::string_view part_name) noexcept;
[[nodiscard]] std::optional<CustomStateIndex> find_custom_state(
    const ControlAnatomy& anatomy, std::string_view state_name) noexcept;
[[nodiscard]] std::optional<CustomStateIndex> find_item_custom_state(
    const ItemSystemDeclaration& system, std::string_view state_name) noexcept;
[[nodiscard]] bool declares_state(const ControlAnatomy& anatomy,
                                  AnatomyState state) noexcept;
[[nodiscard]] bool declares_item_state(const ItemSystemDeclaration& system,
                                       AnatomyState state) noexcept;
[[nodiscard]] bool declares_part_state(const PartDeclaration& part,
                                       AnatomyState state) noexcept;

// Typed records (stage T1). C++ reads a control's parts and states as dot
// members of a record (`parts.thumb.bounds`, `states.pressed`); registered
// names are identities for documents and tools only. A field table pairs
// each record member with the declaration it publishes, so a type's
// declared names and its record members come from one table.

// One member of a parts record and the part it publishes.
template <typename Record>
struct PartField final {
    PartDeclaration declaration;
    PartGeometry Record::*member{};
};

// One member of a states record and the standard state it reports.
template <typename Record>
struct StateField final {
    AnatomyState state{AnatomyState::normal};
    bool Record::*member{};
};

template <typename Record, std::size_t Count>
[[nodiscard]] constexpr std::array<PartDeclaration, Count> part_declarations(
    const std::array<PartField<Record>, Count>& fields) {
    std::array<PartDeclaration, Count> declarations{};
    for (std::size_t index = 0; index < Count; ++index) {
        declarations[index] = fields[index].declaration;
    }
    return declarations;
}

template <std::size_t First, std::size_t Second>
[[nodiscard]] constexpr std::array<PartDeclaration, First + Second> joined_declarations(
    const std::array<PartDeclaration, First>& first,
    const std::array<PartDeclaration, Second>& second) {
    std::array<PartDeclaration, First + Second> joined{};
    for (std::size_t index = 0; index < First; ++index) {
        joined[index] = first[index];
    }
    for (std::size_t index = 0; index < Second; ++index) {
        joined[First + index] = second[index];
    }
    return joined;
}

template <typename Record, std::size_t Count>
[[nodiscard]] constexpr std::array<AnatomyState, Count> state_declarations(
    const std::array<StateField<Record>, Count>& fields) {
    std::array<AnatomyState, Count> states{};
    for (std::size_t index = 0; index < Count; ++index) {
        states[index] = fields[index].state;
    }
    return states;
}

// Publishes every member of `record` into the part `anatomy` declares with
// that member's name. A part the anatomy does not declare is skipped, so a
// base type's record publishes into a subclass's longer declaration list.
// `geometry` holds one record per declared part.
template <typename Record, std::size_t Count>
void write_declared_parts(std::span<const PartDeclaration> declared,
                          const std::array<PartField<Record>, Count>& fields,
                          const Record& record, std::span<PartGeometry> geometry) noexcept {
    for (std::size_t index = 0; index < Count; ++index) {
        const std::optional<PartIndex> part =
            find_declared_part(declared, fields[index].declaration.name);
        if (!part.has_value() || (*part).value >= geometry.size()) {
            continue;
        }
        const PartGeometry& published = record.*(fields[index].member);
        geometry[(*part).value] = published;
    }
}

// Reads the published geometry back into `record`; a member whose part the
// anatomy does not declare is left absent.
template <typename Record, std::size_t Count>
void read_declared_parts(std::span<const PartDeclaration> declared,
                         const std::array<PartField<Record>, Count>& fields,
                         std::span<const PartGeometry> geometry, Record& record) noexcept {
    for (std::size_t index = 0; index < Count; ++index) {
        const std::optional<PartIndex> part =
            find_declared_part(declared, fields[index].declaration.name);
        PartGeometry value;
        if (part.has_value() && (*part).value < geometry.size()) {
            value = geometry[(*part).value];
        }
        record.*(fields[index].member) = value;
    }
}

template <typename Record, std::size_t Count>
void write_parts(const ControlAnatomy& anatomy,
                 const std::array<PartField<Record>, Count>& fields,
                 const Record& record, std::span<PartGeometry> geometry) noexcept {
    write_declared_parts(anatomy.parts, fields, record, geometry);
}

template <typename Record, std::size_t Count>
void read_parts(const ControlAnatomy& anatomy,
                const std::array<PartField<Record>, Count>& fields,
                std::span<const PartGeometry> geometry, Record& record) noexcept {
    read_declared_parts(anatomy.parts, fields, geometry, record);
}

// The same mapping over one realized item's geometry and the parts of
// its item system.
template <typename Record, std::size_t Count>
void write_item_parts(const ItemSystemDeclaration& system,
                      const std::array<PartField<Record>, Count>& fields,
                      const Record& record, std::span<PartGeometry> geometry) noexcept {
    write_declared_parts(system.parts, fields, record, geometry);
}

template <typename Record, std::size_t Count>
void read_item_parts(const ItemSystemDeclaration& system,
                     const std::array<PartField<Record>, Count>& fields,
                     std::span<const PartGeometry> geometry, Record& record) noexcept {
    read_declared_parts(system.parts, fields, geometry, record);
}

template <typename Record, std::size_t Count>
void read_states(const std::array<StateField<Record>, Count>& fields,
                 const AnatomyStateSet& active, Record& record) noexcept {
    for (std::size_t index = 0; index < Count; ++index) {
        record.*(fields[index].member) = active.contains(fields[index].state);
    }
}

// Reasons a declared anatomy is malformed.
enum class AnatomyDefect : std::uint8_t {
    none,
    control_id_not_snake_case,
    no_parts,
    too_many_parts,
    part_name_not_snake_case,
    duplicate_part_name,
    part_role_out_of_range,
    composed_from_not_snake_case,
    state_out_of_range,
    duplicate_state,
    too_many_custom_states,
    custom_state_not_snake_case,
    custom_state_shadows_standard_state,
    duplicate_custom_state,
    too_many_item_parts,
    item_part_name_not_snake_case,
    duplicate_item_part_name,
    item_part_role_out_of_range,
    item_state_out_of_range,
    duplicate_item_state,
    too_many_item_custom_states,
    item_custom_state_not_snake_case,
    item_custom_state_shadows_standard_state,
    duplicate_item_custom_state,
    too_many_item_systems,
    item_system_name_not_snake_case,
    duplicate_item_system_name,
    item_system_without_parts,
    part_state_out_of_range,
    duplicate_part_state,
};

// The result of validate_control_anatomy: the first defect found, or none.
struct AnatomyValidation final {
    AnatomyDefect defect{AnatomyDefect::none};
    // Index into the list the defect concerns (parts, states, custom states,
    // item systems, or one item system's parts or states); zero when the
    // defect concerns the anatomy as a whole.
    std::size_t index{};
    // The item system an item defect concerns; zero otherwise.
    std::size_t item_system{};
};

[[nodiscard]] std::string_view anatomy_defect_name(AnatomyDefect defect) noexcept;
[[nodiscard]] AnatomyValidation validate_control_anatomy(
    const ControlAnatomy& anatomy) noexcept;

// The anatomy every control inherits until its type declares its own: the
// granted region and the focus part.
[[nodiscard]] const ControlAnatomy& base_control_anatomy() noexcept;

} // namespace gui_forms
