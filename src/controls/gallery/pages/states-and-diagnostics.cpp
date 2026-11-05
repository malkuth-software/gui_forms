#include "pages.hpp"

namespace gui_forms::gallery::pages {

class StatesPage;

// One named step of the dispatcher demonstration. begin_invoke/invoke take an
// owning callable, so this small posted record carries the step back to the
// page's named handler; the page is borrowed weakly because posted work may
// outlive it.
enum class DispatcherStep : int {
    append_a,
    append_b_then_post_d,
    commit_turn_one,
    commit_turn_two,
    cancelled_callback,
    worker_released,
};

class PostedStep final {
public:
    PostedStep(std::weak_ptr<StatesPage> page, DispatcherStep step) noexcept
        : page_(std::move(page)), step_(step) {}

    void operator()() const;

private:
    std::weak_ptr<StatesPage> page_;
    DispatcherStep step_{};
};

// Owns the states page: the border/lifecycle specimens, the MetricsView card
// and the UI dispatcher demonstration whose three buttons bind Commands.
class StatesPage final : public gui_forms::Component {
public:
    StatesPage(const std::shared_ptr<Surface>& body,
               const std::shared_ptr<PageContext>& context);
    ~StatesPage() override;

    // The container keeps the page alive; posted dispatcher work borrows it.
    void adopt(const std::shared_ptr<StatesPage>& self) noexcept { self_ = self; }
    void run_step(DispatcherStep step);

private:
    void build_specimens(const std::shared_ptr<Surface>& body);
    void build_dispatcher(const std::shared_ptr<Surface>& body);
    void on_post();
    void on_cancel();
    void on_invoke_from_worker();
    void worker_main();
    [[nodiscard]] DispatcherSnapshot dispatcher_snapshot() const;

    std::weak_ptr<StatesPage> self_;
    Command post_{};
    Command cancel_{};
    Command invoke_{};
    std::shared_ptr<Label> status_;
    std::vector<DispatchOperation> operations_;
    std::thread worker_;
    std::string trace_;
    std::uint64_t batch_{};
};

void PostedStep::operator()() const {
    const std::shared_ptr<StatesPage> page = page_.lock();
    if (page) (*page).run_step(step_);
}

StatesPage::StatesPage(const std::shared_ptr<Surface>& body,
                       const std::shared_ptr<PageContext>& context) {
    static_cast<void>(context);
    (*body).add_at(label("gallery.states.heading", "STATES AND DIAGNOSTICS", 22.0, 700,
                         shell_blue_dark), {24.0, 18.0, 560.0, 34.0});
    (*body).add_at(label("gallery.states.lead",
                         "Focus, disabled, checked, indeterminate, visited, border, and runtime accounting.",
                         12.0, 400, muted), {26.0, 52.0, 900.0, 24.0});
    build_specimens(body);
    build_dispatcher(body);
}

StatesPage::~StatesPage() {
    if (worker_.joinable()) worker_.join();
}

void StatesPage::build_specimens(const std::shared_ptr<Surface>& body) {
    const std::array<std::pair<BorderStyle, std::string_view>, 4> borders{{
        {BorderStyle::none, "No border"}, {BorderStyle::line, "Line"},
        {BorderStyle::sunken, "Sunken"}, {BorderStyle::raised, "Raised"},
    }};
    for (std::size_t index = 0; index < borders.size(); ++index) {
        std::shared_ptr<Panel> panel = make_control<Panel>(
            StableId("gallery.states.border." + std::to_string(index)));
        (*panel).set_background(index % 2U == 0U ? paper : Color::rgba(230, 238, 245));
        (*panel).set_border_style(borders[index].first);
        (*body).add_at(panel, {24.0 + static_cast<double>(index) * 232.0,
                               98.0, 210.0, 104.0});
        std::shared_ptr<Label> caption = label(
            "gallery.states.border.caption." + std::to_string(index),
            std::string(borders[index].second), 12.0, 600, shell_blue_dark);
        (*caption).set_requested_bounds({16.0, 18.0, 170.0, 24.0});
        (*panel).add_child(caption);
        std::shared_ptr<Label> state = label(
            "gallery.states.border.state." + std::to_string(index),
            "Retained panel", 11.0, 400, muted);
        (*state).set_requested_bounds({16.0, 52.0, 170.0, 24.0});
        (*panel).add_child(state);
    }

    std::shared_ptr<LayoutGroup> lifecycle = group(
        "gallery.states.lifecycle", "Lifecycle and interaction states", {450.0, 240.0});
    (*body).add_at(lifecycle, {24.0, 226.0, 450.0, 240.0});
    std::shared_ptr<CheckBox> checked =
        make_control<CheckBox>(StableId("gallery.states.checked"), "Checked");
    (*checked).set_checked(true);
    (*lifecycle).add_at(checked, {24.0, 42.0, 180.0, 28.0});
    std::shared_ptr<CheckBox> indeterminate = make_control<CheckBox>(
        StableId("gallery.states.indeterminate"), "Indeterminate");
    (*indeterminate).set_three_state(true);
    (*indeterminate).set_check_state(CheckState::indeterminate);
    (*lifecycle).add_at(indeterminate, {24.0, 82.0, 180.0, 28.0});
    std::shared_ptr<Button> unavailable = make_control<Button>(
        StableId("gallery.states.unavailable"), "Unavailable action");
    (*unavailable).enabled = false;
    (*lifecycle).add_at(unavailable, {230.0, 42.0, 180.0, 34.0});
    std::shared_ptr<LinkLabel> visited = make_control<LinkLabel>(
        StableId("gallery.states.visited"), "Visited link state");
    (*visited).set_visited(true);
    (*lifecycle).add_at(visited, {230.0, 88.0, 180.0, 28.0});
    std::shared_ptr<Label> cursor_note = label(
        "gallery.states.cursor",
        "Cursor roles: arrow · text · hand · resize · wait · forbidden",
        11.0, 400, muted);
    (*lifecycle).add_at(cursor_note, {24.0, 142.0, 390.0, 42.0});

    std::shared_ptr<DiagnosticsCard> diagnostics =
        make_control<DiagnosticsCard>(StableId("gallery.states.diagnostics"));
    (*diagnostics).set_title("RETAINED RUNTIME");
    BasicControlStyle diagnostics_style = page_style();
    diagnostics_style.text = Color::rgba(28, 39, 50);
    diagnostics_style.face = Color::rgba(175, 203, 221);
    diagnostics_style.face_light = Color::rgba(226, 238, 246);
    diagnostics_style.accent = green;
    (*diagnostics).set_style(diagnostics_style);
    (*body).add_at(diagnostics, {494.0, 226.0, 460.0, 240.0});
}

void StatesPage::build_dispatcher(const std::shared_ptr<Surface>& body) {
    std::shared_ptr<LayoutGroup> dispatcher = group(
        "gallery.states.dispatcher", "UI dispatcher · posted, bounded, cancellable",
        {930.0, 134.0});
    (*body).add_at(dispatcher, {24.0, 488.0, 930.0, 134.0});

    post_.set_text("Post A · B · C → D");
    post_.set_description("Post three callbacks; one of them posts a fourth");
    cancel_.set_text("Post then cancel");
    cancel_.set_description("Post one callback and cancel it before dispatch");
    invoke_.set_text("Worker Invoke");
    invoke_.set_description("Block a worker thread on a synchronous UI invocation");

    std::shared_ptr<Button> post =
        make_control<Button>(StableId("gallery.dispatcher.post"), "Post A · B · C → D");
    (*post).set_visual_style(ButtonVisualStyle::accent);
    (*post).bind(post_);
    std::shared_ptr<Button> cancel =
        make_control<Button>(StableId("gallery.dispatcher.cancel"), "Post then cancel");
    (*cancel).bind(cancel_);
    std::shared_ptr<Button> invoke =
        make_control<Button>(StableId("gallery.dispatcher.invoke"), "Worker Invoke");
    (*invoke).bind(invoke_);
    (*dispatcher).add_at(post, {22.0, 40.0, 174.0, 36.0});
    (*dispatcher).add_at(cancel, {208.0, 40.0, 154.0, 36.0});
    (*dispatcher).add_at(invoke, {374.0, 40.0, 170.0, 36.0});
    status_ = label("gallery.dispatcher.status",
                    "Ready · BeginInvoke never executes inside the click callback",
                    11.0, 600, green);
    (*status_).set_text_wrapping(TextWrapping::word);
    (*status_).set_vertical_alignment(VerticalAlignment::near);
    (*dispatcher).add_at(status_, {566.0, 36.0, 328.0, 58.0});

    on(post_.invoked(), *this, &StatesPage::on_post);
    on(cancel_.invoked(), *this, &StatesPage::on_cancel);
    on(invoke_.invoked(), *this, &StatesPage::on_invoke_from_worker);
}

DispatcherSnapshot StatesPage::dispatcher_snapshot() const {
    Window* const window = (*status_).attached_window();
    if (window == nullptr) return {};
    return (*window).dispatcher_snapshot();
}

void StatesPage::run_step(const DispatcherStep step) {
    switch (step) {
    case DispatcherStep::append_a:
        trace_.push_back('A');
        return;
    case DispatcherStep::append_b_then_post_d:
        trace_.push_back('B');
        operations_.push_back((*status_).begin_invoke(
            PostedStep(self_, DispatcherStep::commit_turn_two)));
        return;
    case DispatcherStep::commit_turn_one:
        trace_.push_back('C');
        (*status_).set_text("Turn 1 committed · order " + trace_ +
                            " · nested D remains posted");
        return;
    case DispatcherStep::commit_turn_two: {
        trace_.push_back('D');
        const DispatcherSnapshot snapshot = dispatcher_snapshot();
        (*status_).set_text("Turn 2 committed · order " + trace_ +
                            " · invoked " + std::to_string(snapshot.invoked) +
                            " · faults " + std::to_string(snapshot.faulted));
        return;
    }
    case DispatcherStep::cancelled_callback:
        (*status_).set_text("ERROR · cancelled callback executed");
        return;
    case DispatcherStep::worker_released: {
        const DispatcherSnapshot snapshot = dispatcher_snapshot();
        (*status_).set_text("Worker released after UI callback · marshalled " +
                            std::to_string(snapshot.marshalled_invocations));
        invoke_.set_enabled(true);
        return;
    }
    }
    // A step outside the enumeration was never posted; nothing runs.
    return;
}

void StatesPage::on_post() {
    ++batch_;
    trace_.clear();
    operations_.clear();
    (*status_).set_text("Click returned · batch " + std::to_string(batch_) +
                        " is pending");
    operations_.push_back((*status_).begin_invoke(
        PostedStep(self_, DispatcherStep::append_a)));
    operations_.push_back((*status_).begin_invoke(
        PostedStep(self_, DispatcherStep::append_b_then_post_d)));
    operations_.push_back((*status_).begin_invoke(
        PostedStep(self_, DispatcherStep::commit_turn_one)));
}

void StatesPage::on_cancel() {
    operations_.clear();
    DispatchOperation operation = (*status_).begin_invoke(
        PostedStep(self_, DispatcherStep::cancelled_callback));
    const std::uint64_t sequence = operation.sequence();
    const bool cancelled = operation.cancel();
    operations_.push_back(std::move(operation));
    (*status_).set_text("Cancelled posted operation #" + std::to_string(sequence) +
                        (cancelled ? " before dispatch" : " too late"));
}

void StatesPage::on_invoke_from_worker() {
    // The Command's enabled state projects to its button while the worker
    // waits; the posted completion step re-enables it on the UI thread.
    invoke_.set_enabled(false);
    (*status_).set_text("Worker blocked · no nested pump · waiting for UI turn");
    if (worker_.joinable()) worker_.join();
    worker_ = std::thread(&StatesPage::worker_main, this);
}

void StatesPage::worker_main() {
    try {
        (*status_).invoke(PostedStep(self_, DispatcherStep::worker_released));
    } catch (const DispatchCancelledError&) {
        // Window shutdown is the terminal owner of this worker.
    }
}

void add_states(const std::shared_ptr<Surface>& page,
                const std::shared_ptr<PageContext>& context) {
    std::shared_ptr<StatesPage> owner = std::make_shared<StatesPage>(page, context);
    (*owner).adopt(owner);
    (*context).components.add(std::move(owner));
}

} // namespace gui_forms::gallery::pages
