// The by-role paint path (owner ruling, 2026-10-14): a part described by its
// role and states is drawn from the theme, and parts whose role draws nothing
// of its own stay untouched. The shadow role paints the theme's overlay
// shadow (owner decision RB-37).

#include "gui_forms/control.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// One drawing operation as the recording painter saw it.
struct Operation final {
    std::string kind;
    Rect bounds{};
    Color color{};
    Point offset{};
    double radius{};
    double blur{};
};

class RecordingPainter final : public Painter {
public:
    void save() override { operations.push_back({"save", {}, {}}); }
    void restore() override { operations.push_back({"restore", {}, {}}); }
    void translate(Point) override {}
    void clip_rect(Rect rect) override { operations.push_back({"clip", rect, {}}); }
    void fill_rect(Rect rect, Color color) override { operations.push_back({"fill", rect, color}); }
    void fill_rounded_rect(Rect rect, double, Color color) override {
        operations.push_back({"fill_rounded", rect, color});
    }
    void stroke_rect(Rect rect, Color color, double) override {
        operations.push_back({"stroke", rect, color});
    }
    void stroke_rounded_rect(Rect rect, double, Color color, double) override {
        operations.push_back({"stroke_rounded", rect, color});
    }
    void draw_line(Point from, Point to, Color color, double) override {
        const Rect extent{std::min(from.x, to.x), std::min(from.y, to.y),
                          std::abs(to.x - from.x), std::abs(to.y - from.y)};
        operations.push_back({"line", extent, color});
    }
    void draw_text_utf8(Point origin, std::string_view, FontSpec, Color color) override {
        operations.push_back({"text", {origin.x, origin.y, 0.0, 0.0}, color});
    }
    void draw_image(ImageId, Rect destination, double) override {
        operations.push_back({"image", destination, {}});
    }
    void draw_box_shadow(Rect rect, double corner_radius, Point offset, double blur_radius, double,
                         Color color) override {
        operations.push_back({"shadow", rect, color, offset, corner_radius, blur_radius});
    }

    [[nodiscard]] std::size_t count(const char* kind) const {
        std::size_t total = 0U;
        for (const Operation& operation : operations) {
            if (operation.kind == kind) ++total;
        }
        return total;
    }

    std::vector<Operation> operations;
};

AnatomyStateSet states_of(AnatomyState state) {
    AnatomyStateSet states;
    states.insert(state);
    return states;
}

void silent_roles(const Control& control) {
    const AnatomyState none = AnatomyState::normal;
    const PartRole silent[] = {PartRole::region, PartRole::edge, PartRole::perimeter,
                               PartRole::sheen};
    for (const PartRole role : silent) {
        RecordingPainter painter;
        PartPaint part;
        part.role = role;
        part.bounds = {0.0, 0.0, 40.0, 20.0};
        part.states = states_of(none);
        paint_part(painter, control, part);
        require(painter.operations.empty(), "a role the surface material draws paints nothing itself");
    }
    RecordingPainter painter;
    PartPaint empty;
    empty.role = PartRole::face;
    paint_part(painter, control, empty);
    require(painter.operations.empty(), "an empty part paints nothing");
}

void surfaces_and_cues(const Control& control) {
    RecordingPainter face_painter;
    PartPaint face;
    face.role = PartRole::face;
    face.bounds = {0.0, 0.0, 80.0, 24.0};
    face.family = ControlVisualRole::button;
    paint_part(face_painter, control, face);
    require(!face_painter.operations.empty(), "a face paints its recipe's material");

    const Theme& theme = control.effective_theme();
    PartPaint focus;
    focus.role = PartRole::focus;
    focus.bounds = {0.0, 0.0, 80.0, 24.0};
    focus.family = ControlVisualRole::button;
    RecordingPainter unfocused;
    paint_part(unfocused, control, focus);
    require(unfocused.operations.empty(), "a focus part without focus_visible paints nothing");
    focus.states = states_of(AnatomyState::focus_visible);
    RecordingPainter focused;
    paint_part(focused, control, focus);
    const ControlVisualRecipe& recipe =
        theme.resolve(ControlVisualRole::button, part_visual_context(control, focus.states));
    require(focused.count("stroke_rounded") == 1U && focused.operations[0].color == recipe.focus_ring,
            "a visible focus cue strokes one ring in the recipe's focus colour");

    PartPaint preselect = focus;
    preselect.role = PartRole::preselect;
    preselect.states = states_of(AnatomyState::preselect);
    RecordingPainter defaulted;
    paint_part(defaulted, control, preselect);
    const ControlVisualRecipe& default_recipe =
        theme.resolve(ControlVisualRole::button, part_visual_context(control, preselect.states));
    require(defaulted.count("stroke_rounded") == 1U &&
                defaulted.operations[0].color == default_recipe.default_ring,
            "a preselect cue strokes the default ring");
}

void marks_and_text(const Control& control) {
    const Theme& theme = control.effective_theme();
    PartPaint check;
    check.role = PartRole::indicator;
    check.bounds = {2.0, 2.0, 12.0, 9.0};
    check.family = ControlVisualRole::choice;
    check.mark = PartMark::check;
    check.states = states_of(AnatomyState::checked);
    RecordingPainter checked;
    paint_part(checked, control, check);
    const ControlVisualRecipe& choice =
        theme.resolve(ControlVisualRole::choice, part_visual_context(control, check.states));
    require(checked.count("line") == 2U && checked.operations[0].color == choice.glyph,
            "a check mark is two strokes in the recipe's glyph colour");

    PartPaint arrow;
    arrow.role = PartRole::glyph;
    arrow.bounds = {0.0, 0.0, 9.0, 9.0};
    arrow.mark = PartMark::arrow_down;
    RecordingPainter arrows;
    paint_part(arrows, control, arrow);
    require(arrows.count("fill") == 4U, "a 9-unit arrow fills four rows");
    for (const Operation& row : arrows.operations) {
        require(row.bounds.x >= arrow.bounds.x && row.bounds.right() <= arrow.bounds.right() &&
                    row.bounds.y >= arrow.bounds.y && row.bounds.bottom() <= arrow.bounds.bottom(),
                "an arrow stays inside its part");
    }

    PartPaint label;
    label.role = PartRole::label;
    label.bounds = {0.0, 0.0, 100.0, 20.0};
    label.family = ControlVisualRole::button;
    label.text = "Apply";
    RecordingPainter enabled;
    paint_part(enabled, control, label);
    const ControlVisualRecipe& button =
        theme.resolve(ControlVisualRole::button, part_visual_context(control, label.states));
    require(enabled.count("text") == 1U && enabled.count("clip") == 1U,
            "a label draws its text once, clipped to the part");
    for (const Operation& operation : enabled.operations) {
        if (operation.kind == "text") {
            require(operation.color == button.text, "an enabled label uses the recipe's text colour");
        }
    }
    label.states = states_of(AnatomyState::unavailable);
    RecordingPainter unavailable;
    paint_part(unavailable, control, label);
    const ControlVisualRecipe& disabled =
        theme.resolve(ControlVisualRole::button, part_visual_context(control, label.states));
    for (const Operation& operation : unavailable.operations) {
        if (operation.kind == "text") {
            require(operation.color == disabled.text, "an unavailable label uses the theme's disabled text colour");
        }
    }
}

// The strips of a solid arrow as the by-role path first drew them (pinned
// before the arrow kernel was split by direction): `half` one-unit strips,
// strip `row` reaching `row` units either side of the centre, the tip strip
// at the box's centre plus or minus half the depth.
std::vector<Rect> reference_arrow_strips(Rect bounds, PartMark direction) {
    std::vector<Rect> strips;
    const double side = std::min(bounds.width, bounds.height);
    const double half = std::floor(side * 0.5);
    if (half < 1.0) return strips;
    const double centre_x = std::floor(bounds.x + bounds.width * 0.5);
    const double centre_y = std::floor(bounds.y + bounds.height * 0.5);
    const int rows = static_cast<int>(half);
    for (int row = 0; row < rows; ++row) {
        const double extent = static_cast<double>(row);
        const double offset = static_cast<double>(row) - std::floor(half * 0.5);
        const double length = extent * 2.0 + 1.0;
        if (direction == PartMark::arrow_down) {
            strips.push_back({centre_x - extent, centre_y - offset, length, 1.0});
        } else if (direction == PartMark::arrow_up) {
            strips.push_back({centre_x - extent, centre_y + offset - 1.0, length, 1.0});
        } else if (direction == PartMark::arrow_right) {
            strips.push_back({centre_x - offset, centre_y - extent, 1.0, length});
        } else {
            strips.push_back({centre_x + offset - 1.0, centre_y - extent, 1.0, length});
        }
    }
    return strips;
}

// Every arrow direction, in a square box and in an off-grid oblong one,
// fills exactly the pinned strips in order.
void arrow_strips(const Control& control) {
    const PartMark directions[] = {PartMark::arrow_up, PartMark::arrow_down, PartMark::arrow_left,
                                   PartMark::arrow_right};
    const Rect boxes[] = {{0.0, 0.0, 9.0, 9.0}, {3.5, 2.25, 13.0, 10.0}, {1.0, 1.0, 1.0, 1.0}};
    for (const PartMark direction : directions) {
        for (const Rect box : boxes) {
            PartPaint arrow;
            arrow.role = PartRole::glyph;
            arrow.bounds = box;
            arrow.mark = direction;
            RecordingPainter painter;
            paint_part(painter, control, arrow);
            const std::vector<Rect> expected = reference_arrow_strips(box, direction);
            require(painter.operations.size() == expected.size(), "an arrow fills its pinned strip count");
            for (std::size_t index = 0; index < expected.size(); ++index) {
                const Operation& strip = painter.operations[index];
                require(strip.kind == "fill" && strip.bounds == expected[index],
                        "an arrow fills its pinned strips in order");
            }
        }
    }
}

void state_context(const Control& control) {
    require(part_visual_context(control, states_of(AnatomyState::hot)).surface ==
                ControlSurfaceState::hot,
            "hot selects the hot surface");
    require(part_visual_context(control, states_of(AnatomyState::captured)).surface ==
                ControlSurfaceState::pressed,
            "a captured part is drawn pressed");
    require(part_visual_context(control, states_of(AnatomyState::checked)).selected,
            "checked selects the selected recipes");
    require(part_visual_context(control, states_of(AnatomyState::unavailable)).surface ==
                ControlSurfaceState::disabled,
            "an unavailable part is drawn disabled");
    require(!part_visual_context(control, states_of(AnatomyState::focused)).focused,
            "focus without focus_visible draws no cue");
}

// The shadow role casts the theme's overlay shadow around the casting
// surface's rect, layer by layer, in the family's corner radius; it reaches
// as far as overlay_shadow_outsets says; paint_part_from_recipe leaves it to
// paint_part.
void overlay_shadow(const Control& control) {
    const Theme& theme = control.effective_theme();
    const std::vector<MaterialShadow>& layers = theme.overlay_shadow(false);
    require(layers.size() == 2U && theme.overlay_shadow(true).empty(),
            "the default theme casts a two-layer overlay shadow, and none in high contrast");
    for (const MaterialShadow& layer : layers) {
        require(!layer.inset && layer.offset.y > 0.0 && layer.offset.x > 0.0 &&
                    layer.offset.x <= layer.offset.y * 0.5,
                "each layer falls down and right from the upper-left light, right by at most half its drop");
    }
    PartPaint shadow;
    shadow.role = PartRole::shadow;
    shadow.family = ControlVisualRole::tool_tip;
    shadow.bounds = {10.0, 12.0, 120.0, 40.0};
    RecordingPainter painter;
    paint_part(painter, control, shadow);
    require(painter.operations.size() == layers.size() && painter.count("shadow") == layers.size(),
            "a shadow part paints one box shadow per layer and nothing else");
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const Operation& operation = painter.operations[index];
        require(operation.bounds == shadow.bounds && operation.offset == layers[index].offset &&
                    operation.blur == layers[index].blur_radius && operation.color == layers[index].color &&
                    operation.radius == 0.0,
                "each layer is cast from the surface's rect with the theme's offset, blur and colour");
    }
    shadow.corner_radius = 5.0;
    RecordingPainter rounded;
    paint_part(rounded, control, shadow);
    require(rounded.operations.size() == layers.size() && rounded.operations[0].radius == 5.0,
            "a part's own corner radius shapes its shadow");

    const Insets reach = overlay_shadow_outsets(control);
    // The wide layer reaches furthest: three blur radii (30) shifted by its
    // offset (2, 4).
    require(reach.left == 28.0 && reach.top == 26.0 && reach.right == 32.0 && reach.bottom == 34.0,
            "the overlay shadow reaches three blur radii past the surface, shifted down and right");

    RecordingPainter from_recipe;
    const ControlVisualContext context = part_visual_context(control, shadow.states);
    paint_part_from_recipe(from_recipe, shadow, theme.resolve(shadow.family, context), context);
    require(from_recipe.operations.empty(), "the recipe path paints no overlay shadow");
}

// A theme that casts no overlay shadow paints nothing for a shadow part and
// reports no reach; Theme::create refuses inset or too many layers.
void theme_without_overlay_shadow() {
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "no-overlay-shadow";
    definition.overlay_shadow.clear();
    const std::shared_ptr<Control> control = make_control<Control>(StableId("part.flat"));
    (*control).set_theme_override(Theme::create(definition));
    PartPaint shadow;
    shadow.role = PartRole::shadow;
    shadow.bounds = {0.0, 0.0, 60.0, 20.0};
    RecordingPainter painter;
    paint_part(painter, *control, shadow);
    const Insets reach = overlay_shadow_outsets(*control);
    require(painter.operations.empty() && reach == Insets{},
            "a theme without an overlay shadow casts none");

    const std::unique_ptr<ThemeDefinition> held_inset = test_support::professional_definition();

    ThemeDefinition& inset = *held_inset;
    inset.overlay_shadow[0].inset = true;
    bool refused_inset = false;
    try {
        static_cast<void>(Theme::create(inset));
    } catch (const std::invalid_argument&) {
        refused_inset = true;
    }
    const std::unique_ptr<ThemeDefinition> held_crowded = test_support::professional_definition();
    ThemeDefinition& crowded = *held_crowded;
    crowded.high_contrast_overlay_shadow.assign(SurfaceMaterial::maximum_shadows + 1U, MaterialShadow{});
    bool refused_crowded = false;
    try {
        static_cast<void>(Theme::create(crowded));
    } catch (const std::invalid_argument&) {
        refused_crowded = true;
    }
    require(refused_inset && refused_crowded,
            "a theme's overlay shadow must hold at most four outer layers");
}

// A content colour (owner decision W-7): a fill part paints it flat in its
// shape, in place of the recipe's material, at 45 % opacity when disabled
// and as given in high contrast; every other role ignores it.
void content_colors(const Control& control) {
    const Color sample = Color::rgba(192, 0, 0);
    PartPaint fill;
    fill.role = PartRole::fill;
    fill.family = ControlVisualRole::menu_item;
    fill.bounds = {4.0, 4.0, 12.0, 12.0};
    fill.content_color = sample;
    RecordingPainter flat;
    paint_part(flat, control, fill);
    require(flat.operations.size() == 1U && flat.operations[0].kind == "fill" &&
                flat.operations[0].bounds == fill.bounds && flat.operations[0].color == sample,
            "a fill with a content colour is that colour, flat, and nothing else");
    fill.corner_radius = 2.0;
    RecordingPainter rounded;
    paint_part(rounded, control, fill);
    require(rounded.operations.size() == 1U && rounded.operations[0].kind == "fill_rounded" &&
                rounded.operations[0].color == sample,
            "a corner radius shapes the content colour");
    fill.corner_radius.reset();
    fill.states.insert(AnatomyState::disabled);
    RecordingPainter disabled;
    paint_part(disabled, control, fill);
    require(disabled.operations.size() == 1U && disabled.operations[0].color == sample,
            "a disabled part's content colour is drawn exactly as given (GIVEN E2-1)");
    fill.states = {};
    const Theme& theme = control.effective_theme();
    ControlVisualContext contrast = part_visual_context(control, fill.states);
    contrast.high_contrast = true;
    RecordingPainter high;
    paint_part_from_recipe(high, fill, theme.resolve(fill.family, contrast), contrast);
    require(high.operations.size() == 1U && high.operations[0].color == sample,
            "high contrast draws the content colour as given: it is data, as an image is");
    const PartRole others[] = {PartRole::face, PartRole::well, PartRole::label, PartRole::glyph};
    for (const PartRole role : others) {
        PartPaint plain;
        plain.role = role;
        plain.family = ControlVisualRole::menu_item;
        plain.bounds = {0.0, 0.0, 40.0, 20.0};
        plain.text = "Text";
        RecordingPainter without;
        paint_part(without, control, plain);
        plain.content_color = sample;
        RecordingPainter with;
        paint_part(with, control, plain);
        bool same = with.operations.size() == without.operations.size();
        for (std::size_t index = 0; same && index < with.operations.size(); ++index) {
            same = with.operations[index].kind == without.operations[index].kind &&
                   with.operations[index].color == without.operations[index].color;
        }
        require(same, "roles other than fill ignore a content colour");
    }
    PartGeometry first = PartGeometry::from_bounds({0.0, 0.0, 10.0, 10.0});
    PartGeometry second = first;
    second.content_color = sample;
    require(!(first == second) && second.content_color == sample,
            "a part's geometry record carries its content colour");
}

// Whether `painter` recorded an operation of `kind` in `color`.
bool recorded(const RecordingPainter& painter, const char* kind, Color color) {
    for (const Operation& operation : painter.operations) {
        if (operation.kind == kind && operation.color == color) return true;
    }
    return false;
}

// Authored colours (owner decision T4-1): a ground paints the background
// as its material's only fill, keeping its border; text, marks and rules
// take the foreground; a disabled part's text and marks keep the theme's
// disabled colours, and a fill with a content colour shows that.
void authored_colors(const Control& control) {
    const Color ground = Color::rgba(250, 220, 120);
    const Color ink = Color::rgba(120, 20, 90);
    PartPaint face;
    face.role = PartRole::face;
    face.family = ControlVisualRole::button;
    face.bounds = {0.0, 0.0, 60.0, 24.0};
    RecordingPainter plain_face;
    paint_part(plain_face, control, face);
    face.authored_background = ground;
    RecordingPainter styled_face;
    paint_part(styled_face, control, face);
    const bool filled = recorded(styled_face, "fill", ground) || recorded(styled_face, "fill_rounded", ground);
    require(filled && styled_face.count("stroke") + styled_face.count("stroke_rounded") ==
                          plain_face.count("stroke") + plain_face.count("stroke_rounded"),
            "a face paints the authored background as its fill and keeps its border");
    face.authored_background.reset();
    face.authored_foreground = ink;
    RecordingPainter inked_face;
    paint_part(inked_face, control, face);
    require(!recorded(inked_face, "fill", ink) && !recorded(inked_face, "fill_rounded", ink),
            "a face reads no foreground");

    PartPaint label;
    label.role = PartRole::label;
    label.family = ControlVisualRole::button;
    label.bounds = {0.0, 0.0, 60.0, 24.0};
    label.text = "Cell";
    label.authored_foreground = ink;
    label.authored_background = ground;
    RecordingPainter styled_label;
    paint_part(styled_label, control, label);
    const bool grounded = recorded(styled_label, "fill", ground) || recorded(styled_label, "fill_rounded", ground);
    require(recorded(styled_label, "text", ink) && !grounded,
            "a label draws in the authored foreground and paints no background");
    label.states.insert(AnatomyState::disabled);
    const Theme& theme = control.effective_theme();
    const ControlVisualContext off = part_visual_context(control, label.states);
    RecordingPainter disabled_label;
    paint_part(disabled_label, control, label);
    require(recorded(disabled_label, "text", theme.resolve(label.family, off).text),
            "a disabled label keeps the theme's disabled text colour");

    PartPaint check;
    check.role = PartRole::indicator;
    check.family = ControlVisualRole::button;
    check.mark = PartMark::check;
    check.bounds = {0.0, 0.0, 12.0, 9.0};
    check.authored_foreground = ink;
    RecordingPainter inked_check;
    paint_part(inked_check, control, check);
    require(inked_check.count("line") == 2U && recorded(inked_check, "line", ink),
            "a mark is inked with the authored foreground");
    PartPaint rule;
    rule.role = PartRole::divider;
    rule.family = ControlVisualRole::panel;
    rule.bounds = {0.0, 0.0, 60.0, 1.0};
    rule.authored_foreground = ink;
    RecordingPainter inked_rule;
    paint_part(inked_rule, control, rule);
    require(inked_rule.operations.size() == 1U && recorded(inked_rule, "line", ink),
            "a rule is drawn in the authored foreground");

    PartPaint sample;
    sample.role = PartRole::fill;
    sample.family = ControlVisualRole::menu_item;
    sample.bounds = {0.0, 0.0, 12.0, 12.0};
    sample.content_color = Color::rgba(192, 0, 0);
    sample.authored_background = ground;
    RecordingPainter shown;
    paint_part(shown, control, sample);
    require(shown.operations.size() == 1U && shown.operations[0].color == *sample.content_color,
            "a fill with a content colour shows the content colour");

    PartGeometry first = PartGeometry::from_bounds({0.0, 0.0, 10.0, 10.0});
    PartGeometry second = first;
    second.authored_background = ground;
    PartGeometry third = first;
    third.authored_foreground = ink;
    require(!(first == second) && !(first == third) && !(second == third),
            "a part's geometry record carries its authored colours");
}

// A clear plate: one solid fill with no coverage.
// Control colours (WinForms' BackColor and ForeColor): a part without its
// own authored colours takes its control's; a container's colours flow to
// descendants that set none; an inherited background leaves wells alone
// while the control's own fills them; a part's own colours stand over the
// control's; and disabled text stays the theme's.
void control_colors() {
    const Color ground = Color::rgba(240, 200, 90);
    const Color ink = Color::rgba(30, 90, 40);
    const Color own = Color::rgba(200, 230, 250);
    const Color part_ground = Color::rgba(90, 40, 120);
    const std::shared_ptr<Control> parent = make_control<Control>(StableId("part.colors"));
    const std::shared_ptr<Control> child = make_control<Control>(StableId("part.colors.child"));
    (*parent).add_child(child);
    (*parent).set_back_color(ground);
    (*parent).set_fore_color(ink);
    require(!(*child).back_color() && !(*child).fore_color() && (*child).ambient_back_color() == ground &&
                (*child).ambient_fore_color() == ink,
            "a control without its own colours takes its parent's");

    PartPaint face;
    face.role = PartRole::face;
    face.family = ControlVisualRole::button;
    face.bounds = {0.0, 0.0, 60.0, 24.0};
    RecordingPainter inherited_face;
    paint_part(inherited_face, *child, face);
    require(recorded(inherited_face, "fill", ground) || recorded(inherited_face, "fill_rounded", ground),
            "a face takes an inherited background");
    PartPaint well = face;
    well.role = PartRole::well;
    RecordingPainter inherited_well;
    paint_part(inherited_well, *child, well);
    require(!recorded(inherited_well, "fill", ground) && !recorded(inherited_well, "fill_rounded", ground),
            "an inherited background leaves a well alone");
    (*child).set_back_color(own);
    RecordingPainter own_well;
    paint_part(own_well, *child, well);
    require(recorded(own_well, "fill", own) || recorded(own_well, "fill_rounded", own),
            "a control's own background fills its well");
    face.authored_background = part_ground;
    RecordingPainter part_face;
    paint_part(part_face, *child, face);
    const bool part_filled = recorded(part_face, "fill", part_ground) || recorded(part_face, "fill_rounded", part_ground);
    const bool own_filled = recorded(part_face, "fill", own) || recorded(part_face, "fill_rounded", own);
    require(part_filled && !own_filled, "a part's own authored background stands over the control's");

    PartPaint label;
    label.role = PartRole::label;
    label.family = ControlVisualRole::button;
    label.bounds = {0.0, 0.0, 60.0, 24.0};
    label.text = "Cell";
    RecordingPainter inherited_label;
    paint_part(inherited_label, *child, label);
    require(recorded(inherited_label, "text", ink), "a label draws in an inherited foreground");
    label.states.insert(AnatomyState::disabled);
    const ControlVisualContext off = part_visual_context(*child, label.states);
    const Color disabled_text = (*child).effective_theme().resolve(label.family, off).text;
    RecordingPainter disabled_label;
    paint_part(disabled_label, *child, label);
    require(recorded(disabled_label, "text", disabled_text) && !recorded(disabled_label, "text", ink),
            "a disabled label keeps the theme's disabled text under a control's foreground");

    (*child).clear_back_color();
    require((*child).ambient_back_color() == ground, "clearing a control's colour restores its parent's");
    (*parent).clear_back_color();
    (*parent).clear_fore_color();
    require(!(*child).ambient_back_color() && !(*child).ambient_fore_color(),
            "with no colours set anywhere the theme's stand");
}

bool clear_fill(const MaterialFillLayer& fill) noexcept {
    const bool clear = fill.kind == MaterialFillKind::solid && fill.color.alpha == 0U;
    return clear;
}

// Every family has a registered name, and the tooltip family has its own
// recipe (owner decision RB-38) apart from the card it borrowed before.
void families() {
    for (std::size_t index = 0; index < control_visual_role_count; ++index) {
        require(!control_visual_role_name(static_cast<ControlVisualRole>(index)).empty(),
                "every recipe family has a registered name");
    }
    require(control_visual_role_name(ControlVisualRole::tool_tip) == "tool_tip" &&
                control_visual_role_name(ControlVisualRole::count).empty(),
            "the tooltip family is registered as tool_tip");
    const std::shared_ptr<const Theme> theme = default_theme();
    const ControlVisualContext normal{};
    const ControlVisualRecipe& tool_tip = (*theme).resolve(ControlVisualRole::tool_tip, normal);
    const ControlVisualRecipe& card = (*theme).resolve(ControlVisualRole::card, normal);
    require(tool_tip.material.fills.front().color == Color::rgba(255, 255, 225) &&
                tool_tip.material.border && (*tool_tip.material.border).color == Color::rgba(104, 111, 118) &&
                tool_tip.material.corner_radius == 0.0 && tool_tip.text == Color::rgba(24, 31, 38) &&
                !(tool_tip.material == card.material),
            "the default theme's tooltip family keeps the plain tooltip's face, edge and text");

    // The link families (owner decision W-Q5): clear plates in every state,
    // the compatibility style's link colour in every enabled state (hover
    // keeps it) and its disabled text when disabled.
    require(control_visual_role_name(ControlVisualRole::link) == "link" &&
                control_visual_role_name(ControlVisualRole::visited_link) == "visited_link",
            "the link families are registered as link and visited_link");
    const BasicControlStyle& compatibility = (*theme).basic_style();
    const ControlVisualRole links[] = {ControlVisualRole::link, ControlVisualRole::visited_link};
    for (const ControlVisualRole family : links) {
        const Color expected = family == ControlVisualRole::link ? compatibility.link : compatibility.visited_link;
        for (std::size_t index = 0; index < control_surface_state_count; ++index) {
            const ControlSurfaceState state = static_cast<ControlSurfaceState>(index);
            const Color text = state == ControlSurfaceState::disabled ? compatibility.disabled_text : expected;
            for (const bool selected : {false, true}) {
                ControlVisualContext context;
                context.surface = state;
                context.selected = selected;
                const ControlVisualRecipe& recipe = (*theme).resolve(family, context);
                const bool clear = std::all_of(recipe.material.fills.begin(), recipe.material.fills.end(),
                                               &clear_fill) &&
                                   (!recipe.material.border || (*recipe.material.border).color.alpha == 0U);
                require(recipe.text == text && clear, "a link family draws only its text, in the link's colour");
            }
        }
    }
}

} // namespace

// A dotted focus ring (design language: the default theme's ring) is square
// dots one focus width across, one every two widths, centred on the ring the
// solid stroke would follow: the house ring three units inside the bounds.
void dotted_focus() {
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "dotted-focus";
    for (ControlRoleRecipes& role : definition.roles) {
        for (ControlVisualRecipe& recipe : role.ordinary) recipe.dotted_focus = true;
        for (ControlVisualRecipe& recipe : role.selected) recipe.dotted_focus = true;
    }
    const std::shared_ptr<Control> control = make_control<Control>(StableId("part.dotted"));
    (*control).set_theme_override(Theme::create(definition));
    PartPaint focus;
    focus.role = PartRole::focus;
    focus.bounds = {0.0, 0.0, 40.0, 20.0};
    focus.family = ControlVisualRole::button;
    focus.states = states_of(AnatomyState::focus_visible);
    RecordingPainter painter;
    paint_part(painter, *control, focus);
    const ControlVisualRecipe& recipe = (*control).effective_theme().resolve(
        ControlVisualRole::button, part_visual_context(*control, focus.states));
    require(recipe.dotted_focus && recipe.focus_width == 1.0, "the test theme's ring is dotted, one unit wide");
    // The house ring inset by max(3, default width + 1) = 3: a 34 by 14
    // ring whose stroke's dots start at (2.5, 2.5), 17 across and 7 down on
    // each side.
    const double inset = std::max(3.0, recipe.default_width + 1.0);
    const double across = 40.0 - inset * 2.0;
    const double down = 20.0 - inset * 2.0;
    const std::size_t expected = static_cast<std::size_t>(std::ceil(across / 2.0) + std::ceil(down / 2.0)) * 2U;
    bool squares = true;
    bool inked = true;
    for (const Operation& operation : painter.operations) {
        squares = squares && operation.bounds.width == 1.0 && operation.bounds.height == 1.0;
        inked = inked && operation.color == recipe.focus_ring;
    }
    require(painter.count("fill") == expected && painter.operations.size() == expected && squares && inked,
            "a dotted ring is one-unit dots in the focus colour, one every two units, and no stroke");
    const Operation& first = painter.operations.front();
    require(first.bounds.x == inset - 0.5 && first.bounds.y == inset - 0.5,
            "the first dot sits on the ring's top-left corner");
}

// The pointed face (the raised breadcrumb's shape): its nose narrows to a
// tip at the middle of the right edge, the point is clamped to the width,
// and hit testing follows the painted shape. Only surface roles point; a
// label ignores the point.
void pointed_faces(const Control& control) {
    const Rect bounds{10.0, 0.0, 40.0, 20.0};
    require(pointed_face_contains(bounds, 8.0, {49.0, 10.0}), "the tip is inside the face");
    require(!pointed_face_contains(bounds, 8.0, {49.0, 1.0}), "the corner beside the nose is outside");
    require(pointed_face_contains(bounds, 8.0, {41.0, 1.0}), "the body before the shoulder is inside");
    require(!pointed_face_contains(bounds, 8.0, {9.0, 10.0}), "a point left of the face is outside");
    require(pointed_face_contains(bounds, 0.0, {49.0, 1.0}), "without a point the face is square");
    require(!pointed_face_contains(bounds, 100.0, {30.0, 1.0}), "the point is clamped to the width");
    PartPaint face;
    face.role = PartRole::face;
    face.family = ControlVisualRole::button;
    face.bounds = bounds;
    RecordingPainter square;
    paint_part(square, control, face);
    face.point = 8.0;
    RecordingPainter pointed;
    paint_part(pointed, control, face);
    require(pointed.operations.size() > square.operations.size(), "a pointed face paints its nose strips");
    PartPaint label;
    label.role = PartRole::label;
    label.bounds = bounds;
    label.text = "Segment";
    RecordingPainter plain_label;
    paint_part(plain_label, control, label);
    label.point = 8.0;
    RecordingPainter pointed_label;
    paint_part(pointed_label, control, label);
    require(pointed_label.operations.size() == plain_label.operations.size(), "a label ignores the point");
}

int main() {
    dotted_focus();
    const std::shared_ptr<Control> control = make_control<Control>(StableId("part.painting"));
    silent_roles(*control);
    surfaces_and_cues(*control);
    marks_and_text(*control);
    arrow_strips(*control);
    state_context(*control);
    overlay_shadow(*control);
    theme_without_overlay_shadow();
    content_colors(*control);
    authored_colors(*control);
    control_colors();
    families();
    pointed_faces(*control);
    std::cout << "by-role paint path: silent roles, surfaces, cues, marks, text, states, overlay shadow, "
                 "content colours, authored colours, control colours, families and pointed faces passed\n";
    return 0;
}
