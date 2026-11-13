// Every command path through the command chain (owner decision DK-47,
// review R8): a button bound to a Command, a CommandBinding, a
// GalleryCommand's commit and a NotifyIcon's menu row reach a target on the
// window's chain, which takes the command before its own `invoked` event;
// without a window they execute the Command as before.

#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/commands/command_binding/command_binding.hpp"
#include "gui_forms/commands/command_route/command_route.hpp"
#include "gui_forms/commands/gallery_command/gallery_command.hpp"
#include "gui_forms/commands/types/command_types.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/components/notify_icon/notify_icon.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/host/image/host_image.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/window/window.hpp"
#include "headless_host.hpp"

#include <cstddef>
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
    std::cerr << "gui_forms_command_chain_tests: " << message << '\n';
    std::exit(1);
}

struct Journal final {
    std::vector<std::string> handled{};
};

class Handler final {
public:
    explicit Handler(Journal& journal) : journal_(&journal) {}
    void run(const CommandInvocation& invocation) { (*journal_).handled.push_back(invocation.command_id); }

private:
    Journal* journal_{};
};

struct CountInvocations final {
    int* count{};
    void operator()(const CommandInvocation&) const { ++(*count); }
};

std::shared_ptr<CommandTarget> main_target(Handler& handler, std::string command_id) {
    std::shared_ptr<CommandTarget> target = std::make_shared<CommandTarget>("main");
    (*target).set_handler(std::move(command_id), Delegate<const CommandInvocation&>::bind<Handler, &Handler::run>(handler));
    return target;
}

void bound_buttons() {
    Journal journal{};
    Handler handler(journal);
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Button> bound = make_control<Button>(StableId("bound"), "Save");
    std::shared_ptr<Button> binding = make_control<Button>(StableId("binding"), "Save too");
    (*root).add_child(bound);
    (*root).add_child(binding);
    Window window(root, {200.0, 100.0});
    const std::shared_ptr<Command> save = std::make_shared<Command>("save", "Save");
    int invoked = 0;
    SubscriptionToken counting = (*save).invoked().subscribe(CountInvocations{&invoked});
    (*bound).bind(*save);
    const CommandBinding paired(save, binding);
    const std::shared_ptr<CommandTarget> target = main_target(handler, "save");
    window.set_command_target(CommandRouteStage::main_window, target);
    require((*bound).perform_click() && journal.handled.size() == 1U && invoked == 0,
            "a bound button routes its command: the main window's target takes it");
    require((*binding).perform_click() && journal.handled.size() == 2U && invoked == 0,
            "a CommandBinding routes its command too");
    window.set_command_target(CommandRouteStage::main_window, {});
    require((*bound).perform_click() && invoked == 1, "with nothing on the chain the command's own event runs");

    // A detached button executes the command as before.
    std::shared_ptr<Button> alone = make_control<Button>(StableId("alone"), "Save");
    (*alone).bind(*save);
    require((*alone).perform_click() && invoked == 2, "a button without a window executes the command");
}

void gallery_commands() {
    Journal journal{};
    Handler handler(journal);
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    Window window(root, {200.0, 100.0});
    const std::shared_ptr<Command> style = std::make_shared<Command>("style", "Style");
    int invoked = 0;
    SubscriptionToken counting = (*style).invoked().subscribe(CountInvocations{&invoked});
    GalleryCommand gallery(style);
    const std::shared_ptr<CommandTarget> target = main_target(handler, "style");
    window.set_command_target(CommandRouteStage::main_window, target);
    require(gallery.commit_routed("style.heading", window, "gallery") && journal.handled.size() == 1U &&
                invoked == 0 && gallery.value() && *gallery.value() == "style.heading",
            "a gallery commit routes through the window's chain after setting its value");
    require(gallery.commit("style.body", "gallery") && invoked == 1 && journal.handled.size() == 1U,
            "a commit without a window executes the command");
}

void notify_icon_menus() {
    Journal journal{};
    Handler handler(journal);
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    Window window(root, {200.0, 100.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    NotifyIcon icon(window);
    HostImage image{};
    image.width = 16U;
    image.height = 16U;
    image.row_bytes = 16U * 4U;
    image.pixels.assign(static_cast<std::size_t>(image.row_bytes) * 16U, std::byte{0x7F});
    icon.icon = std::vector<HostImage>{image};
    icon.visible = true;
    require(icon.status().accepted() && icon.shown(), "the icon shows");
    const std::shared_ptr<Command> mute = std::make_shared<Command>("tray.mute", "Mute");
    int invoked = 0;
    SubscriptionToken counting = (*mute).invoked().subscribe(CountInvocations{&invoked});
    std::shared_ptr<ContextMenu> menu = std::make_shared<ContextMenu>("tray.menu");
    (*menu).set_items({MenuItemSpec("tray.menu.mute", MenuItemKind::command, mute)});
    icon.context_menu = menu;
    const std::shared_ptr<CommandTarget> target = main_target(handler, "tray.mute");
    window.set_command_target(CommandRouteStage::main_window, target);
    services.queue_tray_menu_choice("tray.menu.mute");
    services.simulate_tray_icon_event({HostTrayIconId{1U}, HostTrayIconAction::context});
    require(journal.handled.size() == 1U && invoked == 0,
            "a notification-area menu row routes its command through the window's chain");
    icon.dispose();
}

} // namespace

int main() {
    bound_buttons();
    gallery_commands();
    notify_icon_menus();
    std::cout << "Command chain: bound buttons, CommandBinding, GalleryCommand and NotifyIcon menus route "
                 "through the window's chain passed\n";
    return EXIT_SUCCESS;
}
