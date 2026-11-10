/* Kit part: the gf_value forms of fonts, images and collections (stage C5). */

#include "gf_conformance_internal.h"

#include <string.h>

/* A 1x1 RGBA PNG. */
static const uint8_t one_pixel_png[70] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff,
    0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static gf_value blank_value(void) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    return value;
}

static gf_value text_value(const char* text) {
    gf_value value = blank_value();
    value.kind = GF_VALUE_KIND_TEXT;
    value.text = gf_kit_text(text);
    return value;
}

static void fonts(gf_kit* kit) {
    const gf_object label = gf_kit_create(kit, "label", "kit.proxies.font");
    gf_value font = blank_value();
    font.kind = GF_VALUE_KIND_FONT;
    font.font.role = GF_FONT_ROLE_CONTENT;
    font.font.weight = 700u;
    font.font.italic = 1u;
    font.font.size = 18.0;
    font.font.letter_spacing = 0.5;
    gf_kit_status(kit, (*kit).properties.set(label, gf_kit_text("font"), &font), GF_STATUS_OK, "set a font");
    gf_value read = blank_value();
    char text[8];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).properties.get(label, gf_kit_text("font"), &read, text, sizeof(text), &required),
                  GF_STATUS_OK, "read the font back");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_FONT && read.font.role == GF_FONT_ROLE_CONTENT &&
                          read.font.weight == 700u && read.font.italic == 1u && read.font.size == 18.0 &&
                          read.font.letter_spacing == 0.5,
                 "a font round-trips");
    gf_value bad = font;
    bad.font.weight = 1001u;
    gf_kit_status(kit, (*kit).properties.set(label, gf_kit_text("font"), &bad), GF_STATUS_INVALID_ARGUMENT,
                  "a weight past 1000 is refused");
    bad = font;
    bad.font.role = 3u;
    gf_kit_status(kit, (*kit).properties.set(label, gf_kit_text("font"), &bad), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown role is refused");
    bad = font;
    bad.font.letter_spacing = 20.0;
    gf_kit_status(kit, (*kit).properties.set(label, gf_kit_text("font"), &bad), GF_STATUS_INVALID_ARGUMENT,
                  "letter spacing past the size is refused");
    bad = font;
    bad.font.reserved = 1u;
    gf_kit_status(kit, (*kit).properties.set(label, gf_kit_text("font"), &bad), GF_STATUS_INVALID_ARGUMENT,
                  "a font's reserved field is zero");
    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the font label");
}

static void images(gf_kit* kit) {
    uint64_t image = 0u;
    gf_kit_status(kit, (*kit).themes.load_image(one_pixel_png, sizeof(one_pixel_png), &image), GF_STATUS_OK,
                  "load a PNG");
    gf_kit_check(kit, image != 0u, "a loaded image has an id");
    const uint8_t not_png[4] = {1u, 2u, 3u, 4u};
    uint64_t refused = 9u;
    gf_kit_status(kit, (*kit).themes.load_image(not_png, sizeof(not_png), &refused), GF_STATUS_INVALID_ARGUMENT,
                  "bytes that are not a PNG are refused");
    gf_kit_check(kit, refused == 9u, "a refused load leaves its output");
    gf_kit_status(kit, (*kit).themes.load_image(NULL, 3u, &refused), GF_STATUS_INVALID_ARGUMENT,
                  "load_image needs its bytes");

    const gf_object picture = gf_kit_create(kit, "picture_box", "kit.proxies.picture");
    gf_value value = blank_value();
    value.kind = GF_VALUE_KIND_IMAGE;
    value.unsigned_integer = image;
    gf_kit_status(kit, (*kit).properties.set(picture, gf_kit_text("image"), &value), GF_STATUS_OK,
                  "show the image");
    gf_value read = blank_value();
    char text[8];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).properties.get(picture, gf_kit_text("image"), &read, text, sizeof(text), &required),
                  GF_STATUS_OK, "read the image back");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_IMAGE && read.unsigned_integer == image, "an image round-trips");
    gf_kit_status(kit, (*kit).objects.release(picture), GF_STATUS_OK, "release the picture box");
    gf_kit_status(kit, (*kit).themes.remove_image(image), GF_STATUS_OK, "remove the image");
    gf_kit_status(kit, (*kit).themes.remove_image(image), GF_STATUS_NOT_FOUND, "a removed image is gone");
}

static void collections(gf_kit* kit) {
    const gf_object combo = gf_kit_create(kit, "combo_box", "kit.proxies.combo");
    gf_value items[3];
    items[0] = text_value("one");
    items[1] = text_value("two");
    items[2] = text_value("three");
    gf_value value = blank_value();
    value.kind = GF_VALUE_KIND_COLLECTION;
    value.elements = items;
    value.element_count = 3u;
    gf_kit_status(kit, (*kit).properties.set(combo, gf_kit_text("items"), &value), GF_STATUS_OK, "set the items");
    gf_value read = blank_value();
    char text[8];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).properties.get(combo, gf_kit_text("items"), &read, text, sizeof(text), &required),
                  GF_STATUS_OK, "read the items");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_COLLECTION && read.unsigned_integer == 3u,
                 "a collection reads as its count");
    read = blank_value();
    gf_kit_status(kit, (*kit).properties.element_at(combo, gf_kit_text("items"), 1u, &read, text, sizeof(text),
                                                    &required),
                  GF_STATUS_OK, "read one element");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_TEXT && required == 3u && memcmp(text, "two", 3u) == 0,
                 "an element reads as its value");
    gf_kit_status(kit, (*kit).properties.element_at(combo, gf_kit_text("items"), 3u, &read, text, sizeof(text),
                                                    &required),
                  GF_STATUS_NOT_FOUND, "an element past the count is not found");
    gf_kit_status(kit, (*kit).properties.element_at(combo, gf_kit_text("text"), 0u, &read, text, sizeof(text),
                                                    &required),
                  GF_STATUS_INVALID_ARGUMENT, "element_at needs a collection");
    gf_kit_status(kit, (*kit).properties.element_at(combo, gf_kit_text("items"), 2u, &read, text, 2u, &required),
                  GF_STATUS_BUFFER_TOO_SMALL, "a small text buffer reports the size");
    gf_kit_check(kit, required == 5u, "the element's text size is reported");

    /* Elements of a later minor's larger size are stepped by the first
     * element's struct_size (N14). */
    typedef struct wide_value {
        gf_value value;
        uint64_t tail;
    } wide_value;
    wide_value wide[2];
    memset(wide, 0, sizeof(wide));
    wide[0].value = text_value("alpha");
    wide[0].value.struct_size = (uint32_t)sizeof(wide_value);
    wide[0].tail = 0xFFFFFFFFFFFFFFFFull;
    wide[1] = wide[0];
    wide[1].value.text = gf_kit_text("beta");
    gf_value wide_items = blank_value();
    wide_items.kind = GF_VALUE_KIND_COLLECTION;
    wide_items.elements = &wide[0].value;
    wide_items.element_count = 2u;
    gf_kit_status(kit, (*kit).properties.set(combo, gf_kit_text("items"), &wide_items), GF_STATUS_OK,
                  "set items larger than this library's gf_value");
    read = blank_value();
    gf_kit_status(kit, (*kit).properties.element_at(combo, gf_kit_text("items"), 1u, &read, text, sizeof(text),
                                                    &required),
                  GF_STATUS_OK, "read the second wide element");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_TEXT && required == 4u && memcmp(text, "beta", 4u) == 0,
                 "a wide element is read at the caller's stride");
    wide[1].value.struct_size = (uint32_t)sizeof(gf_value);
    gf_kit_status(kit, (*kit).properties.set(combo, gf_kit_text("items"), &wide_items), GF_STATUS_INVALID_ARGUMENT,
                  "every element repeats the first element's struct_size");

    gf_value nested[1];
    nested[0] = value;
    gf_value outer = blank_value();
    outer.kind = GF_VALUE_KIND_COLLECTION;
    outer.elements = nested;
    outer.element_count = 1u;
    gf_kit_status(kit, (*kit).properties.set(combo, gf_kit_text("items"), &outer), GF_STATUS_INVALID_ARGUMENT,
                  "a collection of collections is refused");
    outer.elements = NULL;
    outer.element_count = 2u;
    gf_kit_status(kit, (*kit).properties.set(combo, gf_kit_text("items"), &outer), GF_STATUS_INVALID_ARGUMENT,
                  "a collection's elements are given");
    outer.element_count = 0u;
    gf_kit_status(kit, (*kit).properties.set(combo, gf_kit_text("items"), &outer), GF_STATUS_OK,
                  "an empty collection clears the items");
    gf_kit_status(kit, (*kit).properties.get(combo, gf_kit_text("items"), &read, text, sizeof(text), &required),
                  GF_STATUS_OK, "read the cleared items");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_COLLECTION && read.unsigned_integer == 0u, "the items are cleared");
    gf_kit_status(kit, (*kit).objects.release(combo), GF_STATUS_OK, "release the combo box");
}

void gf_kit_proxies(gf_kit* kit) {
    fonts(kit);
    images(kit);
    collections(kit);
}
