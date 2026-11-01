#include "pages.hpp"

namespace gui_forms::gallery::pages {

// Owns the timing page: the start/stop Commands its buttons bind, the
// interval Value its TrackBar binds, and the retained progress/motion targets
// the shell's UI Timer tick animates. The Timer, ToolTip, ErrorProvider and
// HelpProvider need a Window, so the Gallery shell creates them in
// initialize_gallery_runtime and reaches these controls through PageContext.
class TimingPage final : public gui_forms::Component {
public:
    TimingPage(const std::shared_ptr<Surface>& body,
               const std::shared_ptr<PageContext>& context);

private:
    void build_timer(const std::shared_ptr<Surface>& body, PageContext& context);
    void build_tooltips(const std::shared_ptr<Surface>& body, PageContext& context);
    void build_worker(const std::shared_ptr<Surface>& body);
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
};

TimingPage::TimingPage(const std::shared_ptr<Surface>& body,
                       const std::shared_ptr<PageContext>& context)
    : context_(context) {
    (*body).add_at(label("gallery.timing.heading", "TIMING AND TOOLTIPS", 22.0,
                         700, shell_blue_dark), {24.0, 18.0, 620.0, 34.0});
    (*body).add_at(label("gallery.timing.lead",
                         "UI-thread component deadlines, coalesced cadence, focus/hover help, owner revocation, and retained overlays.",
                         12.0, 400, muted), {26.0, 52.0, 920.0, 24.0});
    build_timer(body, *context);
    build_tooltips(body, *context);
    build_worker(body);
    on(start_.invoked(), *this, &TimingPage::on_start);
    on(stop_.invoked(), *this, &TimingPage::on_stop);
    on(interval_.changed(), *this, &TimingPage::on_interval_changed);
}

void TimingPage::build_timer(const std::shared_ptr<Surface>& body, PageContext& context) {
    std::shared_ptr<LayoutGroup> timer_group = group(
        "gallery.timing.timer.group", "Timer · deterministic UI-loop component",
        {930.0, 248.0});
    (*body).add_at(timer_group, {24.0, 92.0, 930.0, 248.0});
    status_ = label("gallery.timing.tick.status", "Idle · enter this page to start",
                    12.0, 700, green);
    (*timer_group).add_at(status_, {22.0, 38.0, 510.0, 28.0});
    context.timer_status = status_;

    start_.set_text("Start timer");
    start_.set_description("Arm the UI Timer deadline on the window queue");
    stop_.set_text("Stop timer");
    stop_.set_description("Stop the UI Timer; the scheduler becomes quiescent");
    std::shared_ptr<Button> start =
        make_control<Button>(StableId("gallery.timing.start"), "Start timer");
    (*start).set_visual_style(ButtonVisualStyle::accent);
    (*start).bind(start_);
    std::shared_ptr<Button> stop =
        make_control<Button>(StableId("gallery.timing.stop"), "Stop timer");
    (*stop).bind(stop_);
    (*timer_group).add_at(start, {22.0, 78.0, 132.0, 34.0});
    (*timer_group).add_at(stop, {166.0, 78.0, 132.0, 34.0});
    context.timer_start = start;
    context.timer_stop = stop;

    (*timer_group).add_at(label("gallery.timing.interval.label", "Interval", 10.0,
                                700, shell_blue_dark), {326.0, 76.0, 80.0, 20.0});
    std::shared_ptr<TrackBar> interval =
        make_control<TrackBar>(StableId("gallery.timing.interval"));
    (*interval).set_range(80.0, 600.0);
    (*interval).set_tick_frequency(4U);
    (*interval).set_accessible_name("UI timer interval in milliseconds");
    (*interval).bind(interval_);
    (*timer_group).add_at(interval, {326.0, 98.0, 260.0, 44.0});
    context.timer_interval = interval;

    std::shared_ptr<ProgressBar> progress =
        make_control<ProgressBar>(StableId("gallery.timing.progress"));
    (*progress).set_range(0.0, 100.0);
    (*progress).set_value(0.0);
    (*progress).set_accessible_name("Timer cycle");
    (*timer_group).add_at(progress, {22.0, 164.0, 564.0, 24.0});
    context.timer_progress = progress;

    std::shared_ptr<ScaledPanel> motion = make_control<ScaledPanel>(
        StableId("gallery.timing.motion"), Size{294.0, 170.0});
    (*motion).set_background(Color::rgba(247, 249, 251));
    (*motion).set_border_style(BorderStyle::line);
    (*motion).add_at(label("gallery.timing.motion.caption",
                           "WINDOW-QUEUE MOTION · 4 Hz", 10.0, 700, muted),
                     {14.0, 8.0, 266.0, 24.0});
    std::shared_ptr<Panel> motion_rule =
        make_control<Panel>(StableId("gallery.timing.motion.rule"));
    (*motion_rule).set_background(rule);
    (*motion).add_at(motion_rule, {14.0, 84.0, 266.0, 1.0});
    std::shared_ptr<Button> motion_target = make_control<Button>(
        StableId("gallery.timing.motion.target"), "Moving tooltip target");
    (*motion_target).set_visual_style(ButtonVisualStyle::accent);
    (*motion_target).set_style(page_style());
    (*motion).add_at(motion_target, {14.0, 39.0, 168.0, 34.0});
    (*timer_group).add_at(motion, {612.0, 38.0, 294.0, 170.0});
    context.timer_motion = motion;
    context.timer_motion_target = motion_target;

    std::shared_ptr<Label> cadence = label(
        "gallery.timing.cadence.note",
        "Late deadlines coalesce to one ordered tick; no catch-up burst and no hidden paint control.",
        10.0, 400, muted);
    (*cadence).set_text_wrapping(TextWrapping::word);
    (*cadence).set_vertical_alignment(VerticalAlignment::near);
    (*timer_group).add_at(cadence, {22.0, 200.0, 564.0, 36.0});
}

void TimingPage::build_tooltips(const std::shared_ptr<Surface>& body,
                                PageContext& context) {
    std::shared_ptr<LayoutGroup> tips_group = group(
        "gallery.timing.tooltip.group", "ToolTip · text on each control, one presenter per window",
        {930.0, 264.0});
    (*body).add_at(tips_group, {24.0, 362.0, 930.0, 264.0});
    std::shared_ptr<Button> hover =
        make_control<Button>(StableId("gallery.timing.tooltip.hover"), "Hover for detail");
    std::shared_ptr<Button> keyboard =
        make_control<Button>(StableId("gallery.timing.tooltip.focus"), "Tab here for help");
    std::shared_ptr<Button> persistent = make_control<Button>(
        StableId("gallery.timing.tooltip.persistent"), "Show persistent help");
    (*persistent).set_visual_style(ButtonVisualStyle::accent);
    (*keyboard).set_visual_style(ButtonVisualStyle::command);
    (*tips_group).add_at(hover, {22.0, 44.0, 176.0, 36.0});
    (*tips_group).add_at(keyboard, {214.0, 44.0, 176.0, 36.0});
    (*tips_group).add_at(persistent, {406.0, 44.0, 190.0, 36.0});
    std::shared_ptr<Button> disabled_target = make_control<Button>(
        StableId("gallery.timing.tooltip.disabled"), "Disabled target");
    (*disabled_target).set_enabled(false);
    std::shared_ptr<Button> show_disabled = make_control<Button>(
        StableId("gallery.timing.tooltip.show-disabled"), "Explain disabled control");
    (*tips_group).add_at(disabled_target, {22.0, 102.0, 176.0, 36.0});
    (*tips_group).add_at(show_disabled, {214.0, 102.0, 206.0, 36.0});
    context.tooltip_disabled_target = disabled_target;
    context.tooltip_show_disabled = show_disabled;

    std::shared_ptr<Label> policy = label(
        "gallery.timing.tooltip.policy",
        "Hover: 420 ms · Reshow: 90 ms · Auto-pop: 4 s\nEach control carries its own tooltip text; the window's one presenter shows it on rest and on keyboard focus, follows moving targets and closes with its owner.",
        10.0, 400, muted);
    (*policy).set_text_wrapping(TextWrapping::word);
    (*policy).set_vertical_alignment(VerticalAlignment::near);
    (*tips_group).add_at(policy, {22.0, 166.0, 574.0, 70.0});
    std::shared_ptr<Label> proof = label(
        "gallery.timing.tooltip.proof",
        "TRY IT\nHover each target, tab through the controls, adjust the interval, and watch the anchored moving tooltip.",
        11.0, 700, shell_blue_dark);
    (*proof).set_text_wrapping(TextWrapping::word);
    (*proof).set_vertical_alignment(VerticalAlignment::near);
    (*tips_group).add_at(proof, {628.0, 44.0, 270.0, 126.0});

    std::shared_ptr<TextBox> endpoint = make_control<TextBox>(
        StableId("gallery.timing.provider.field"), "192.168.1.96:5555");
    (*endpoint).set_accessible_name("Remote endpoint");
    std::shared_ptr<Button> toggle =
        make_control<Button>(StableId("gallery.timing.provider.toggle"), "Clear");
    (*toggle).set_visual_style(ButtonVisualStyle::command);
    (*tips_group).add_at(endpoint, {628.0, 174.0, 204.0, 32.0});
    (*tips_group).add_at(toggle, {840.0, 174.0, 66.0, 32.0});
    context.provider_error_target = endpoint;
    context.provider_error_toggle = toggle;
    (*tips_group).add_at(label("gallery.timing.provider.note",
                               "ErrorProvider adornment · focus the field and press F1",
                               9.0, 400, muted),
                         {628.0, 210.0, 278.0, 22.0});
}

// A BackgroundWorker counting to 100 on a worker thread, its progress and
// completion arriving on the UI thread.
void TimingPage::build_worker(const std::shared_ptr<Surface>& body) {
    std::shared_ptr<LayoutGroup> worker_group = group(
        "gallery.timing.worker.group", "BackgroundWorker · work off the UI thread, results on it",
        {930.0, 118.0});
    (*body).add_at(worker_group, {24.0, 648.0, 930.0, 118.0});
    run_work_.set_text("Run in background");
    cancel_work_.set_text("Cancel");
    cancel_work_.set_enabled(false);
    run_button_ = make_control<Button>(StableId("gallery.timing.worker.run"), "Run in background");
    (*run_button_).set_visual_style(ButtonVisualStyle::accent);
    (*run_button_).bind(run_work_);
    std::shared_ptr<Button> cancel = make_control<Button>(StableId("gallery.timing.worker.cancel"), "Cancel");
    (*cancel).bind(cancel_work_);
    (*worker_group).add_at(run_button_, {22.0, 38.0, 160.0, 34.0});
    (*worker_group).add_at(cancel, {194.0, 38.0, 104.0, 34.0});
    work_progress_ = make_control<ProgressBar>(StableId("gallery.timing.worker.progress"));
    (*work_progress_).set_range(0.0, 100.0);
    (*work_progress_).set_accessible_name("Background work");
    (*worker_group).add_at(work_progress_, {322.0, 44.0, 264.0, 24.0});
    work_status_ = label("gallery.timing.worker.status", "Idle", 11.0, 700, shell_blue_dark);
    (*worker_group).add_at(work_status_, {612.0, 40.0, 294.0, 28.0});
    (*worker_group).add_at(label("gallery.timing.worker.note",
                                 "progress_changed and run_worker_completed arrive through the window's dispatcher.",
                                 10.0, 400, muted),
                           {22.0, 80.0, 884.0, 22.0});
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
    (*work_status_).set_text("Running on a worker thread");
}

void TimingPage::on_cancel_work() {
    if (worker_ && (*worker_).is_busy()) (*worker_).cancel_async();
}

void TimingPage::on_work_progress(const BackgroundWorkerProgress& progress) {
    (*work_progress_).set_value(static_cast<double>(progress.percent));
}

void TimingPage::on_work_completed(const BackgroundWorkerCompleted& completed) {
    run_work_.set_enabled(true);
    cancel_work_.set_enabled(false);
    if (completed.cancelled) {
        (*work_status_).set_text("Cancelled");
    } else if (completed.error) {
        (*work_status_).set_text("Failed: " + completed.error_message);
    } else {
        (*work_status_).set_text(binding_value_to_string(completed.result));
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
    (*status_).set_text("Running · deadline armed on the UI queue");
}

void TimingPage::on_stop() {
    const std::shared_ptr<Timer> ui_timer = timer();
    if (!ui_timer) return;
    (*ui_timer).stop();
    (*status_).set_text("Stopped · zero idle wakeups");
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
