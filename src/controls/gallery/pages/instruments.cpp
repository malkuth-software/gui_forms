#include "pages.hpp"

#include "instruments_form.hpp"

#include <array>
#include <cmath>
#include <ios>
#include <locale>
#include <optional>
#include <sstream>
#include <string>

namespace gui_forms::gallery::pages {

// Owns the instruments page (W-5b, W-9): the controls
// demo/pages/instruments.form describes, and their behaviour. It sets the
// gauges' ranges, readings and alarm limits (the limits are not form
// properties), lets the Ghost segments check box turn the unlit segments
// on and off, and drives the linked readout and gauge from the dial.
class InstrumentsPage final : public gui_forms::Component {
public:
    explicit InstrumentsPage(const std::shared_ptr<Surface>& body);

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void on_ghosts_changed(bool ghosts);
    void on_dial_changed(double value);

    // Owns the document's controls; the shell's page body holds them too
    // once mount_document() has moved them there.
    InstrumentsDocument<InstrumentsPage> document_{};
};

InstrumentsPage::InstrumentsPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);

    LinearGauge& pressure = *document_.gallery_instruments_pressure;
    pressure.set_range(0.0, 200.0);
    pressure.value = 128.0;
    pressure.high_alarm = std::optional<double>{170.0};
    LinearGauge& temperature = *document_.gallery_instruments_temperature;
    temperature.set_range(-25.0, 100.0);
    temperature.value = 92.0;
    temperature.high_alarm = std::optional<double>{85.0};
    LinearGauge& level = *document_.gallery_instruments_level;
    level.orientation = Orientation::vertical;
    level.value = 18.0;
    level.low_alarm = std::optional<double>{20.0};
    level.show_value = false;
    RadialGauge& speed = *document_.gallery_instruments_speed;
    speed.set_range(0.0, 8000.0);
    speed.value = 3250.0;
    speed.high_alarm = std::optional<double>{6500.0};

    RadialGauge& linked = *document_.gallery_instruments_dial_gauge;
    linked.high_alarm = std::optional<double>{80.0};
    Dial& dial = *document_.gallery_instruments_dial;
    dial.value = 35.0;
    on(dial.value_changed(), *this, &InstrumentsPage::on_dial_changed);
    on_dial_changed(dial.value.read());
    on((*document_.gallery_instruments_ghosts).checked_changed(), *this, &InstrumentsPage::on_ghosts_changed);
}

void InstrumentsPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move
    // into the shell's page body, a design surface of the same 1000 x 660
    // size, where each is placed by its bounds in design coordinates. The
    // copy of the child list keeps the iteration valid while the root
    // shrinks.
    Control& root = *document_.gallery_instruments_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
    const std::array<std::shared_ptr<ScaledGroupBox>, 3> groups{
        document_.gallery_instruments_readouts, document_.gallery_instruments_gauges, document_.gallery_instruments_knob};
    for (const std::shared_ptr<ScaledGroupBox>& group : groups) {
        (*group).set_style(page_style());
    }
}

void InstrumentsPage::on_ghosts_changed(bool ghosts) {
    const std::array<std::shared_ptr<SevenSegmentDisplay>, 4> displays{
        document_.gallery_instruments_clock, document_.gallery_instruments_reading, document_.gallery_instruments_word,
        document_.gallery_instruments_dial_readout};
    for (const std::shared_ptr<SevenSegmentDisplay>& display : displays) {
        (*display).ghost_segments = ghosts;
    }
}

// The readout shows the dial's value as a whole number and the gauge
// follows it.
void InstrumentsPage::on_dial_changed(double value) {
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << static_cast<long>(std::lround(value));
    (*document_.gallery_instruments_dial_readout).text = text.str();
    (*document_.gallery_instruments_dial_gauge).value = value;
}

void add_instruments(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<InstrumentsPage>(page));
}

}  // namespace gui_forms::gallery::pages
