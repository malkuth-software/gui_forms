#include "typography_scale_lab.hpp"

#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/typography/types/typography_types.hpp"
#include "gui_forms/visual_inspection/types/visual_inspection_types.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms_demo/typography_specimen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right, double tolerance = 0.000001) {
    return std::abs(left - right) <= tolerance;
}

class ExactFixtureText final : public TextMetricsProvider {
public:
    ResolvedTextLayout resolve_text_layout_utf8(
        std::string_view text, FontSpec font) override {
        ResolvedTextLayout result;
        result.effective_font = font;
        result.primary_family = font.role == FontRole::control
            ? "Portsmouth Rapids"
            : font.role == FontRole::monospace ? "Cousine" : "Carlito";
        std::size_t scalars{};
        for (const unsigned char byte : text) {
            if ((byte & 0xc0U) != 0x80U) ++scalars;
        }
        result.logical_size = {
            static_cast<double>(scalars) * font.size * 0.52 +
                (scalars > 1U ? (scalars - 1U) * font.letter_spacing : 0.0),
            font.size * 1.18};
        result.ascent = font.size * 0.86;
        result.descent = result.logical_size.height - result.ascent;
        result.status = TextResolutionStatus::exact;
        const std::size_t cjk = text.find("日本語");
        const std::size_t emoji = text.find("🚀");
        if (cjk == std::string_view::npos && emoji == std::string_view::npos) {
            result.runs.push_back(
                {0U, text.size(), result.primary_family, font.weight,
                 font.italic, false});
        } else {
            if (cjk > 0U) {
                result.runs.push_back(
                    {0U, cjk, result.primary_family, font.weight,
                     font.italic, false});
            }
            result.runs.push_back(
                {cjk, std::string_view("日本語").size(), "Noto Sans CJK JP",
                 400, false, true});
            if (emoji > cjk + std::string_view("日本語").size()) {
                result.runs.push_back(
                    {cjk + std::string_view("日本語").size(),
                     emoji - cjk - std::string_view("日本語").size(),
                     result.primary_family, font.weight, font.italic, false});
            }
            result.runs.push_back(
                {emoji, std::string_view("🚀").size(), "Noto Emoji",
                 400, false, true});
            if (emoji + std::string_view("🚀").size() < text.size()) {
                result.runs.push_back(
                    {emoji + std::string_view("🚀").size(),
                     text.size() - emoji - std::string_view("🚀").size(),
                     result.primary_family, font.weight, font.italic, false});
            }
        }
        return result;
    }
};

class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

const VisualPaintOperationSnapshot* text_operation(
    const VisualControlInspection& control, FontRole role, double size) {
    for (const VisualPaintOperationSnapshot& operation :
         control.display_operations) {
        if (operation.operation == VisualPaintOperation::draw_text &&
            operation.font.role == role && near(operation.font.size, size)) {
            return &operation;
        }
    }
    return nullptr;
}

bool is_cjk_fallback_run(const ResolvedFontRun& run) {
    return run.fallback && run.family == "Noto Sans CJK JP";
}

bool is_emoji_fallback_run(const ResolvedFontRun& run) {
    return run.fallback && run.family == "Noto Emoji";
}

bool is_body_specimen_readout(const VisualPaintOperationSnapshot& operation) {
    return operation.operation == VisualPaintOperation::draw_text &&
        operation.text.find("content 12.0/400 · exact · Carlito") !=
            std::string::npos;
}

void test_exact_resolution_redaction_and_roles() {
    std::unique_ptr<Window> window =
        gui_forms::typography_scale_lab::make_typography_scale_lab();
    ExactFixtureText provider;
    (*window).set_text_metrics_provider(&provider);
    (*window).perform_layout();
    NullPainter painter;
    static_cast<void>((*window).paint(painter, {0.0, 0.0, 1320.0, 840.0}));
    const VisualInspectionSnapshot snapshot =
        (*window).visual_inspection_snapshot();
    const VisualControlInspection* title = snapshot.find("typography-scale.title");
    const VisualControlInspection* body = snapshot.find("typography-scale.body");
    const VisualControlInspection* mono = snapshot.find("typography-scale.monospace");
    const VisualControlInspection* fallback = snapshot.find("typography-scale.fallback");
    require(title && body && mono && fallback,
            "typography lab must retain every named role specimen");
    const VisualPaintOperationSnapshot* title_text =
        text_operation(*title, FontRole::control, 17.0);
    const VisualPaintOperationSnapshot* body_text =
        text_operation(*body, FontRole::content, 12.0);
    const VisualPaintOperationSnapshot* mono_text =
        text_operation(*mono, FontRole::monospace, 10.0);
    const VisualPaintOperationSnapshot* fallback_text =
        text_operation(*fallback, FontRole::content, 12.0);
    require(title_text && body_text && mono_text && fallback_text,
            "inspection must retain committed effective FontSpec per specimen");
    require((*title_text).text.empty() && (*title_text).text_byte_count != 0U &&
                (*title_text).resolved_text &&
                (*(*title_text).resolved_text).primary_family == "Portsmouth Rapids",
            "text must remain redacted while exact primary face survives inspection");
    require((*(*body_text).resolved_text).primary_family == "Carlito" &&
                (*(*mono_text).resolved_text).primary_family == "Cousine",
            "content and terminal roles must resolve independently");
    require((*(*fallback_text).resolved_text).status == TextResolutionStatus::exact &&
                (*(*fallback_text).resolved_text).runs.size() >= 4U &&
                std::any_of((*(*fallback_text).resolved_text).runs.begin(),
                            (*(*fallback_text).resolved_text).runs.end(),
                            is_cjk_fallback_run) &&
                std::any_of((*(*fallback_text).resolved_text).runs.begin(),
                            (*(*fallback_text).resolved_text).runs.end(),
                            is_emoji_fallback_run),
            "inspection must report actual per-cluster fallback run families");

    VisualInspectionOptions readable_options;
    readable_options.include_text = true;
    const VisualInspectionSnapshot readable =
        (*window).visual_inspection_snapshot(readable_options);
    const VisualControlInspection* inspector =
        readable.find("typography-scale.inspector");
    require(inspector != nullptr &&
                std::any_of((*inspector).display_operations.begin(),
                            (*inspector).display_operations.end(),
                            is_body_specimen_readout),
            "inspector must prefer the representative specimen text over its caption");
}

void test_text_scale_reflows_without_role_drift() {
    std::unique_ptr<Window> window =
        gui_forms::typography_scale_lab::make_typography_scale_lab();
    ExactFixtureText provider;
    (*window).set_text_metrics_provider(&provider);
    const std::array<double, 4U> scales{1.0, 1.25, 1.5, 2.0};
    double previous_title_height{};
    for (const double scale : scales) {
        (*window).set_text_scale(scale);
        (*window).perform_layout();
        const VisualInspectionSnapshot snapshot =
            (*window).visual_inspection_snapshot({.include_display_operations = false});
        const VisualControlInspection* title = snapshot.find("typography-scale.title");
        const VisualControlInspection* control = snapshot.find("typography-scale.control");
        const VisualControlInspection* long_label = snapshot.find("typography-scale.long");
        require(title && control && long_label,
                "text-scale geometry must retain all essential specimens");
        require((*title).layout.absolute_bounds.height > previous_title_height &&
                    ((*control).layout.absolute_bounds.x !=
                         (*title).layout.absolute_bounds.x ||
                     (*control).layout.absolute_bounds.y >=
                         (*title).layout.absolute_bounds.y +
                             (*title).layout.absolute_bounds.height) &&
                    (*long_label).layout.absolute_bounds.y +
                        (*long_label).layout.absolute_bounds.height <= 840.0 + 0.001,
                "100/125/150/200% layout must reflow without specimen overlap or clipping");
        previous_title_height = (*title).layout.absolute_bounds.height;

        const Control::Ptr node = (*window).find("typography-scale.title");
        const TypographySpecimen* specimen =
            dynamic_cast<const TypographySpecimen*>(node.get());
        require(specimen != nullptr &&
                    near((*specimen).resolved_layout().effective_font.size,
                         17.0 * scale) &&
                    (*specimen).resolved_layout().primary_family ==
                        "Portsmouth Rapids",
                "text scale must change effective size without role/family drift");
    }
}

void test_device_scale_changes_snapping_not_logical_metrics() {
    std::unique_ptr<Window> window =
        gui_forms::typography_scale_lab::make_typography_scale_lab();
    ExactFixtureText provider;
    (*window).set_text_metrics_provider(&provider);
    const Control::Ptr node = (*window).find("typography-scale.body");
    const TypographySpecimen* specimen =
        dynamic_cast<const TypographySpecimen*>(node.get());
    require(specimen != nullptr, "body specimen must be queryable");
    (*window).set_scale(1.0);
    const ResolvedTextLayout at_one = (*specimen).resolved_layout();
    (*window).set_scale(2.0);
    const ResolvedTextLayout at_two = (*specimen).resolved_layout();
    require(at_one == at_two &&
                near(snap_text_baseline(17.25, 1.0), 17.0) &&
                near(snap_text_baseline(17.25, 2.0), 17.5),
            "display scale must alter final pixel snapping, not logical font metrics");
}

void test_unavailable_and_invalid_resolution_are_labelled() {
    std::unique_ptr<Window> window =
        gui_forms::typography_scale_lab::make_typography_scale_lab();
    const Control::Ptr node = (*window).find("typography-scale.body");
    const TypographySpecimen* specimen =
        dynamic_cast<const TypographySpecimen*>(node.get());
    require(specimen != nullptr &&
                (*specimen).resolved_layout().status ==
                    TextResolutionStatus::estimated &&
                (*specimen).resolved_layout().primary_family.empty(),
            "headless provider absence must be estimated, never a fabricated face");
    const ResolvedTextLayout invalid = estimate_text_layout_utf8(
        "broken", {FontRole::content, -1.0, 400, false});
    require(invalid.status == TextResolutionStatus::invalid_request &&
                invalid.runs.empty() && invalid.primary_family.empty(),
            "invalid FontSpec must fail closed without inferred host identity");
    const std::string invalid_utf8(1U, static_cast<char>(0xc3));
    const ResolvedTextLayout invalid_text = estimate_text_layout_utf8(
        invalid_utf8, {FontRole::content, 12.0, 400, false});
    require(invalid_text.status == TextResolutionStatus::invalid_request &&
                invalid_text.runs.empty() && invalid_text.primary_family.empty(),
            "invalid UTF-8 must fail closed without estimated geometry");
}

} // namespace

int main() {
    try {
        test_exact_resolution_redaction_and_roles();
        test_text_scale_reflows_without_role_drift();
        test_device_scale_changes_snapping_not_logical_metrics();
        test_unavailable_and_invalid_resolution_are_labelled();
        std::cout << "gui_forms_typography_scale_lab_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_typography_scale_lab_tests: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
