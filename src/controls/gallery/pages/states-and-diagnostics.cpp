#include "pages.hpp"

#include "states-and-diagnostics_form.hpp"

#include <array>
#include <vector>

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

// Owns the states page: the controls demo/pages/states-and-diagnostics.form
// describes and their behaviour (the border and lifecycle specimens, the
// DiagnosticsCard and the UI dispatcher whose three buttons bind Commands).
class StatesPage final : public gui_forms::Component {
public:
    StatesPage(const std::shared_ptr<Surface>& body,
               const std::shared_ptr<PageContext>& context);
    ~StatesPage() override;

    // The container keeps the page alive; posted dispatcher work borrows it.
    void adopt(const std::shared_ptr<StatesPage>& self) noexcept { self_ = self; }
    void run_step(DispatcherStep step);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void build_diagnostics();
    void build_specimens();
    void build_dispatcher();
    void on_post();
    void on_cancel();
    void on_invoke_from_worker();
    void worker_main();
    [[nodiscard]] DispatcherSnapshot dispatcher_snapshot() const;

    std::weak_ptr<StatesPage> self_;
    Command post_{};
    Command cancel_{};
    Command invoke_{};
    // Declared after the Commands its buttons bind, so it is released first.
    StatesDocument<StatesPage> document_{};
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
    document_.initialize_component(*this);
    mount_document(body);
    build_specimens();
    build_diagnostics();
    build_dispatcher();
}

StatesPage::~StatesPage() {
    if (worker_.joinable()) worker_.join();
}

// The document's root is a declared boundary only: its children move into
// the shell's page body, where each is placed by its bounds in design
// coordinates. The copy of the child list keeps the iteration valid while
// the root shrinks.
void StatesPage::mount_document(const std::shared_ptr<Surface>& body) {
    Control& root = *document_.gallery_states_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
    (*document_.gallery_states_lifecycle).set_style(page_style());
    (*document_.gallery_states_dispatcher).set_style(page_style());
    status_ = document_.gallery_dispatcher_status;
}

// The retained-runtime card (a MetricsView): its title and colours.
void StatesPage::build_diagnostics() {
    DiagnosticsCard& diagnostics = *document_.gallery_states_diagnostics;
    diagnostics.set_title("RETAINED RUNTIME");
    BasicControlStyle diagnostics_style = page_style();
    diagnostics_style.text = Color::rgba(28, 39, 50);
    diagnostics_style.face = Color::rgba(175, 203, 221);
    diagnostics_style.face_light = Color::rgba(226, 238, 246);
    diagnostics_style.accent = green;
    diagnostics.set_style(diagnostics_style);
}

// The panels' border styles are in the document; their alternating
// backgrounds are the page's.
void StatesPage::build_specimens() {
    const std::array<std::shared_ptr<Panel>, 4> panels{
        document_.gallery_states_border_0, document_.gallery_states_border_1,
        document_.gallery_states_border_2, document_.gallery_states_border_3};
    for (std::size_t index = 0; index < panels.size(); ++index) {
        (*panels[index]).set_background(index % 2U == 0U ? paper : Color::rgba(230, 238, 245));
    }
    (*document_.gallery_states_visited).set_visited(true);
}

void StatesPage::build_dispatcher() {
    post_.set_text("Post A · B · C → D");
    post_.set_description("Post three callbacks; one of them posts a fourth");
    cancel_.set_text("Post then cancel");
    cancel_.set_description("Post one callback and cancel it before dispatch");
    invoke_.set_text("Worker Invoke");
    invoke_.set_description("Block a worker thread on a synchronous UI invocation");
    (*document_.gallery_dispatcher_post).bind(post_);
    (*document_.gallery_dispatcher_cancel).bind(cancel_);
    (*document_.gallery_dispatcher_invoke).bind(invoke_);
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
        (*status_).text = "Turn 1 committed · order " + trace_ +
                            " · nested D remains posted";
        return;
    case DispatcherStep::commit_turn_two: {
        trace_.push_back('D');
        const DispatcherSnapshot snapshot = dispatcher_snapshot();
        (*status_).text = "Turn 2 committed · order " + trace_ +
                            " · invoked " + std::to_string(snapshot.invoked) +
                            " · faults " + std::to_string(snapshot.faulted);
        return;
    }
    case DispatcherStep::cancelled_callback:
        (*status_).text = "ERROR · cancelled callback executed";
        return;
    case DispatcherStep::worker_released: {
        const DispatcherSnapshot snapshot = dispatcher_snapshot();
        (*status_).text = "Worker released after UI callback · marshalled " +
                            std::to_string(snapshot.marshalled_invocations);
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
    (*status_).text = "Click returned · batch " + std::to_string(batch_) +
                        " is pending";
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
    (*status_).text = "Cancelled posted operation #" + std::to_string(sequence) +
                        (cancelled ? " before dispatch" : " too late");
}

void StatesPage::on_invoke_from_worker() {
    // The Command's enabled state projects to its button while the worker
    // waits; the posted completion step re-enables it on the UI thread.
    invoke_.set_enabled(false);
    (*status_).text = "Worker blocked · no nested pump · waiting for UI turn";
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
