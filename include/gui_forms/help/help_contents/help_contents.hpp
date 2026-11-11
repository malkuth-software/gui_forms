#pragma once

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/help/help_records/help_records.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class SurfaceRoot;

// Where a stand-alone topic goes in the generated contents (help system
// study, section 4): after the record it names (its next sibling, after the
// record's own subtree), as the last child of the record it names, or at
// the top of the contents. A place whose record has no page in the
// contents falls back to the end of the top level.
enum class HelpPlaceKind : std::uint8_t { after, under, top };

class HelpPlace final {
public:
    // After `anchor` (a stable ID; checked when the program is compiled).
    [[nodiscard]] static HelpPlace after(StableIdLiteral anchor) {
        return HelpPlace(HelpPlaceKind::after, anchor.value());
    }
    // The last child of `anchor`.
    [[nodiscard]] static HelpPlace under(StableIdLiteral anchor) {
        return HelpPlace(HelpPlaceKind::under, anchor.value());
    }
    // The start of the top level: a welcome or home topic.
    [[nodiscard]] static HelpPlace top() { return HelpPlace(HelpPlaceKind::top, {}); }
    // A place read at run time (the C ABI's "after:<id>", "under:<id>",
    // "top"). Throws std::invalid_argument for any other text or an anchor
    // that is not a stable ID. The anchor is copied.
    [[nodiscard]] static HelpPlace parse(std::string_view text);

    [[nodiscard]] HelpPlaceKind kind() const noexcept { return kind_; }
    [[nodiscard]] std::string_view anchor() const& noexcept { return anchor_; }
    std::string_view anchor() const&& = delete;

private:
    HelpPlace(HelpPlaceKind kind, std::string_view anchor) : kind_(kind), anchor_(anchor) {}

    HelpPlaceKind kind_{HelpPlaceKind::top};
    std::string anchor_{};
};

// A stand-alone topic ("Make your first picture"): an ordinary help record
// that no control holds, declared like a message,
//
//   inline const gui_forms::HelpTopic first_picture{
//       gui_forms::StableId("help.first-picture"), "Make your first picture",
//       first_picture_body, gui_forms::HelpPlace::after("main")};
//
// Its title is the record's `text`, its body the record's `help`. The text
// given here is the built-in language's; a language file translates it by
// the topic's stable ID. A declaration joins the process's topic list when
// it is made and leaves it when destroyed, so a topic at namespace scope is
// declared for the whole run. Declarations are made during static
// initialization or on the UI thread, never concurrently.
class HelpTopic final {
public:
    HelpTopic(StableId stable_id, std::string title, std::string body, HelpPlace place,
              std::string help_line = {}, std::string keywords = {});
    ~HelpTopic();
    HelpTopic(const HelpTopic&) = delete;
    HelpTopic& operator=(const HelpTopic&) = delete;
    HelpTopic(HelpTopic&&) = delete;
    HelpTopic& operator=(HelpTopic&&) = delete;

    [[nodiscard]] const StableId& stable_id() const noexcept { return stable_id_; }
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    [[nodiscard]] const std::string& body() const noexcept { return body_; }
    [[nodiscard]] const HelpPlace& place() const noexcept { return place_; }
    [[nodiscard]] const std::string& help_line() const noexcept { return help_line_; }
    [[nodiscard]] const std::string& keywords() const noexcept { return keywords_; }

    // The first declared topic, in declaration order, or null; `next` walks
    // the rest. The pointers stay valid while the topics live.
    [[nodiscard]] static const HelpTopic* first() noexcept;
    [[nodiscard]] const HelpTopic* next() const noexcept { return next_; }

private:
    StableId stable_id_;
    std::string title_;
    std::string body_;
    HelpPlace place_;
    std::string help_line_;
    std::string keywords_;
    HelpTopic* previous_{};
    HelpTopic* next_{};
};

// A topic's place in the contents, as the contents generator reads it: from
// a HelpTopic or from a run-time declaration (HelpBookSystem::declare_topic).
struct HelpTopicPlacement final {
    std::string stable_id{};
    HelpPlaceKind kind{HelpPlaceKind::top};
    std::string anchor{};
};

// Fills `placements` (cleared first) from every declared HelpTopic, in
// declaration order.
void collect_declared_topics(std::vector<HelpTopicPlacement>& placements);

// Writes every declared HelpTopic's text into `layer` (the built-in layer):
// title, body, help line and keywords.
void put_declared_topics(HelpRecordLayer& layer);
// The same, as language-file records (text, help, help-line, keywords), for
// the template export.
void put_declared_topics(LocaleLayer& layer);

// What a node of the program's structure is, as the contents tree arranges
// it: a window, a group that holds other controls (a panel, a group box, a
// page), or a control.
enum class HelpSkeletonKind : std::uint8_t { window, group, control };

// One node of the program's structure, in the order the skeleton walk
// enumerates it (depth first, children in order). `depth` is 0 for a
// window. `name` is the control's accessible name or text, the title the
// contents uses when no record gives one.
struct HelpSkeletonNode final {
    std::string stable_id{};
    std::string name{};
    std::uint32_t depth{};
    HelpSkeletonKind kind{HelpSkeletonKind::control};
};

// Appends `window`'s structure to `nodes`: its root as the window, then
// every control of its tree, depth first. Overlays and controls without a
// stable ID path under the root are not part of the structure. UI thread.
void append_help_skeleton(const SurfaceRoot& window, std::vector<HelpSkeletonNode>& nodes);

// Puts the help fields `window`'s controls hold in code (Control::help_line,
// help and keywords; HelpProvider writes the first and last) into `layer`
// as program values, keyed by each control's stable ID: only the non-empty
// fields, over what `layer` held for that ID. The same walk as
// append_help_skeleton (an explicit stack, bounded by the window's own
// nesting limit). UI thread.
void append_control_help_fields(const SurfaceRoot& window, LocaleLayer& layer);

inline constexpr std::uint32_t help_no_parent = 0xFFFFFFFFU;

// One page of the generated contents, in preorder. `parent` indexes the
// tree's nodes (help_no_parent at the top level). Titles come from the
// catalog in force, so they differ by language; the shape does not.
struct HelpContentsNode final {
    std::string stable_id{};
    std::string title{};
    std::uint32_t parent{help_no_parent};
    std::uint32_t depth{};
    // A stand-alone topic rather than a part of the program's structure.
    bool topic{};
};

// A control with a help line but no help body: listed on the page of its
// nearest ancestor that has one (`page` indexes the tree's nodes).
struct HelpContentsLine final {
    std::uint32_t page{};
    std::string stable_id{};
    std::string title{};
    std::string help_line{};
};

struct HelpContentsTree final {
    std::vector<HelpContentsNode> nodes{};
    std::vector<HelpContentsLine> lines{};

    // The node holding `stable_id`, or help_no_parent.
    [[nodiscard]] std::uint32_t find(std::string_view stable_id) const noexcept;
    void clear() noexcept;
};

// Generates the contents (help system study, ruling A): the skeleton's
// windows, groups and controls, in skeleton order, keeping only records with
// a `help` body as pages; a record with only a `help-line` becomes a line on
// its nearest page; then each placement slotted in at its place, in order.
// A placement whose record has no body is left out. Titles are the resolved
// `text`, else the skeleton's name, else the ID's last segment. `tree` is
// replaced.
void build_help_contents(std::span<const HelpSkeletonNode> skeleton, std::span<const HelpTopicPlacement> topics,
                         const HelpCatalog& catalog, HelpContentsTree& tree);

} // namespace gui_forms
