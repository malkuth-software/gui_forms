#include "../demo/gallery_dml.hpp"
#include "../demo/gallery.hpp"
#include "../demo/gallery_model.hpp"
#include "../src/controls/gallery_controls.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "gallery interaction failure: " << message << '\n';
        std::exit(1);
    }
}

class CountingPainter final : public gui_forms::Painter {
public:
    void save() override { ++saves; }
    void restore() override { ++restores; }
    void translate(gui_forms::Point) override {}
    void clip_rect(gui_forms::Rect rect) override {
        ++clips;
        clip_rectangles.push_back(rect);
    }
    void fill_rect(gui_forms::Rect, gui_forms::Color) override { ++fills; }
    void stroke_rect(gui_forms::Rect, gui_forms::Color, double) override { ++strokes; }
    void draw_line(gui_forms::Point, gui_forms::Point, gui_forms::Color, double) override {
        ++lines;
    }
    void draw_text_utf8(gui_forms::Point,
                        std::string_view text,
                        gui_forms::FontSpec font,
                        gui_forms::Color) override {
        ++texts;
        text_roles.emplace_back(text, font.role);
    }
    void draw_image(gui_forms::ImageId, gui_forms::Rect, double) override { ++images; }

    std::uint64_t saves{};
    std::uint64_t restores{};
    std::uint64_t clips{};
    std::uint64_t fills{};
    std::uint64_t strokes{};
    std::uint64_t lines{};
    std::uint64_t texts{};
    std::uint64_t images{};
    std::vector<gui_forms::Rect> clip_rectangles;
    std::vector<std::pair<std::string, gui_forms::FontRole>> text_roles;

    [[nodiscard]] bool observed_role(std::string_view text,
                                     gui_forms::FontRole role) const {
        for (const std::pair<std::string, gui_forms::FontRole>& observed : text_roles) {
            if (observed.first == text && observed.second == role) {
                return true;
            }
        }
        return false;
    }
};

class OverflowPaintProbe final : public gui_forms::Control {
public:
    explicit OverflowPaintProbe(gui_forms::StableId id)
        : Control(std::move(id)) {}

    void on_paint(gui_forms::Painter& painter, gui_forms::Rect) override {
        const gui_forms::Rect bounds = committed_arranged_bounds();
        painter.fill_rect({0.0, 0.0, bounds.width, bounds.height},
                          gui_forms::Color::rgba(255, 0, 0));
    }
};

[[nodiscard]] bool contains(gui_forms::Rect parent, gui_forms::Rect child)
{
    constexpr double tolerance = 0.001;
    return child.x + tolerance >= parent.x && child.y + tolerance >= parent.y &&
           child.x + child.width <= parent.x + parent.width + tolerance &&
           child.y + child.height <= parent.y + parent.height + tolerance;
}

[[nodiscard]] gui_forms::Point center_of(const gui_forms::Control::Ptr& control)
{
    const gui_forms::Rect bounds = (*control).absolute_bounds();
    return {bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
}

void click(gui_forms::Window& window, std::string_view stable_id)
{
    const gui_forms::Control::Ptr control = window.find(stable_id);
    expect(control != nullptr, "click target must resolve by stable ID");
    const gui_forms::Point point = center_of(control);
    const gui_forms::Control::Ptr hit = window.hit_test(point);
    if (hit == nullptr || (*hit).stable_id().value() != stable_id) {
        std::cerr << "expected hit " << stable_id << ", got "
                  << (hit == nullptr ? std::string_view("<none>") : (*hit).stable_id().value())
                  << '\n';
    }
    expect(hit != nullptr && (*hit).stable_id().value() == stable_id,
           "click center must hit the retained target");
    expect(window.dispatch_pointer({gui_forms::PointerAction::down,
                                    gui_forms::PointerButton::primary,
                                    point}),
           "pointer down must be handled");
    expect(window.dispatch_pointer({gui_forms::PointerAction::up,
                                    gui_forms::PointerButton::primary,
                                    point}),
           "pointer up must be handled");
}

}  // namespace

int main()
{
    using gui_forms::gallery::GalleryModel;
    using gui_forms::gallery::StyleMode;

    expect(gui_forms::gallery::dml::ids_are_unique(), "DML stable IDs must be unique");
    expect(gui_forms::gallery::dml::parents_resolve(), "DML parents must resolve");

    GalleryModel gallery;
    expect(gallery.state().precise_updates, "checkbox begins checked");
    expect(gallery.activate("gallery.checkbox"), "checkbox accepts activation");
    expect(!gallery.state().precise_updates, "checkbox toggles deterministically");

    expect(gallery.activate("gallery.radio.quiet"), "quiet radio accepts activation");
    expect(gallery.state().style_mode == StyleMode::quiet_relief, "quiet radio is exclusive");
    expect(gallery.activate("gallery.radio.classic"), "classic radio accepts activation");
    expect(gallery.state().style_mode == StyleMode::classic_relief, "classic radio is exclusive");

    expect(gallery.set_slider_value(67.0), "slider mutation changes state");
    expect(gallery.state().progress_value == 67.0, "slider updates bound progress");
    expect(gallery.state().instrument_value == 0.67, "slider updates custom instrument");
    expect(gui_forms::gallery::format_percent(gallery.state().progress_value) == "67%",
           "bound value label is deterministic");
    expect(gallery.set_slider_value(200.0), "slider accepts an out-of-range value");
    expect(gallery.state().slider_value == 100.0, "slider clamps to declared maximum");

    expect(gallery.replace_text("Retained text"), "text edit changes application state");
    expect(gallery.state().text == "Retained text", "text edit remains retained");

    expect(gallery.select_collection_row("gallery.collection.gamma"),
           "collection selection changes");
    expect(gallery.state().selected_collection_row == "gallery.collection.gamma",
           "collection selection is retained by stable ID");

    expect(!gallery.activate("gallery.disabled-button"), "disabled button cannot activate");
    expect(!gallery.activate("gallery.unknown"), "unknown stable ID cannot activate");

    expect(gallery.activate("gallery.command.diagnostics"), "diagnostics toggle activates");
    expect(gallery.state().diagnostics_visible, "diagnostics become visible");

    const std::uint64_t revision_before_reset = gallery.state().revision;
    expect(gallery.activate("gallery.command.reset"), "reset command activates");
    expect(gallery.state().revision > revision_before_reset, "reset is an observable mutation");
    expect(gallery.state().slider_value == 42.0, "reset restores linked values");

    std::unique_ptr<gui_forms::Window> window = gui_forms::gallery::make_gallery();
    expect((*window).metrics_snapshot().control_count > gui_forms::gallery::dml::gallery_nodes.size(),
           "compiled DML population reaches the retained window beside the registry-driven shell");
    expect(gui_forms::gallery::selected_gallery_page(*window) == "control-spectrum" &&
               (*window).find("gallery.page.basics") != nullptr &&
               (*window).find("gallery.surface.basics") != nullptr,
           "navigation rows and page surfaces resolve by stable page id");
    expect((*window).find("gallery.instrument") != nullptr,
           "custom instrument resolves by stable ID");
    expect((*window).find("gallery.diagnostics") != nullptr,
           "hidden diagnostics remain addressable by stable ID");
    const std::shared_ptr<gui_forms::UserControl> lifecycle = std::dynamic_pointer_cast<gui_forms::UserControl>(
        (*window).find("gallery.lifecycle-card"));
    const std::shared_ptr<gui_forms::Label> lifecycle_status = std::dynamic_pointer_cast<gui_forms::Label>(
        (*window).find("gallery.lifecycle-status"));
    expect(lifecycle != nullptr && (*lifecycle).is_loaded() &&
               (*lifecycle).attachment_count() == 1 && lifecycle_status != nullptr &&
               (*lifecycle_status).text() == "Load 1 · attach 1 · init 1",
           "Gallery visibly consumes composed UserControl load and initialization lifecycle");
    expect(gui_forms::gallery::select_gallery_page(*window, "instrument"),
           "instrument page selects by stable id");
    // The instrument sweeps on its own paint-gated UI timer (ADR-016),
    // which starts when the shown instrument is first painted.
    CountingPainter instrument_painter;
    (*window).paint(instrument_painter);
    expect((*window).next_wake().has_value(),
           "custom instrument schedules its sweep timer while shown");
    const gui_forms::ImageRegistrySnapshot images = (*window).image_resource_snapshot();
    // One validated PNG (the status badge, 146 encoded bytes), the six
    // in-memory BGRA fixtures the image pages consume (128x80 test card, three
    // 16x16 folder icons, 18x18 nine-patch, 8x8 tile: 45,584 source bytes) and
    // the images page's 12x12 RasterCanvas bitmap (576 bytes).
    expect(images.resource_count == 8 && images.encoded_bytes == 46306 &&
               images.decoded_bytes > 16U * 16U * 4U,
           "Gallery status badge is owned and bounded by the core PNG registry");
    expect(!(*(*window).find("gallery.diagnostics")).visible(),
           "diagnostics begin hidden");
    for (const gui_forms::gallery::dml::NodeSpec& node :
         gui_forms::gallery::dml::gallery_nodes) {
        const gui_forms::Control::Ptr control = (*window).find(node.id);
        if (control == nullptr || !(*control).effectively_visible()) {
            continue;
        }
        const gui_forms::Control::Ptr parent = (*control).parent();
        if (parent != nullptr) {
            if (!contains((*parent).absolute_bounds(), (*control).absolute_bounds())) {
                const gui_forms::Rect parent_bounds = (*parent).absolute_bounds();
                const gui_forms::Rect child_bounds = (*control).absolute_bounds();
                std::cerr << "containment parent=" << (*parent).stable_id().value()
                          << " child=" << (*control).stable_id().value()
                          << " parent_bounds=" << parent_bounds.x << ',' << parent_bounds.y
                          << ',' << parent_bounds.width << ',' << parent_bounds.height
                          << " child_bounds=" << child_bounds.x << ',' << child_bounds.y
                          << ',' << child_bounds.width << ',' << child_bounds.height << '\n';
            }
            expect(contains((*parent).absolute_bounds(), (*control).absolute_bounds()),
                   "every visible Gallery child must remain inside its parent edge");
        }
    }

    expect(gui_forms::gallery::select_gallery_page(*window, "basics"),
           "basics page selects by stable id");
    CountingPainter painter;
    (*window).paint(painter, {0.0, 0.0, 1280.0, 820.0});
    expect(painter.saves == painter.restores, "painter state is balanced");
    expect(painter.fills > 10 && painter.lines > 10 && painter.texts > 10,
           "gallery paints classic surfaces, relief, and labels");
    expect(painter.images == 1,
           "Gallery replays exactly one validated PNG status resource");
    expect(painter.observed_role("Apply", gui_forms::FontRole::control) &&
               painter.observed_role("COMPOSED", gui_forms::FontRole::control) &&
               painter.observed_role("Load 1 · attach 1 · init 1",
                                     gui_forms::FontRole::content) &&
               painter.observed_role("Information display", gui_forms::FontRole::control),
           "titles and control chrome must use the control typography role");
    expect(painter.observed_role("Edit this text", gui_forms::FontRole::content),
           "editable field text must use the content typography role");
    expect(gui_forms::gallery::select_gallery_page(*window, "retained-rows"),
           "retained rows page selects by stable id");
    CountingPainter rows_painter;
    (*window).paint(rows_painter, {0.0, 0.0, 1280.0, 820.0});
    expect(rows_painter.observed_role("Alpha channel", gui_forms::FontRole::content),
           "collection field text must use the content typography role");
    expect(gui_forms::gallery::select_gallery_page(*window, "basics"),
           "basics page selects again by stable id");
    // Selecting a page damages the whole page surface; present that frame so
    // the checkbox toggle below is the only pending damage.
    CountingPainter page_switch_painter;
    (*window).paint(page_switch_painter);
    (*window).reset_activity_metrics();

    const std::shared_ptr<gui_forms::CheckBox> checkbox = std::dynamic_pointer_cast<gui_forms::CheckBox>(
        (*window).find("gallery.checkbox"));
    expect(checkbox != nullptr && (*checkbox).checked(), "retained checkbox begins checked");
    const std::uint64_t activations_before_checkbox = (*window).metrics_snapshot().activations;
    click(*window, "gallery.checkbox");
    expect(!(*checkbox).checked(), "window dispatch toggles checkbox state");
    expect((*window).metrics_snapshot().activations == activations_before_checkbox + 1U,
           "matching press and release produces exactly one activation");
    CountingPainter partial_painter;
    (*window).paint(partial_painter);
    const gui_forms::MetricsSnapshot partial_metrics = (*window).metrics_snapshot();
    expect(partial_metrics.partial_paints > 0U,
           "localized checkbox mutation produces a partial paint");
    expect(partial_metrics.display_chunks_rebuilt > 0U &&
               partial_metrics.display_chunks_rebuilt < partial_metrics.control_count,
           "localized checkbox mutation rebuilds fewer chunks than the tree size");
    expect(partial_metrics.display_chunks_rebuilt ==
               partial_metrics.paint_invalidations_consumed &&
               partial_metrics.display_commands_replayed > 0U,
           "localized checkbox mutation replays only its rebuilt chunk commands");
    expect(partial_metrics.paint_invalidations_consumed > 0U &&
               partial_metrics.paint_invalidations_consumed < partial_metrics.control_count,
           "localized checkbox mutation consumes fewer paint invalidations than the tree size");

    const gui_forms::Control::Ptr disabled =
        (*window).find("gallery.disabled-button");
    const std::uint64_t activations_before_disabled = (*window).metrics_snapshot().activations;
    const gui_forms::Point disabled_point = center_of(disabled);
    (void)(*window).dispatch_pointer({gui_forms::PointerAction::down,
                                    gui_forms::PointerButton::primary,
                                    disabled_point});
    (void)(*window).dispatch_pointer({gui_forms::PointerAction::up,
                                    gui_forms::PointerButton::primary,
                                    disabled_point});
    expect((*window).focused_control() != disabled, "disabled control cannot receive focus");
    // A disabled control is transparent to hit testing, so the press lands on
    // the page body beneath it; the body records the one pass-through
    // activation and the disabled button records none.
    const gui_forms::Control::Ptr beneath = (*window).hit_test(disabled_point);
    expect(beneath != nullptr && beneath != disabled &&
               (*beneath).stable_id().value() == "gallery.body.basics",
           "disabled control passes the press through to the page body");
    expect((*window).metrics_snapshot().activations == activations_before_disabled + 1U,
           "disabled control cannot activate");

    click(*window, "gallery.text-input");
    expect((*window).dispatch_text({" retained", false, -1, 0, false}),
           "focused text input accepts committed UTF-8 text");
    const std::shared_ptr<gui_forms::gallery::GalleryControl> text_input = std::dynamic_pointer_cast<gui_forms::gallery::GalleryControl>(
        (*window).find("gallery.text-input"));
    expect((*text_input).display_text() == "Edit this text retained",
           "text edit survives event dispatch in application state");

    expect(gui_forms::gallery::select_gallery_page(*window, "values"),
           "values page selects by stable id");
    const std::shared_ptr<gui_forms::TrackBar> slider = std::dynamic_pointer_cast<gui_forms::TrackBar>(
        (*window).find("gallery.slider"));
    const std::shared_ptr<gui_forms::ProgressBar> progress = std::dynamic_pointer_cast<gui_forms::ProgressBar>(
        (*window).find("gallery.progress"));
    const std::shared_ptr<gui_forms::gallery::GalleryControl> instrument = std::dynamic_pointer_cast<gui_forms::gallery::GalleryControl>(
        (*window).find("gallery.instrument"));
    const gui_forms::Rect slider_bounds = (*slider).absolute_bounds();
    const gui_forms::Point near_end {slider_bounds.x + slider_bounds.width - 11.0,
                                     slider_bounds.y + slider_bounds.height * 0.5};
    expect((*window).dispatch_pointer({gui_forms::PointerAction::down,
                                     gui_forms::PointerButton::primary,
                                     near_end}),
           "slider handles pointer down");
    expect((*window).dispatch_pointer({gui_forms::PointerAction::up,
                                     gui_forms::PointerButton::primary,
                                     near_end}),
           "slider handles pointer up");
    expect((*slider).value() > 98.0, "slider maps pointer position to bounded value");
    expect((*progress).value() == (*slider).value(), "progress remains bound to slider");
    expect((*instrument).value() == (*slider).value() / 100.0,
           "custom instrument remains bound to slider");

    expect(gui_forms::gallery::select_gallery_page(*window, "retained-rows"),
           "retained rows page selects before row interaction");
    click(*window, "gallery.collection.gamma");
    const std::shared_ptr<gui_forms::gallery::GalleryControl> gamma = std::dynamic_pointer_cast<gui_forms::gallery::GalleryControl>(
        (*window).find("gallery.collection.gamma"));
    const std::shared_ptr<gui_forms::gallery::GalleryControl> alpha = std::dynamic_pointer_cast<gui_forms::gallery::GalleryControl>(
        (*window).find("gallery.collection.alpha"));
    expect((*gamma).selected() && !(*alpha).selected(),
           "retained list row selection is exclusive");

    gui_forms::DragEvent drag;
    drag.action = gui_forms::DragAction::enter;
    drag.session_id = 41;
    drag.position = center_of(gamma);
    drag.allowed_effects = gui_forms::DragEffect::copy;
    drag.items = {gui_forms::DragFileListData{{"/captures/live.iq"}}};
    expect((*window).dispatch_drag(drag).accepted_effect == gui_forms::DragEffect::copy,
           "Gallery collection advertises a retained copy drop target");
    drag.action = gui_forms::DragAction::drop;
    expect((*window).dispatch_drag(std::move(drag)).handled,
           "Gallery collection handles a typed file drop");
    const std::shared_ptr<gui_forms::Label> status = std::dynamic_pointer_cast<gui_forms::Label>(
        (*window).find("gallery.command.status"));
    expect((*status).text() == "Drop received · 1 file · 0 text · 0 data",
           "Gallery exposes the accepted typed drop without platform payloads");

    click(*window, "gallery.command.diagnostics");
    expect((*(*window).find("gallery.diagnostics")).visible(),
           "diagnostics command reveals structured metrics panel");
    const std::shared_ptr<gui_forms::Label> diagnostic = std::dynamic_pointer_cast<gui_forms::Label>(
        (*window).find("gallery.diagnostics.input"));
    expect((*diagnostic).text().find("input ") == 0,
           "on-screen diagnostics consume the core MetricsSnapshot");

    std::shared_ptr<OverflowPaintProbe> clip_root =
        gui_forms::make_control<OverflowPaintProbe>(
        gui_forms::StableId("clip.root"));
    std::shared_ptr<OverflowPaintProbe> clip_child =
        gui_forms::make_control<OverflowPaintProbe>(
        gui_forms::StableId("clip.child"));
    (*clip_child).set_requested_bounds({40.0, 40.0, 30.0, 30.0});
    (*clip_root).add_child(clip_child);
    gui_forms::Window clip_window(clip_root, {50.0, 50.0});
    clip_window.perform_layout();
    CountingPainter clip_painter;
    clip_window.paint(clip_painter, {0.0, 0.0, 50.0, 50.0});
    bool observed_ancestor_clip = false;
    for (const gui_forms::Rect clip : clip_painter.clip_rectangles) {
        observed_ancestor_clip = observed_ancestor_clip ||
            clip == gui_forms::Rect{0.0, 0.0, 10.0, 10.0};
    }
    expect(observed_ancestor_clip,
           "descendant paint must inherit the intersection of every ancestor edge");

    std::cout << "gallery_interaction_tests: ok\n";
    return 0;
}
