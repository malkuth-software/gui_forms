// Strip merging (tool_strip_manager.hpp): each MergeAction on tool strips
// and menu strips, matching by text and by merge_index, match_only
// recursion into menus, refusals, the history, and both reverts (including
// re-merging newer merges and a destroyed source).

#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

ToolStripItemSpec button(std::string id, std::string text, MergeAction action = MergeAction::append) {
    ToolStripItemSpec item{};
    item.stable_id = std::move(id);
    item.text = std::move(text);
    item.merge_action = action;
    return item;
}

std::shared_ptr<Command> command(const std::string& id, const std::string& text) {
    std::shared_ptr<Command> made = std::make_shared<Command>(id, text);
    return made;
}

MenuItemSpec menu_command(const std::string& id, const std::string& text, MergeAction action = MergeAction::append) {
    MenuItemSpec item(id, MenuItemKind::command, command(id + ".command", text));
    item.merge_action = action;
    return item;
}

std::string ids(const std::vector<ToolStripItemSpec>& items) {
    std::string joined{};
    for (const ToolStripItemSpec& item : items) {
        if (!joined.empty()) joined += ' ';
        joined += item.stable_id;
    }
    return joined;
}

std::string ids(const std::vector<MenuItemSpec>& items) {
    std::string joined{};
    for (const MenuItemSpec& item : items) {
        if (!joined.empty()) joined += ' ';
        joined += item.stable_id;
    }
    return joined;
}

std::string ids(const std::vector<MenuStripItemSpec>& items) {
    std::string joined{};
    for (const MenuStripItemSpec& item : items) {
        if (!joined.empty()) joined += ' ';
        joined += item.stable_id + "[" + ids(item.items) + "]";
    }
    return joined;
}

struct Strips final {
    std::shared_ptr<ToolStrip> source;
    std::shared_ptr<ToolStrip> target;
};

Strips make_strips(std::vector<ToolStripItemSpec> source_items, std::vector<ToolStripItemSpec> target_items) {
    Strips strips{};
    strips.source = make_control<ToolStrip>(StableId("merge.source"));
    strips.target = make_control<ToolStrip>(StableId("merge.target"));
    (*strips.source).set_items(std::move(source_items));
    (*strips.target).set_items(std::move(target_items));
    return strips;
}

void test_append_insert_and_revert() {
    ToolStripItemSpec inserted = button("s.first", "First", MergeAction::insert);
    inserted.merge_index = 0U;
    ToolStripItemSpec unplaced = button("s.unplaced", "Unplaced", MergeAction::insert);
    ToolStripItemSpec past = button("s.past", "Past", MergeAction::insert);
    past.merge_index = 99U;
    Strips strips = make_strips({button("s.append", "Append"), inserted, unplaced, past},
                                {button("t.one", "One"), button("t.two", "Two")});
    require(merge_strips(*strips.source, *strips.target), "an append merge reports a change");
    require(ids((*strips.target).items()) == "s.first t.one t.two s.append s.past",
            "append goes to the end, insert to its index, past the end to the end");
    require(ids((*strips.source).items()) == "s.unplaced", "moved items leave the source; insert without index stays");
    require(revert_merge(*strips.target), "a merge reverts");
    require(ids((*strips.target).items()) == "t.one t.two", "revert restores the target");
    require(ids((*strips.source).items()) == "s.append s.first s.unplaced s.past", "revert restores the source order");
    require(!revert_merge(*strips.target), "nothing is left to revert");
}

void test_replace_remove_and_matching() {
    ToolStripItemSpec by_index = button("s.by-index", "Nothing alike", MergeAction::remove);
    by_index.merge_index = 2U;
    Strips strips = make_strips({button("s.save", "SAVE", MergeAction::replace), button("s.print", "print", MergeAction::remove), by_index,
                                 button("s.none", "Absent", MergeAction::replace)},
                                {button("t.open", "Open"), button("t.save", "Save"), button("t.print", "Print"),
                                 button("t.close", "Close")});
    require(merge_strips(*strips.source, *strips.target), "replace and remove change the target");
    require(ids((*strips.target).items()) == "t.open s.save",
            "replace takes the matched item's place; remove matches by text, then by index");
    require(ids((*strips.source).items()) == "s.print s.by-index s.none",
            "removing and unmatched items stay in the source");
    require(revert_merge(*strips.target), "the merge reverts");
    require(ids((*strips.target).items()) == "t.open t.save t.print t.close", "removed items come back in place");
    require(ids((*strips.source).items()) == "s.save s.print s.by-index s.none", "the replacing item returns");
}

void test_match_only_recursion() {
    ToolStripItemSpec source_file = button("s.file", "File", MergeAction::match_only);
    source_file.kind = ToolStripItemKind::drop_down_button;
    MenuItemSpec export_item = menu_command("s.file.export", "Export", MergeAction::insert);
    export_item.merge_index = 1U;
    source_file.drop_down = {export_item, menu_command("s.file.recent", "Recent")};
    ToolStripItemSpec target_file = button("t.file", "file");
    target_file.kind = ToolStripItemKind::drop_down_button;
    target_file.drop_down = {menu_command("t.file.open", "Open"), menu_command("t.file.exit", "Exit")};
    Strips strips = make_strips({source_file}, {target_file});
    require(merge_strips(*strips.source, *strips.target), "match_only merges the menus");
    require((*strips.target).items().size() == 1U, "the matched item stays single");
    require(ids((*strips.target).items()[0].drop_down) == "t.file.open s.file.export t.file.exit s.file.recent",
            "menu items merge by their own actions");
    require(ids((*strips.source).items()) == "s.file", "the match_only item stays in the source");
    require((*strips.source).items()[0].drop_down.empty(), "its emptied menu is valid");
    require(revert_merge(*strips.target), "the menu merge reverts");
    require(ids((*strips.target).items()[0].drop_down) == "t.file.open t.file.exit", "the target menu is restored");
    require(ids((*strips.source).items()[0].drop_down) == "s.file.export s.file.recent", "the source menu is restored");
}

void test_refusals() {
    Strips strips = make_strips({button("s.a", "A")}, {button("t.b", "B")});
    bool threw = false;
    try {
        static_cast<void>(merge_strips(*strips.target, *strips.target));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a strip cannot merge into itself");
    (*strips.target).set_allow_merge(false);
    require(!merge_strips(*strips.source, *strips.target), "a target that does not allow merging refuses");
    (*strips.target).set_allow_merge(true);
    (*strips.source).set_allow_merge(false);
    require(!merge_strips(*strips.source, *strips.target), "a source that does not allow merging refuses");
    (*strips.source).set_allow_merge(true);
    Strips colliding = make_strips({button("same.id", "A")}, {button("same.id", "B")});
    require(!merge_strips(*colliding.source, *colliding.target), "a repeated ID refuses the merge");
    require(ids((*colliding.source).items()) == "same.id" && ids((*colliding.target).items()) == "same.id",
            "a refused merge changes nothing");
    require(!revert_merge(*colliding.target), "a refused merge leaves no history");
    Strips idle = make_strips({button("s.stay", "Stay", MergeAction::match_only)}, {button("t.other", "Other")});
    require(!merge_strips(*idle.source, *idle.target), "a merge that applies nothing reports no change");
}

void test_check_state_moves() {
    ToolStripItemSpec toggle = button("s.toggle", "Toggle");
    toggle.check_on_click = true;
    Strips strips = make_strips({toggle}, {});
    (*strips.source).set_item_checked("s.toggle", true);
    require(merge_strips(*strips.source, *strips.target), "the toggle moves");
    require((*strips.target).item_checked("s.toggle"), "its checked state moves with it");
}

void test_revert_one_source() {
    std::shared_ptr<ToolStrip> target = make_control<ToolStrip>(StableId("merge.target"));
    (*target).set_items({button("t.base", "Base")});
    std::shared_ptr<ToolStrip> first = make_control<ToolStrip>(StableId("merge.first"));
    (*first).set_items({button("f.item", "First")});
    std::shared_ptr<ToolStrip> second = make_control<ToolStrip>(StableId("merge.second"));
    (*second).set_items({button("s.item", "Second")});
    require(merge_strips(*first, *target) && merge_strips(*second, *target), "two sources merge");
    require(ids((*target).items()) == "t.base f.item s.item", "in order");
    require(!revert_merge(*target, *target), "a strip that never merged reverts nothing");
    require(revert_merge(*target, *first), "the first source reverts");
    require(ids((*target).items()) == "t.base s.item", "the newer merge is merged again");
    require(ids((*first).items()) == "f.item", "the first source is restored");
    require(ids((*second).items()).empty(), "the second source stays merged");
    second.reset();
    require(revert_merge(*target), "a merge from a destroyed source reverts");
    require(ids((*target).items()) == "t.base", "its items leave the target");
}

void test_menu_strips() {
    std::shared_ptr<MenuStrip> parent = make_control<MenuStrip>(StableId("merge.parent"));
    std::shared_ptr<MenuStrip> child = make_control<MenuStrip>(StableId("merge.child"));
    MenuStripItemSpec parent_file{};
    parent_file.stable_id = "p.file";
    parent_file.text = "&File";
    parent_file.items = {menu_command("p.file.open", "Open"), menu_command("p.file.exit", "Exit")};
    MenuStripItemSpec parent_window{};
    parent_window.stable_id = "p.window";
    parent_window.text = "&Window";
    parent_window.items = {menu_command("p.window.tile", "Tile")};
    (*parent).set_items({parent_file, parent_window});
    MenuStripItemSpec child_file{};
    child_file.stable_id = "c.file";
    child_file.text = "&File";
    child_file.merge_action = MergeAction::match_only;
    MenuItemSpec save = menu_command("c.file.save", "Save", MergeAction::insert);
    save.merge_index = 1U;
    child_file.items = {save};
    MenuStripItemSpec child_format{};
    child_format.stable_id = "c.format";
    child_format.text = "F&ormat";
    child_format.merge_action = MergeAction::insert;
    child_format.merge_index = 1U;
    child_format.items = {menu_command("c.format.font", "Font")};
    (*child).set_items({child_file, child_format});
    require(merge_strips(*child, *parent), "the child menu merges into the parent");
    require(ids((*parent).items()) == "p.file[p.file.open c.file.save p.file.exit] c.format[c.format.font] "
                                      "p.window[p.window.tile]",
            "match_only on equal text merges menus; insert places the new menu");
    require(ids((*child).items()) == "c.file[]", "the child keeps its emptied File menu");
    require(revert_merge(*parent, *child), "the child's merge reverts");
    require(ids((*parent).items()) == "p.file[p.file.open p.file.exit] p.window[p.window.tile]", "parent restored");
    require(ids((*child).items()) == "c.file[c.file.save] c.format[c.format.font]", "child restored");
}

} // namespace

int main() {
    test_append_insert_and_revert();
    test_replace_remove_and_matching();
    test_match_only_recursion();
    test_refusals();
    test_check_state_moves();
    test_revert_one_source();
    test_menu_strips();
    std::cout << "strip merge tests passed\n";
    return 0;
}
