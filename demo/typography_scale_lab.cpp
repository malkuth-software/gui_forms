#include "typography_scale_lab.hpp"

#include "gui_forms/gui_forms.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms::typography_scale_lab {
namespace {

class LabRoot final : public Panel {
public:
    explicit LabRoot(StableId id) : Panel(std::move(id)) {}

    void set_title(std::shared_ptr<Label> value) { title_ = std::move(value); }
    void set_status(std::shared_ptr<Label> value) { status_ = std::move(value); }
    void add_scale_button(std::shared_ptr<Button> value) {
        scale_buttons_.push_back(std::move(value));
    }
    void add_specimen(std::shared_ptr<TypographySpecimen> value) {
        specimens_.push_back(std::move(value));
    }
    void set_inspector(std::shared_ptr<VisualInspectorView> value) {
        inspector_ = std::move(value);
    }
    void retain(SubscriptionToken token) {
        subscriptions_.push_back(std::move(token));
    }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        const double s = effective_text_scale();
        const double width = final_bounds.width;
        const double height = final_bounds.height;
        const bool columns = width >= 920.0;
        const double gutter = 18.0;
        const double inspector_width = columns ? 390.0 : 0.0;
        const double specimen_width = columns
            ? width - inspector_width - gutter * 3.0
            : width - gutter * 2.0;
        if (title_) set_child_layout(title_, {18.0, 8.0, width - 36.0, 32.0 * s});
        if (status_) set_child_layout(status_, {18.0, 40.0 * s,
                                               width - 36.0, 22.0 * s});
        double button_x = 18.0;
        const double button_y = 68.0 * s;
        for (const std::shared_ptr<Button>& button : scale_buttons_) {
            set_child_layout(button, {button_x, button_y, 92.0,
                                      std::max(28.0, 18.0 * s)});
            button_x += 98.0;
        }

        const double specimen_y = button_y + 39.0 * s;
        double y = specimen_y;
        if (s >= 1.5) {
            const double column_gap = 8.0;
            const double column_width =
                std::max(160.0, (specimen_width - column_gap) * 0.5);
            double column_y[2]{specimen_y, specimen_y};
            for (std::size_t index = 0U; index < specimens_.size(); ++index) {
                const std::size_t column = index % 2U;
                const double row = std::max(
                    36.0, (*specimens_[index]).measure({column_width, 10000.0}).height);
                set_child_layout(specimens_[index],
                    {gutter + column * (column_width + column_gap),
                     column_y[column], column_width, row});
                column_y[column] += row + 7.0;
            }
            y = std::max(column_y[0], column_y[1]);
        } else {
            for (const std::shared_ptr<TypographySpecimen>& value : specimens_) {
                const double row = std::max(
                    36.0, (*value).measure({specimen_width, 10000.0}).height);
                set_child_layout(value, {gutter, y, specimen_width, row});
                y += row + 7.0;
            }
        }
        if (inspector_) {
            if (columns) {
                set_child_layout(inspector_,
                    {width - inspector_width - gutter, button_y,
                     inspector_width, std::max(200.0, height - button_y - gutter)});
            } else {
                set_child_layout(inspector_, {gutter, y + 6.0,
                    specimen_width, std::max(220.0, height - y - 24.0)});
            }
        }
    }

private:
    std::shared_ptr<Label> title_;
    std::shared_ptr<Label> status_;
    std::vector<std::shared_ptr<Button>> scale_buttons_;
    std::vector<std::shared_ptr<TypographySpecimen>> specimens_;
    std::shared_ptr<VisualInspectorView> inspector_;
    std::vector<SubscriptionToken> subscriptions_;
};

struct SelectScale final {
    double scale{};
    std::weak_ptr<Label> status;

    void operator()(ButtonBase& source) const {
        Window* owner = source.attached_window();
        if (owner == nullptr) return;
        (*owner).set_text_scale(scale);
        if (const std::shared_ptr<Label> value = status.lock()) {
            (*value).text =
                "Text scale " + std::to_string(static_cast<int>(scale * 100.0)) +
                "% · display " + std::to_string((*owner).scale()) +
                "× · logical metrics reflow; only final baselines snap to device pixels";
        }
    }
};

std::shared_ptr<TypographySpecimen> specimen(
    std::string id, std::string caption, std::string text, FontSpec font) {
    std::shared_ptr<TypographySpecimen> result =
        make_control<TypographySpecimen>(StableId(std::move(id)),
                                         std::move(text));
    (*result).set_caption(std::move(caption));
    (*result).set_font(font);
    return result;
}

} // namespace

std::unique_ptr<Window> make_typography_scale_lab() {
    std::shared_ptr<LabRoot> root =
        make_control<LabRoot>(StableId("typography-scale.root"));
    (*root).set_background(Color::rgba(218, 226, 231));

    std::shared_ptr<Label> title = make_control<Label>(
        StableId("typography-scale.heading"),
        "Deterministic Typography + Scale");
    (*title).font = FontSpec{FontRole::control, 20.0, 700, false, 0.20};
    (*title).fore_color = Color::rgba(24, 54, 78);
    (*root).add_child(title);
    (*root).set_title(title);

    std::shared_ptr<Label> status = make_control<Label>(
        StableId("typography-scale.status"),
        "Text scale 100% · display host-bound · effective FontSpec, exact face runs, logical metrics, and snapped baseline remain distinct");
    (*status).font = FontSpec{FontRole::content, 10.0, 400, false};
    (*status).fore_color = Color::rgba(65, 83, 96);
    (*root).add_child(status);
    (*root).set_status(status);

    const std::vector<std::pair<std::string, double>> scales{
        {"100%", 1.0}, {"125%", 1.25}, {"150%", 1.5}, {"200%", 2.0}};
    for (std::size_t index = 0U; index < scales.size(); ++index) {
        std::shared_ptr<Button> choice = make_control<Button>(
            StableId("typography-scale.scale." + std::to_string(index)),
            scales[index].first);
        (*choice).font = {FontRole::control, 10.0, 700, false, 0.18};
        (*root).add_child(choice);
        (*root).add_scale_button(choice);
        (*root).retain((*choice).clicked().subscribe(
            *root, SelectScale{scales[index].second, status}));
    }

    std::vector<std::shared_ptr<TypographySpecimen>> specimens{
        specimen("typography-scale.title", "TITLE / PORTSMOUTH 17/700 + .25",
                 "Projects — File Manager", {FontRole::control, 17.0, 700, false, 0.25}),
        specimen("typography-scale.control", "CONTROL / PORTSMOUTH 10/700",
                 "Back   Forward   Up   Sort A→Z ▼", {FontRole::control, 10.0, 700, false, 0.18}),
        specimen("typography-scale.body", "BODY / CARLITO 12/400",
                 "Facade Study — modified today · 12 objects", {FontRole::content, 12.0, 400, false}),
        specimen("typography-scale.caption", "CAPTION / CARLITO 8/700 + .45",
                 "SELECTION · LOCAL AUTHORITY · GENERATION 42", {FontRole::content, 8.0, 700, false, 0.45}),
        specimen("typography-scale.italic", "ITALIC + TRACKING / CARLITO 11/400",
                 "Measured evidence, not estimated browser width", {FontRole::content, 11.0, 400, true, 0.30}),
        specimen("typography-scale.monospace", "MONO / COUSINE 10/400",
                 "/Work/Projects/Facade Study  0x002A", {FontRole::monospace, 10.0, 400, false}),
        specimen("typography-scale.fallback", "FALLBACK / CARLITO → CJK → EMOJI",
                 "Report 日本語 · launch 🚀 · Журнал", {FontRole::content, 12.0, 400, false}),
        specimen("typography-scale.long", "LONG LABEL / REFLOW GUARD",
                 "Complete menu command vocabulary remains reachable while long names and larger text remeasure the retained surface.",
                 {FontRole::content, 10.0, 400, false}),
    };
    (*specimens[4]).set_wrap_text(true);
    (*specimens.back()).set_wrap_text(true);
    for (const std::shared_ptr<TypographySpecimen>& value : specimens) {
        (*root).add_child(value);
        (*root).add_specimen(value);
    }

    std::shared_ptr<VisualInspectorView> inspector =
        make_control<VisualInspectorView>(
            StableId("typography-scale.inspector"), "typography-scale.fallback");
    (*root).add_child(inspector);
    (*root).set_inspector(inspector);

    std::unique_ptr<Window> window =
        std::make_unique<Window>(root, Size{1320.0, 840.0});
    (*window).perform_layout();
    return window;
}

} // namespace gui_forms::typography_scale_lab
