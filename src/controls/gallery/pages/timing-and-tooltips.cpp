#include "pages.hpp"

#include "timing-and-tooltips_form.hpp"

#include <array>
#include <vector>

namespace gui_forms::gallery::pages {

// Owns the timing page: the controls demo/pages/timing-and-tooltips.form
// describes, the start/stop Commands its buttons bind, the
// interval Value its TrackBar binds, and the retained progress/motion targets
// the shell's UI Timer tick animates. The Timer, ToolTip, ErrorProvider and
// HelpProvider need a Window, so the Gallery shell creates them in
// initialize_gallery_runtime and reaches these controls through PageContext.
class TimingPage final : public gui_forms::Component {
public:
    TimingPage(const std::shared_ptr<Surface>& body,
               const std::shared_ptr<PageContext>& context);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void build_timer(PageContext& context);
    void build_tooltips(PageContext& context);
    void build_worker();
    void on_run_work();
    void on_cancel_work();
    void on_work_progress(const BackgroundWorkerProgress& progress);
    void on_work_completed(const BackgroundWorkerCompleted& completed);
    // The BackgroundWorker's work, on its worker thread: it touches only its
    // task.
    void count_in_background(BackgroundWorkerTask& task);
    void on_start();
    void on_stop();
    void on_interval_changed(double milliseconds);
    [[nodiscard]] std::shared_ptr<Timer> timer() const;

    std::weak_ptr<PageContext> context_;
    Command start_{};
    Command stop_{};
    Value<double> interval_{250.0};
    std::shared_ptr<Label> status_;
    Command run_work_{};
    Command cancel_work_{};
    std::shared_ptr<Button> run_button_;
    std::shared_ptr<ProgressBar> work_progress_;
    std::shared_ptr<Label> work_status_;
    // Created on the first run, when the page's window is known.
    std::shared_ptr<BackgroundWorker> worker_;
    // Declared after the Commands and the Value its controls bind, so it is
    // released first.
    TimingDocument<TimingPage> document_{};
};

TimingPage::TimingPage(const std::shared_ptr<Surface>& body,
                       const std::shared_ptr<PageContext>& context)
    : context_(context) {
    document_.initialize_component(*this);
    mount_document(body);
    build_timer(*context);
    build_tooltips(*context);
    build_worker();
    on(start_.invoked(), *this, &TimingPage::on_start);
    on(stop_.invoked(), *this, &TimingPage::on_stop);
    on(interval_.changed(), *this, &TimingPage::on_interval_changed);
}

// The document's root is a declared boundary only: its children move into
// the shell's page body, where each is placed by its bounds in design
// coordinates. The copy of the child list keeps the iteration valid while
// the root shrinks.
void TimingPage::mount_document(const std::shared_ptr<Surface>& body) {
    Control& root = *document_.gallery_timing_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
    const std::array<std::shared_ptr<ScaledGroupBox>, 3> groups{
        document_.gallery_timing_timer_group, document_.gallery_timing_tooltip_group,
        document_.gallery_timing_worker_group};
    for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
        (*group).set_style(page_style());
    }
    status_ = document_.gallery_timing_tick_status;
    run_button_ = document_.gallery_timing_worker_run;
    work_progress_ = document_.gallery_timing_worker_progress;
    work_status_ = document_.gallery_timing_worker_status;
}

// The shell's UI Timer animates these controls; PageContext hands them over.
void TimingPage::build_timer(PageContext& context) {
    context.timer_status = status_;

    start_.set_text("Start timer");
    start_.set_description("Arm the UI Timer deadline on the window queue");
    stop_.set_text("Stop timer");
    stop_.set_description("Stop the UI Timer; the scheduler becomes quiescent");
    const std::shared_ptr<Button>& start = document_.gallery_timing_start;
    const std::shared_ptr<Button>& stop = document_.gallery_timing_stop;
    (*start).bind(start_);
    (*stop).bind(stop_);
    context.timer_start = start;
    context.timer_stop = stop;

    const std::shared_ptr<TrackBar>& interval = document_.gallery_timing_interval;
    (*interval).set_range(80.0, 600.0);
    (*interval).bind(interval_);
    context.timer_interval = interval;

    const std::shared_ptr<ProgressBar>& progress = document_.gallery_timing_progress;
    (*progress).set_range(0.0, 100.0);
    (*progress).value = 0.0;
    context.timer_progress = progress;

    const std::shared_ptr<ScaledPanel>& motion = document_.gallery_timing_motion;
    (*motion).set_background(Color::rgba(247, 249, 251));
    (*document_.gallery_timing_motion_rule).set_background(rule);
    const std::shared_ptr<Button>& motion_target = document_.gallery_timing_motion_target;
    (*motion_target).set_style(page_style());
    context.timer_motion = motion;
    context.timer_motion_target = motion_target;
}

void TimingPage::build_tooltips(PageContext& context) {
    context.tooltip_disabled_target = document_.gallery_timing_tooltip_disabled;
    context.tooltip_show_disabled = document_.gallery_timing_tooltip_show_disabled;
    context.provider_error_target = document_.gallery_timing_provider_field;
    context.provider_error_toggle = document_.gallery_timing_provider_toggle;
}

// A BackgroundWorker counting to 100 on a worker thread, its progress and
// completion arriving on the UI thread.
void TimingPage::build_worker() {
    run_work_.set_text("Run in background");
    cancel_work_.set_text("Cancel");
    cancel_work_.set_enabled(false);
    (*run_button_).bind(run_work_);
    (*document_.gallery_timing_worker_cancel).bind(cancel_work_);
    (*work_progress_).set_range(0.0, 100.0);
    on(run_work_.invoked(), *this, &TimingPage::on_run_work);
    on(cancel_work_.invoked(), *this, &TimingPage::on_cancel_work);
}

void TimingPage::count_in_background(BackgroundWorkerTask& task) {
    for (int percent = 0; percent <= 100; percent += 5) {
        if (task.cancellation_pending()) {
            task.set_cancelled(true);
            return;
        }
        task.report_progress(percent);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    task.set_result(BindingValue{std::string("Counted to 100")});
}

void TimingPage::on_run_work() {
    Window* const window = (*run_button_).attached_window();
    if (window == nullptr) return;
    if (!worker_) {
        worker_ = std::make_shared<BackgroundWorker>(*window);
        (*worker_).set_worker_reports_progress(true);
        (*worker_).set_worker_supports_cancellation(true);
        (*worker_).set_do_work(
            BackgroundWorker::Work::bind<TimingPage, &TimingPage::count_in_background>(*this));
        on((*worker_).progress_changed(), *this, &TimingPage::on_work_progress);
        on((*worker_).run_worker_completed(), *this, &TimingPage::on_work_completed);
    }
    if ((*worker_).is_busy()) return;
    (*worker_).run_worker_async();
    run_work_.set_enabled(false);
    cancel_work_.set_enabled(true);
    (*work_status_).text = "Running on a worker thread";
}

void TimingPage::on_cancel_work() {
    if (worker_ && (*worker_).is_busy()) (*worker_).cancel_async();
}

void TimingPage::on_work_progress(const BackgroundWorkerProgress& progress) {
    (*work_progress_).value = static_cast<double>(progress.percent);
}

void TimingPage::on_work_completed(const BackgroundWorkerCompleted& completed) {
    run_work_.set_enabled(true);
    cancel_work_.set_enabled(false);
    if (completed.cancelled) {
        (*work_status_).text = "Cancelled";
    } else if (completed.error) {
        (*work_status_).text = "Failed: " + completed.error_message;
    } else {
        (*work_status_).text = binding_value_to_string(completed.result);
    }
}

std::shared_ptr<Timer> TimingPage::timer() const {
    const std::shared_ptr<PageContext> context = context_.lock();
    if (!context) return nullptr;
    return (*context).ui_timer;
}

void TimingPage::on_start() {
    const std::shared_ptr<Timer> ui_timer = timer();
    if (!ui_timer) return;
    (*ui_timer).start();
    (*status_).text = "Running · deadline armed on the UI queue";
}

void TimingPage::on_stop() {
    const std::shared_ptr<Timer> ui_timer = timer();
    if (!ui_timer) return;
    (*ui_timer).stop();
    (*status_).text = "Stopped · zero idle wakeups";
}

void TimingPage::on_interval_changed(const double milliseconds) {
    const std::shared_ptr<Timer> ui_timer = timer();
    if (!ui_timer) return;
    (*ui_timer).set_interval(
        std::chrono::milliseconds(static_cast<int>(std::round(milliseconds))));
}

void add_timing_and_tooltips(const std::shared_ptr<Surface>& page,
                             const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<TimingPage>(page, context));
}

} // namespace gui_forms::gallery::pages
