/* Kit part: properties by name through gf_value. */

#include "gf_conformance_internal.h"

#include <math.h>
#include <string.h>

static gf_value empty_value(void) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(gf_value);
    return value;
}

/* Sets a property and reads it back; returns the first failing status. */
static gf_status round_trip(gf_kit* kit, gf_object object, const char* property, const gf_value* value,
                            gf_value* read, char* text, uint64_t text_capacity) {
    const gf_status set = (*kit).properties.set(object, gf_kit_text(property), value);
    if (set != GF_STATUS_OK) {
        return set;
    }
    uint64_t required = 0u;
    *read = empty_value();
    const gf_status got = (*kit).properties.get(object, gf_kit_text(property), read, text, text_capacity, &required);
    return got;
}

/* The kinds beyond text: point, size, rectangle, insets, colour and
 * enumeration through set and get, and the kinds without a form yet. */
static void geometry_and_enumerations(gf_kit* kit) {
    const gf_object button = gf_kit_create(kit, "button", "kit.values.geometry");
    char text[64];
    gf_value read = empty_value();

    gf_value bounds = empty_value();
    bounds.kind = GF_VALUE_KIND_RECTANGLE;
    bounds.rectangle.x = 4.0;
    bounds.rectangle.y = 6.0;
    bounds.rectangle.width = 120.0;
    bounds.rectangle.height = 30.0;
    gf_kit_status(kit, round_trip(kit, button, "bounds", &bounds, &read, text, sizeof(text)), GF_STATUS_OK,
                  "a rectangle round-trips");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_RECTANGLE && read.rectangle.x == 4.0 && read.rectangle.y == 6.0 &&
                          read.rectangle.width == 120.0 && read.rectangle.height == 30.0,
                 "the rectangle reads back as written");

    gf_value offset = empty_value();
    offset.kind = GF_VALUE_KIND_POINT;
    gf_kit_status(kit, round_trip(kit, button, "auto_scroll_offset", &offset, &read, text, sizeof(text)),
                  GF_STATUS_OK, "a point round-trips");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_POINT && read.point.x == 0.0 && read.point.y == 0.0,
                 "the point reads back as written");

    gf_value minimum = empty_value();
    minimum.kind = GF_VALUE_KIND_SIZE;
    minimum.size.width = 40.0;
    minimum.size.height = 12.0;
    gf_kit_status(kit, round_trip(kit, button, "minimum_size", &minimum, &read, text, sizeof(text)), GF_STATUS_OK,
                  "a size round-trips");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_SIZE && read.size.width == 40.0 && read.size.height == 12.0,
                 "the size reads back as written");

    gf_value margin = empty_value();
    margin.kind = GF_VALUE_KIND_INSETS;
    margin.insets.left = 1.0;
    margin.insets.top = 2.0;
    margin.insets.right = 3.0;
    margin.insets.bottom = 4.0;
    gf_kit_status(kit, round_trip(kit, button, "margin", &margin, &read, text, sizeof(text)), GF_STATUS_OK,
                  "insets round-trip");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_INSETS && read.insets.left == 1.0 && read.insets.top == 2.0 &&
                          read.insets.right == 3.0 && read.insets.bottom == 4.0,
                 "the insets read back as written");
    margin.insets.bottom = INFINITY;
    gf_kit_status(kit, (*kit).properties.set(button, gf_kit_text("margin"), &margin), GF_STATUS_INVALID_ARGUMENT,
                  "insets that are not finite are refused");

    gf_value dock = empty_value();
    dock.kind = GF_VALUE_KIND_ENUMERATION;
    dock.signed_integer = 5;
    gf_kit_status(kit, round_trip(kit, button, "dock", &dock, &read, text, sizeof(text)), GF_STATUS_OK,
                  "an enumeration round-trips by value");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_ENUMERATION && read.signed_integer == 5 &&
                          gf_kit_view_equals(read.text, "fill"),
                 "the enumeration reads back with its choice's name");
    dock.signed_integer = 77;
    gf_kit_status(kit, (*kit).properties.set(button, gf_kit_text("dock"), &dock), GF_STATUS_INVALID_ARGUMENT,
                  "a value outside the enumeration is refused");
    gf_value anchor = empty_value();
    anchor.kind = GF_VALUE_KIND_ENUMERATION;
    anchor.signed_integer = 5;
    gf_kit_status(kit, round_trip(kit, button, "anchor", &anchor, &read, text, sizeof(text)), GF_STATUS_OK,
                  "a combination of flags round-trips");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_ENUMERATION && read.signed_integer == 5 && read.text.size > 0u,
                 "the combination reads back with its names");

    gf_value font = empty_value();
    font.kind = GF_VALUE_KIND_FONT;
    gf_kit_status(kit, (*kit).properties.set(button, gf_kit_text("font"), &font), GF_STATUS_UNSUPPORTED_CAPABILITY,
                  "a font has no gf_value form yet");
    uint64_t required = 0u;
    read = empty_value();
    gf_kit_status(kit, (*kit).properties.get(button, gf_kit_text("font"), &read, text, sizeof(text), &required),
                  GF_STATUS_UNSUPPORTED_CAPABILITY, "nor can a font be read");
    gf_kit_status(kit, (*kit).objects.release(button), GF_STATUS_OK, "release the geometry button");

    const gf_object label = gf_kit_create(kit, "Label", "kit.values.colour");
    gf_value colour = empty_value();
    colour.kind = GF_VALUE_KIND_COLOR;
    colour.color = 0x336699CCu;
    gf_kit_status(kit, round_trip(kit, label, "fore_color", &colour, &read, text, sizeof(text)), GF_STATUS_OK,
                  "a colour round-trips");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_COLOR && read.color == 0x336699CCu,
                 "the colour reads back as 0xRRGGBBAA");
    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the colour label");
}

void gf_kit_values(gf_kit* kit) {
    const gf_object button = gf_kit_create(kit, "Button", "kit.values");
    gf_kit_status(kit, gf_kit_set_text(kit, button, "text", "Hello"), GF_STATUS_OK, "set a text property");

    gf_value value = empty_value();
    char text[16];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).properties.get(button, gf_kit_text("text"), &value, text, sizeof(text), &required),
                  GF_STATUS_OK, "get a text property");
    gf_kit_check(kit, value.kind == GF_VALUE_KIND_TEXT && gf_kit_view_equals(value.text, "Hello") &&
                          value.text.data == text && required == 5u,
                 "the text arrives in the caller's buffer");

    gf_value untouched = empty_value();
    untouched.kind = 77u;
    gf_kit_status(kit, (*kit).properties.get(button, gf_kit_text("text"), &untouched, text, 2u, &required),
                  GF_STATUS_BUFFER_TOO_SMALL, "a short text buffer is refused");
    gf_kit_check(kit, untouched.kind == 77u && required == 5u, "a short buffer leaves the value and reports the size");

    value = empty_value();
    gf_kit_status(kit, (*kit).properties.get(button, gf_kit_text("enabled"), &value, NULL, 0u, &required),
                  GF_STATUS_OK, "get a boolean property");
    gf_kit_check(kit, value.kind == GF_VALUE_KIND_BOOLEAN && value.boolean == 1u && required == 0u,
                 "a new control is enabled");
    gf_value disabled = empty_value();
    disabled.kind = GF_VALUE_KIND_BOOLEAN;
    disabled.boolean = 0u;
    gf_kit_status(kit, (*kit).properties.set(button, gf_kit_text("enabled"), &disabled), GF_STATUS_OK,
                  "set a boolean property");
    value = empty_value();
    gf_kit_status(kit, (*kit).properties.get(button, gf_kit_text("enabled"), &value, NULL, 0u, &required),
                  GF_STATUS_OK, "get it back");
    gf_kit_check(kit, value.kind == GF_VALUE_KIND_BOOLEAN && value.boolean == 0u, "the control is disabled");
    gf_kit_status(kit, (*kit).properties.reset(button, gf_kit_text("enabled")), GF_STATUS_OK, "reset");
    value = empty_value();
    gf_kit_status(kit, (*kit).properties.get(button, gf_kit_text("enabled"), &value, NULL, 0u, &required),
                  GF_STATUS_OK, "get after reset");
    gf_kit_check(kit, value.boolean == 1u, "reset restores the default");

    gf_kit_status(kit, (*kit).properties.get(button, gf_kit_text("no_such_property"), &value, NULL, 0u, &required),
                  GF_STATUS_NOT_FOUND, "an unknown property is not found");
    gf_kit_status(kit, (*kit).properties.reset(button, gf_kit_text("no_such_property")), GF_STATUS_NOT_FOUND,
                  "reset of an unknown property is not found");
    gf_kit_status(kit, gf_kit_set_text(kit, button, "no_such_property", "x"), GF_STATUS_NOT_FOUND,
                  "set of an unknown property is not found");
    gf_kit_status(kit, gf_kit_set_text(kit, button, "enabled", "neither"), GF_STATUS_INVALID_ARGUMENT,
                  "a value that does not convert is refused");
    gf_kit_status(kit, gf_kit_set_text(kit, button, "text", "\xC3\x28"), GF_STATUS_INVALID_ARGUMENT,
                  "malformed UTF-8 is refused");
    gf_value unknown = empty_value();
    unknown.kind = 99u;
    gf_kit_status(kit, (*kit).properties.set(button, gf_kit_text("enabled"), &unknown),
                  GF_STATUS_UNSUPPORTED_CAPABILITY, "an unknown gf_value kind is refused");
    gf_value small = empty_value();
    small.struct_size = 8u;
    gf_kit_status(kit, (*kit).properties.get(button, gf_kit_text("text"), &small, text, sizeof(text), &required),
                  GF_STATUS_INVALID_ARGUMENT, "a gf_value below the 1.0 size is refused");
    gf_kit_status(kit, (*kit).objects.release(button), GF_STATUS_OK, "release");
    geometry_and_enumerations(kit);
}
