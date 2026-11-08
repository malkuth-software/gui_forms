#include "../application/linux_host_internal.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/panel/combo_box/combo_box.hpp"
#include "gui_forms/controls/panel/list_box/list_box.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace gui_forms::host::linux_detail {
namespace {
struct DialogState;
struct ButtonAction final {
  DialogState *state;
  int action;
  void operator()(ButtonBase &) const;
};
struct ActivateFile final {
  DialogState *state;
  void operator()(std::size_t index) const;
};
struct SelectFile final {
  DialogState *state;
  void operator()(const ListSelectionChange &change) const;
};
struct ChangeFilter final {
  DialogState *state;
  void operator()(std::optional<std::size_t>) const;
};
struct EnterName final {
  DialogState *state;
  void operator()(const std::string &) const;
};
struct EnterPath final {
  DialogState *state;
  void operator()(const std::string &) const;
};
struct DialogClose final {
  DialogState *state;
  void operator()() const;
};
// F1 in a message dialog asks for help, as its Help button does.
struct DialogHelpKey final {
  DialogState *state;
  bool operator()() const;
};
struct Entry final {
  std::filesystem::path path;
  bool directory{};
};
struct EntryLess final {
  bool operator()(const Entry &a, const Entry &b) const {
    if (a.directory != b.directory)
      return a.directory;
    return a.path.filename().string() < b.path.filename().string();
  }
};
struct DialogState final {
  NativeWindow &owner;
  const HostDialogRequest &request;
  std::shared_ptr<Panel> root;
  std::shared_ptr<TextBox> location;
  std::shared_ptr<TextBox> name;
  std::shared_ptr<ListBox> list;
  std::shared_ptr<ComboBox> filter;
  std::shared_ptr<Label> status;
  std::shared_ptr<NativeWindow> native;
  std::shared_ptr<Button> accept_button;
  std::shared_ptr<Button> cancel_button;
  std::vector<SubscriptionToken> tokens;
  std::vector<Entry> entries;
  std::vector<HostFileDialogFilter> filters;
  std::size_t initial_filter{};
  std::filesystem::path directory;
  HostDialogResult result;
  bool done{};
  bool save{};
  bool folder{};
  bool multiple{};
  bool confirm_overwrite{};
  std::string default_extension;
  std::uint32_t color{};
  bool allow_alpha{};
  explicit DialogState(NativeWindow &host, const HostDialogRequest &source)
      : owner(host), request(source) {
    result.request_id = request.request_id;
    root = make_control<Panel>(StableId("host.dialog.root"));
    (*root).set_background({244, 246, 249, 255});
  }
  std::shared_ptr<Button> button(std::string id, std::string text, Rect bounds,
                                 int action) {
    const std::shared_ptr<Button> control =
        make_control<Button>(StableId(std::move(id)), std::move(text));
    (*control).set_bounds(bounds);
    (*control).font = FontSpec{FontRole::control, 14, 400, false};
    (*root).add_child(control);
    tokens.push_back(
        (*control).clicked().subscribe(ButtonAction{this, action}));
    return control;
  }
  // The dialog's help request (its Help button, or F1); it stays open.
  void help(bool keyboard_initiated) {
    if (!owner.services)
      return;
    HostDialogHelpEvent event{};
    event.request_id = request.request_id;
    event.keyboard_initiated = keyboard_initiated;
    static_cast<void>((*owner.services).publish_dialog_help(event));
  }
  void finish(HostDialogChoice choice) {
    result.payload = HostMessageDialogResult{choice == HostDialogChoice::cancel
                                                 ? HostDialogOutcome::cancelled
                                                 : HostDialogOutcome::accepted,
                                             choice};
    done = true;
  }
  void refresh(std::filesystem::path destination) {
    std::error_code error;
    destination = std::filesystem::absolute(destination, error);
    if (error || !std::filesystem::is_directory(destination, error)) {
      (*status).set_text("Choose an existing folder.");
      return;
    }
    directory = destination.lexically_normal();
    (*location).text = directory.string();
    entries.clear();
    const std::filesystem::directory_iterator end;
    std::filesystem::directory_iterator iterator(
        directory, std::filesystem::directory_options::skip_permission_denied,
        error);
    for (; !error && iterator != end; iterator.increment(error)) {
      const std::filesystem::directory_entry &item = *iterator;
      const bool is_directory = item.is_directory(error);
      if (error) {
        error.clear();
        continue;
      }
      bool show = is_directory || !folder;
      if (show && !is_directory && filter && (*filter).selected_index.read() &&
          *(*filter).selected_index.read() < filters.size()) {
        const HostFileDialogFilter &selected =
            filters[*(*filter).selected_index.read()];
        std::string extension = item.path().extension().string();
        if (!extension.empty())
          extension.erase(0, 1);
        for (std::size_t k = 0; k < extension.size(); ++k)
          extension[k] = static_cast<char>(
              std::tolower(static_cast<unsigned char>(extension[k])));
        show = selected.extensions.empty();
        for (std::size_t k = 0; k < selected.extensions.size(); ++k) {
          std::string expected = selected.extensions[k];
          if (expected == "*" || expected == "*.*") {
            show = true;
            break;
          }
          while (!expected.empty() &&
                 (expected.front() == '.' || expected.front() == '*'))
            expected.erase(0, 1);
          if (extension == expected)
            show = true;
        }
      }
      if (show)
        entries.push_back({item.path(), is_directory});
    }
    std::sort(entries.begin(), entries.end(), EntryLess{});
    std::vector<std::string> names;
    for (std::size_t i = 0; i < entries.size(); ++i)
      names.push_back(entries[i].path.filename().string() +
                      (entries[i].directory ? "/" : ""));
    (*list).items = std::move(names);
    (*status).set_text(error ? error.message() : "");
  }
  void activate(std::size_t index) {
    if (index >= entries.size())
      return;
    if (entries[index].directory)
      refresh(entries[index].path);
    else {
      (*name).text = entries[index].path.filename().string();
      accept();
    }
  }
  void accept() {
    if (std::holds_alternative<HostColorDialogRequest>(request.payload)) {
      std::string value((*name).text.read());
      if (!value.empty() && value.front() == '#')
        value.erase(0, 1);
      if (value.size() != 6 && !(allow_alpha && value.size() == 8)) {
        (*status).set_text(
            "Enter six hexadecimal color digits, or eight with alpha.");
        return;
      }
      std::uint32_t parsed{};
      for (std::size_t i = 0; i < value.size(); ++i) {
        const char digit = value[i];
        const int number = digit >= '0' && digit <= '9'   ? digit - '0'
                           : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10
                           : digit >= 'A' && digit <= 'F' ? digit - 'A' + 10
                                                          : -1;
        if (number < 0) {
          (*status).set_text("Use hexadecimal digits 0–9 and A–F.");
          return;
        }
        parsed = (parsed << 4U) | static_cast<std::uint32_t>(number);
      }
      if (value.size() == 6)
        parsed = (parsed << 8U) | 255U;
      result.payload =
          HostColorDialogResult{HostDialogOutcome::accepted, parsed};
      done = true;
      return;
    }
    std::error_code error;
    std::vector<std::string> paths;
    std::filesystem::path candidate =
        (*name).text.read().empty() ? directory
                               : std::filesystem::path((*name).text.read());
    if (candidate.is_relative())
      candidate = directory / candidate;
    candidate = candidate.lexically_normal();
    if (std::filesystem::is_directory(candidate, error)) {
      if (!folder) {
        refresh(candidate);
        return;
      }
      paths.push_back(candidate.string());
    } else if (folder) {
      (*status).set_text("Choose an existing folder.");
      return;
    } else if (save) {
      if (candidate.extension().empty() && !default_extension.empty())
        candidate += default_extension.front() == '.' ? default_extension
                                                      : "." + default_extension;
      if (!std::filesystem::is_directory(candidate.parent_path(), error)) {
        (*status).set_text("The destination folder does not exist.");
        return;
      }
      if (confirm_overwrite && std::filesystem::exists(candidate, error)) {
        HostDialogRequest confirmation;
        confirmation.request_id = request.request_id + 1;
        confirmation.payload = HostMessageDialogRequest{
            "Replace file?",
            candidate.filename().string() + " already exists. Replace it?",
            HostMessageButtons::yes_no, HostMessageIcon::warning,
            HostDialogChoice::no};
        const HostDialogResult answer = retained_dialog(*native, confirmation);
        const HostMessageDialogResult *message =
            std::get_if<HostMessageDialogResult>(&answer.payload);
        if (message == nullptr || (*message).choice != HostDialogChoice::yes)
          return;
      }
      paths.push_back(candidate.string());
    } else {
      if (multiple && (*list).selected_indices().size() > 1) {
        const std::span<const std::size_t> selected =
            (*list).selected_indices();
        for (std::size_t i = 0; i < selected.size(); ++i)
          if (selected[i] < entries.size() && !entries[selected[i]].directory)
            paths.push_back(entries[selected[i]].path.string());
      } else if (std::filesystem::is_regular_file(candidate, error))
        paths.push_back(candidate.string());
      if (paths.empty()) {
        (*status).set_text("Choose an existing file.");
        return;
      }
    }
    HostPathDialogResult accepted{HostDialogOutcome::accepted,
                                  std::move(paths)};
    // The added "All files" is no filter of the request.
    if (!folder && filter && (*filter).selected_index.read() &&
        *(*filter).selected_index.read() < filters.size())
      accepted.chosen_filter = *(*filter).selected_index.read();
    result.payload = std::move(accepted);
    done = true;
  }
};
void ButtonAction::operator()(ButtonBase &) const {
  if (action == 50) {
    (*state).help(false);
    return;
  }
  if (action >= 100) {
    (*state).finish(static_cast<HostDialogChoice>(action - 100));
    return;
  }
  if (action == 0)
    (*state).done = true;
  else if (action == 1)
    (*state).accept();
  else if (action == 2)
    (*state).refresh((*state).directory.parent_path());
  else if (action == 3)
    (*state).refresh((*(*state).location).text.read());
}
void ActivateFile::operator()(std::size_t index) const {
  (*state).activate(index);
}
void SelectFile::operator()(const ListSelectionChange &change) const {
  if (change.active_index && *change.active_index < (*state).entries.size())
    (*(*state).name).text =
        (*state).entries[*change.active_index].path.filename().string();
}
void ChangeFilter::operator()(std::optional<std::size_t>) const {
  (*state).refresh((*state).directory);
}
void EnterName::operator()(const std::string &) const { (*state).accept(); }
void EnterPath::operator()(const std::string &) const {
  (*state).refresh((*(*state).location).text.read());
}
void DialogClose::operator()() const { (*state).done = true; }
bool DialogHelpKey::operator()() const {
  (*state).help(true);
  return true;
}
std::shared_ptr<Label> label(DialogState &state, const char *id,
                             std::string text, Rect bounds) {
  const std::shared_ptr<Label> control =
      make_control<Label>(StableId(std::string(id)), std::move(text));
  (*control).set_font({FontRole::content, 15, 400, false});
  (*control).set_text_wrapping(TextWrapping::word);
  (*control).set_bounds(bounds);
  (*state.root).add_child(control);
  return control;
}
// A message box button's text: the choice's name in title case, with
// Windows' wording for Try Again.
std::string choice_label(HostDialogChoice choice) {
  if (choice == HostDialogChoice::try_again)
    return "Try Again";
  std::string text = host_dialog_choice_name(choice);
  if (!text.empty())
    text[0] =
        static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
  return text;
}
struct ModalScope final {
  NativeWindow &owner;
  bool previous;
  explicit ModalScope(NativeWindow &window)
      : owner(window), previous(window.modal) {
    owner.modal = true;
  }
  ~ModalScope() { owner.modal = previous; }
};
} // namespace
HostDialogResult retained_dialog(NativeWindow &owner,
                                 const HostDialogRequest &request) {
  DialogState state(owner, request);
  LinuxApplicationWindow entry;
  entry.stable_id = "host.dialog";
  entry.owner_id = owner.entry.stable_id;
  entry.tool_window = true;
  entry.options.initial_size = {720, 540};
  entry.options.minimum_size = entry.options.initial_size;
  entry.options.closed = DialogClose{&state};
  const HostMessageDialogRequest *message =
      std::get_if<HostMessageDialogRequest>(&request.payload);
  if (message != nullptr) {
    entry.options.title = (*message).title;
    entry.options.initial_size = {560, 250};
    entry.options.minimum_size = entry.options.initial_size;
    const std::shared_ptr<Label> message_label =
        label(state, "host.dialog.message", (*message).message,
              {22, 20, 516, 164});
    std::vector<HostDialogChoice> choices;
    switch ((*message).buttons) {
    case HostMessageButtons::ok:
      choices = {HostDialogChoice::ok};
      break;
    case HostMessageButtons::ok_cancel:
      choices = {HostDialogChoice::ok, HostDialogChoice::cancel};
      break;
    case HostMessageButtons::yes_no:
      choices = {HostDialogChoice::yes, HostDialogChoice::no};
      break;
    case HostMessageButtons::yes_no_cancel:
      choices = {HostDialogChoice::yes, HostDialogChoice::no,
                 HostDialogChoice::cancel};
      break;
    case HostMessageButtons::retry_cancel:
      choices = {HostDialogChoice::retry, HostDialogChoice::cancel};
      break;
    case HostMessageButtons::abort_retry_ignore:
      choices = {HostDialogChoice::abort, HostDialogChoice::retry,
                 HostDialogChoice::ignore};
      break;
    case HostMessageButtons::cancel_try_continue:
      choices = {HostDialogChoice::cancel, HostDialogChoice::try_again,
                 HostDialogChoice::continue_};
      break;
    }
    // Help follows the set's buttons; it asks for help and stays open.
    if ((*message).help_button)
      choices.push_back(HostDialogChoice::help);
    // Either option aligns the text to the right; the dialog's controls
    // have no right-to-left reading order of their own.
    if ((*message).right_align || (*message).rtl_reading)
      (*message_label).set_alignment(HorizontalAlignment::far);
    for (std::size_t i = 0; i < choices.size(); ++i) {
      const HostDialogChoice choice = choices[i];
      std::string name = host_dialog_choice_name(choice);
      const std::string text = choice_label(choice);
      if (!name.empty())
        name[0] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(name[0])));
      const int action =
          choice == HostDialogChoice::help ? 50 : 100 + static_cast<int>(choice);
      const std::shared_ptr<Button> button =
          state.button("host.dialog.choice." + name, text,
                       {538.0 - static_cast<double>(choices.size() - i) * 110.0,
                        200, 100, 32},
                       action);
      if (choice == (*message).default_choice)
        state.accept_button = button;
      if (choice == HostDialogChoice::cancel)
        state.cancel_button = button;
    }
    state.result.payload = HostMessageDialogResult{};
  } else if (const HostColorDialogRequest *color =
                 std::get_if<HostColorDialogRequest>(&request.payload)) {
    entry.options.title = (*color).title;
    entry.options.initial_size = {560, 230};
    entry.options.minimum_size = entry.options.initial_size;
    state.color = (*color).initial_rgba;
    state.allow_alpha = (*color).allow_alpha;
    static_cast<void>(
        label(state, "host.dialog.color.label",
              state.allow_alpha ? "Color (RRGGBBAA)" : "Color (RRGGBB)",
              {20, 20, 520, 28}));
    std::ostringstream value;
    value << std::hex;
    value.width(state.allow_alpha ? 8 : 6);
    value.fill('0');
    value << (state.allow_alpha ? state.color : state.color >> 8U);
    state.name =
        make_control<TextBox>(StableId("host.dialog.color"), value.str());
    (*state.name).set_bounds({20, 58, 520, 34});
    // Enter in the field accepts through EnterName.
    (*state.name).accepts_enter = true;
    (*state.root).add_child(state.name);
    state.status = label(state, "host.dialog.status", "", {20, 102, 520, 58});
    state.accept_button =
        state.button("host.dialog.accept", "OK", {320, 178, 100, 32}, 1);
    state.cancel_button =
        state.button("host.dialog.cancel", "Cancel", {430, 178, 110, 32}, 0);
    state.result.payload = HostColorDialogResult{};
  } else {
    std::string initial, suggested;
    if (const HostOpenFileDialogRequest *open =
            std::get_if<HostOpenFileDialogRequest>(&request.payload)) {
      entry.options.title = (*open).title;
      initial = (*open).initial_directory;
      suggested = (*open).suggested_name;
      state.filters = (*open).filters;
      state.initial_filter = (*open).initial_filter;
      state.multiple = (*open).allow_multiple;
    } else if (const HostSaveFileDialogRequest *save =
                   std::get_if<HostSaveFileDialogRequest>(&request.payload)) {
      state.save = true;
      entry.options.title = (*save).title;
      initial = (*save).initial_directory;
      suggested = (*save).suggested_name;
      state.filters = (*save).filters;
      state.initial_filter = (*save).initial_filter;
      state.default_extension = (*save).default_extension;
      state.confirm_overwrite = (*save).confirm_overwrite;
    } else if (const HostFolderDialogRequest *folder =
                   std::get_if<HostFolderDialogRequest>(&request.payload)) {
      state.folder = true;
      entry.options.title = (*folder).title;
      initial = (*folder).initial_directory;
    }
    state.location = make_control<TextBox>(StableId("host.dialog.location"));
    (*state.location).set_bounds({16, 16, 550, 32});
    // Enter in the location goes there (EnterPath).
    (*state.location).accepts_enter = true;
    (*state.root).add_child(state.location);
    static_cast<void>(
        state.button("host.dialog.go", "Go", {578, 16, 56, 32}, 3));
    static_cast<void>(
        state.button("host.dialog.up", "Up", {646, 16, 58, 32}, 2));
    state.list = make_control<ListBox>(StableId("host.dialog.files"));
    (*state.list).set_bounds({16, 60, 688, 340});
    (*state.list).font = FontSpec{FontRole::content, 15, 400, false};
    if (state.multiple)
      (*state.list).selection_mode = ListSelectionMode::multiple_extended;
    (*state.root).add_child(state.list);
    state.name =
        make_control<TextBox>(StableId("host.dialog.filename"), suggested);
    (*state.name).set_bounds({16, 410, 688, 32});
    // Enter in the file name accepts through EnterName.
    (*state.name).accepts_enter = true;
    (*state.root).add_child(state.name);
    state.filter = make_control<ComboBox>(StableId("host.dialog.filter"));
    (*state.filter).set_bounds({16, 452, 340, 30});
    std::vector<std::string> names;
    for (std::size_t i = 0; i < state.filters.size(); ++i)
      names.push_back(state.filters[i].label);
    names.push_back("All files");
    (*state.filter).items = std::move(names);
    // The request's starting filter; the added "All files" follows the
    // request's own.
    (*state.filter).selected_index = state.initial_filter < state.filters.size()
                                         ? state.initial_filter
                                         : std::size_t{0U};
    (*state.root).add_child(state.filter);
    state.status = label(state, "host.dialog.status", "", {16, 492, 450, 40});
    state.accept_button = state.button("host.dialog.accept",
                                       state.save     ? "Save"
                                       : state.folder ? "Choose"
                                                      : "Open",
                                       {478, 492, 104, 32}, 1);
    state.cancel_button =
        state.button("host.dialog.cancel", "Cancel", {594, 492, 110, 32}, 0);
    state.tokens.push_back(
        (*state.list).item_activated().subscribe(ActivateFile{&state}));
    state.tokens.push_back(
        (*state.list).selection_changed().subscribe(SelectFile{&state}));
    state.tokens.push_back((*state.filter)
                               .selected_index_changed()
                               .subscribe(ChangeFilter{&state}));
    state.tokens.push_back(
        (*state.location).committed().subscribe(EnterPath{&state}));
    const char *home = std::getenv("HOME");
    std::filesystem::path start =
        initial.empty() ? std::filesystem::path(home == nullptr ? "/" : home)
                        : std::filesystem::path(initial);
    std::error_code error;
    if (!std::filesystem::is_directory(start, error))
      start = "/";
    state.refresh(start);
    state.result.payload = HostPathDialogResult{};
  }
  entry.model = std::make_unique<gui_forms::Window>(state.root,
                                                    entry.options.initial_size);
  if (state.accept_button) {
    (*entry.model).set_accept_button(state.accept_button);
    (*state.accept_button).set_default_button(true);
  }
  if (state.cancel_button)
    (*entry.model).set_cancel_button(state.cancel_button);
  if (state.name)
    state.tokens.push_back(
        (*state.name).committed().subscribe(EnterName{&state}));
  AcceleratorToken help_key;
  if (message != nullptr)
    help_key = (*entry.model)
                   .register_accelerator(
                       *state.root, KeyGesture{PhysicalKey::f1, Modifier::none},
                       DialogHelpKey{&state});
  const ModalScope modal(owner);
  state.native = owner.runtime.add(std::move(entry), owner.xid);
  if (state.name)
    static_cast<void>((*(*state.native).entry.model).request_focus(state.name));
  while (!state.done && !owner.closed)
    owner.runtime.step();
  help_key.disconnect();
  (*state.native).close(HostCloseReason::application, true);
  for (std::size_t i = 0; i < owner.runtime.windows.size(); ++i)
    if (owner.runtime.windows[i] == state.native) {
      owner.runtime.windows.erase(owner.runtime.windows.begin() +
                                  static_cast<std::ptrdiff_t>(i));
      break;
    }
  state.tokens.clear();
  state.native.reset();
  return state.result;
}
} // namespace gui_forms::host::linux_detail
