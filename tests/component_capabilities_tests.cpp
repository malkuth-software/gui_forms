// Stage B9 property capabilities: the components (ColorDialog,
// OpenFileDialog, SaveFileDialog, FolderBrowserDialog, NotifyIcon, ToolTip).
// A component's write does no touch (components do not paint); each test
// writes the properties through their cells or views, checks that a refusal
// leaves the value unchanged, that reset writes every default, and, for the
// window-bound components, that a write from another thread is refused before
// anything is compared or stored.

#include "gui_forms/components/color_dialog/color_dialog.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/components/folder_browser_dialog/folder_browser_dialog.hpp"
#include "gui_forms/components/notify_icon/notify_icon.hpp"
#include "gui_forms/components/open_file_dialog/open_file_dialog.hpp"
#include "gui_forms/components/save_file_dialog/save_file_dialog.hpp"
#include "gui_forms/components/tool_tip/tool_tip.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/host/image/host_image.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/tool_tips/tool_tips.hpp"
#include "gui_forms/window/window.hpp"
#include "headless_host.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_component_capabilities_tests: " << message << '\n';
    std::exit(1);
}

HostImage solid_image(std::uint32_t side) {
    HostImage image;
    image.width = side;
    image.height = side;
    image.row_bytes = static_cast<std::uint64_t>(side) * 4U;
    image.pixels.assign(static_cast<std::size_t>(image.row_bytes) * side, std::byte{0x7F});
    return image;
}

void test_color_dialog() {
    ColorDialog dialog;
    dialog.title = "Accent";
    dialog.color = Color::rgba(10U, 20U, 30U, 128U);
    dialog.allow_alpha = true;
    require(dialog.title.read() == "Accent" && dialog.color.read() == Color::rgba(10U, 20U, 30U, 128U) &&
                dialog.allow_alpha.read(),
            "ColorDialog writes store");
    bool refused = false;
    try {
        dialog.title = std::string("bad\0title", 9U);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && dialog.title.read() == "Accent", "a title holding NUL is refused and the old one kept");
    dialog.reset();
    require(dialog.title.read().empty() && dialog.color.read() == Color::rgba(0U, 0U, 0U, 255U) &&
                !dialog.allow_alpha.read(),
            "ColorDialog reset writes every default");
}

void test_open_file_dialog() {
    OpenFileDialog dialog;
    dialog.title = "Open recordings";
    dialog.initial_directory = "/tmp";
    dialog.filter = "Recordings|*.wav;*.flac|All files|*.*";
    dialog.filter_index = std::size_t{1U};
    dialog.multiselect = true;
    require(dialog.filters().size() == 2U && dialog.filters()[0].extensions.size() == 2U,
            "a filter write parses the host filters");
    require(dialog.title.read() == "Open recordings" && dialog.initial_directory.read() == "/tmp" &&
                dialog.filter_index.read() == 1U && dialog.multiselect.read(),
            "OpenFileDialog writes store");
    bool refused = false;
    try {
        dialog.filter = "Recordings|wav";
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && dialog.filter.read() == "Recordings|*.wav;*.flac|All files|*.*" && dialog.filters().size() == 2U,
            "a malformed filter is refused and the old filter and its parse kept");

    dialog.file_name = "/tmp/a.wav";
    require(dialog.file_name.read() == "/tmp/a.wav" && dialog.file_names().size() == 1U,
            "a file name write makes it the one chosen path");
    dialog.file_name = std::string{};
    require(dialog.file_name.read().empty() && dialog.file_names().empty(), "an empty file name chooses none");

    dialog.reset();
    require(dialog.title.read().empty() && dialog.initial_directory.read().empty() && dialog.filter.read().empty() &&
                dialog.filters().empty() && dialog.filter_index.read() == 0U && !dialog.multiselect.read(),
            "OpenFileDialog reset writes every default");
}

void test_save_file_dialog() {
    SaveFileDialog dialog;
    require(dialog.add_extension.read() && dialog.overwrite_prompt.read(), "both prompts start on");
    dialog.default_ext = ".txt";
    require(dialog.default_ext.read() == "txt", "a leading dot is removed from the default extension");
    bool refused = false;
    try {
        dialog.default_ext = "t*t";
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && dialog.default_ext.read() == "txt", "a wildcard extension is refused and the old one kept");
    dialog.add_extension = false;
    dialog.overwrite_prompt = false;
    dialog.file_name = "summary";
    require(!dialog.add_extension.read() && !dialog.overwrite_prompt.read() && dialog.file_name.read() == "summary",
            "SaveFileDialog writes store");
    dialog.reset();
    require(dialog.default_ext.read().empty() && dialog.add_extension.read() && dialog.overwrite_prompt.read() &&
                dialog.file_name.read().empty(),
            "SaveFileDialog reset writes every default");
}

void test_folder_browser_dialog() {
    FolderBrowserDialog dialog;
    dialog.description = "Choose where the exports go";
    dialog.initial_directory = "/tmp";
    dialog.selected_path = "/tmp/exports";
    require(dialog.description.read() == "Choose where the exports go" && dialog.initial_directory.read() == "/tmp" &&
                dialog.selected_path.read() == "/tmp/exports",
            "FolderBrowserDialog writes store");
    bool refused = false;
    try {
        dialog.selected_path = std::string("\xff");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && dialog.selected_path.read() == "/tmp/exports", "an invalid path is refused and the old one kept");
    dialog.reset();
    require(dialog.description.read().empty() && dialog.initial_directory.read().empty() &&
                dialog.selected_path.read().empty(),
            "FolderBrowserDialog reset writes every default");
}

// Writes the text from another thread and records whether it was refused.
struct OtherThreadText final {
    NotifyIcon* icon{};
    bool* refused{};
    void operator()() const {
        try {
            (*icon).text = "elsewhere";
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

// Writes the same visibility from another thread.
struct OtherThreadVisible final {
    NotifyIcon* icon{};
    bool* refused{};
    void operator()() const {
        try {
            (*icon).visible = true;
        } catch (const std::logic_error&) {
            *refused = true;
        }
    }
};

void test_notify_icon() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("caps.notify.root"));
    Window window(root, {320.0, 200.0});
    host::HeadlessHost headless(window);
    host::HeadlessHostServices& services = static_cast<host::HeadlessHostServices&>(headless.services());
    NotifyIcon icon(window);
    icon.icon = std::vector<HostImage>{solid_image(16U)};
    icon.text = "Radio";
    icon.visible = true;
    require(icon.shown() && icon.status().accepted() && icon.visible.read() && icon.icon.read().size() == 1U,
            "NotifyIcon writes show the icon");

    const std::uint64_t updates = services.snapshot().tray_icon_updates;
    icon.text = "Radio";
    require(services.snapshot().tray_icon_updates == updates, "an equal text write updates nothing");
    icon.text = "Radio, muted";
    require(services.snapshot().tray_icon_updates == updates + 1U, "a changed text write updates the shown icon");
    icon.icon = std::vector<HostImage>{solid_image(16U)};
    require(services.snapshot().tray_icon_updates == updates + 2U, "every icon write updates the shown icon");

    bool refused = false;
    std::thread text_worker(OtherThreadText{&icon, &refused});
    text_worker.join();
    require(refused && icon.text.read() == "Radio, muted", "a text write from another thread is refused");
    refused = false;
    std::thread visible_worker(OtherThreadVisible{&icon, &refused});
    visible_worker.join();
    require(refused, "an equal visible write from another thread is refused");

    const std::shared_ptr<ContextMenu> menu = std::make_shared<ContextMenu>("caps.notify.menu");
    icon.context_menu = menu;
    require(icon.context_menu.read() == menu, "a context menu write stores");
    icon.visible = false;
    require(!icon.shown() && !icon.visible.read() && services.snapshot().tray_icons == 0U, "hiding removes the icon");
    icon.dispose();
}

void test_tool_tip() {
    using namespace std::chrono_literals;
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("caps.tips.root"));
    Window window(root, {320.0, 200.0});
    ToolTip tips(window);
    tips.initial_delay = 30ms;
    tips.reshow_delay = 10ms;
    tips.auto_pop_delay = 900ms;
    tips.show_on_hover = false;
    tips.show_on_focus = false;
    tips.show_always = true;
    tips.maximum_width = 200.0;
    const ToolTipSettings settings = window.tool_tip_settings();
    require(settings.initial_delay == 30ms && settings.reshow_delay == 10ms && settings.auto_pop_delay == 900ms &&
                !settings.show_on_hover && !settings.show_on_focus && settings.show_when_disabled &&
                settings.maximum_width == 200.0,
            "ToolTip writes reach the window's settings");
    require(tips.initial_delay.read() == 30ms && tips.show_always.read() && tips.maximum_width.read() == 200.0,
            "ToolTip reads come from the window's settings");
    bool refused = false;
    try {
        tips.initial_delay = -1ms;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && tips.initial_delay.read() == 30ms, "a negative delay is refused and the old one kept");
    tips.dispose();
}

} // namespace

int main() {
    test_color_dialog();
    test_open_file_dialog();
    test_save_file_dialog();
    test_folder_browser_dialog();
    test_notify_icon();
    test_tool_tip();
    std::cout << "component capability tests passed\n";
    return EXIT_SUCCESS;
}
