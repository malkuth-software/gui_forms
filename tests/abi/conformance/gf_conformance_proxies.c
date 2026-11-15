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
    /* The reserved word is the bundled family id (since 1.2, K1). */
    uint32_t family = 0u;
    gf_kit_status(kit, (*kit).fonts.family_id(gf_kit_text("selawik"), &family), GF_STATUS_OK,
                  "a bundled family has an id, its name matched without case");
    gf_kit_check(kit, family != 0u, "a family id is not zero");
    char name[32];
    gf_kit_status(kit, (*kit).fonts.family_name(family, name, sizeof(name), &required), GF_STATUS_OK,
                  "the id names its family");
    gf_kit_check(kit, required == 7u && memcmp(name, "Selawik", 7u) == 0, "the family's name round-trips");
    /* Since 1.3 a role's face has an id, and a Windows Forms name resolves
     * through the substitution table to a bundled family. */
    uint32_t carlito = 0u;
    gf_kit_status(kit, (*kit).fonts.family_id(gf_kit_text("Carlito"), &carlito), GF_STATUS_OK,
                  "a role's face has an id");
    gf_kit_check(kit, carlito != 0u && carlito != family, "a role's face has its own id");
    gf_kit_status(kit, (*kit).fonts.family_name(carlito, name, sizeof(name), &required), GF_STATUS_OK,
                  "the role's id names its family");
    gf_kit_check(kit, required == 7u && memcmp(name, "Carlito", 7u) == 0, "the role family's name round-trips");
    uint32_t substituted = 0u;
    gf_kit_status(kit, (*kit).fonts.family_id(gf_kit_text("segoe ui"), &substituted), GF_STATUS_OK,
                  "a substituted name resolves");
    gf_kit_check(kit, substituted == family, "Segoe UI resolves to Selawik");
    /* Since font pack stage 4 the substitution table's own families have ids
     * too, and a substitution finds its family by name: Segoe UI Semibold is
     * Selawik Semibold (whose regular face is Selawik's semibold), Arial is
     * Arimo. */
    gf_kit_status(kit, (*kit).fonts.family_id(gf_kit_text("Segoe UI Semibold"), &substituted), GF_STATUS_OK,
                  "a substitution finds its family by name");
    gf_kit_check(kit, substituted != 0u && substituted != family, "Segoe UI Semibold has its own family");
    gf_kit_status(kit, (*kit).fonts.family_name(substituted, name, sizeof(name), &required), GF_STATUS_OK,
                  "the substituted id names its family");
    gf_kit_check(kit, required == 16u && memcmp(name, "Selawik Semibold", 16u) == 0,
                 "Segoe UI Semibold resolves to Selawik Semibold");
    uint32_t arimo = 0u;
    gf_kit_status(kit, (*kit).fonts.family_id(gf_kit_text("Arimo"), &arimo), GF_STATUS_OK,
                  "a substitution table family has an id");
    gf_kit_status(kit, (*kit).fonts.family_id(gf_kit_text("Arial"), &substituted), GF_STATUS_OK,
                  "a metric match resolves");
    gf_kit_check(kit, arimo != 0u && substituted == arimo, "Arial resolves to Arimo");
    uint32_t untouched = 77u;
    gf_kit_status(kit, (*kit).fonts.family_id(gf_kit_text("Comic Sans MS"), &untouched), GF_STATUS_NOT_FOUND,
                  "a host font has no id");
    gf_kit_check(kit, untouched == 77u, "a refused lookup leaves its output");
    gf_kit_status(kit, (*kit).fonts.family_name(0u, name, sizeof(name), &required), GF_STATUS_NOT_FOUND,
                  "zero names no family");
    gf_value familied = font;
    familied.font.reserved = family;
    gf_kit_status(kit, (*kit).properties.set(label, gf_kit_text("font"), &familied), GF_STATUS_OK,
                  "set a font with a family");
    read = blank_value();
    gf_kit_status(kit, (*kit).properties.get(label, gf_kit_text("font"), &read, text, sizeof(text), &required),
                  GF_STATUS_OK, "read the familied font back");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_FONT && read.font.reserved == family &&
                          read.font.role == GF_FONT_ROLE_CONTENT,
                 "the family round-trips beside the role");
    bad = font;
    bad.font.reserved = 200u;
    gf_kit_status(kit, (*kit).properties.set(label, gf_kit_text("font"), &bad), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown family id is refused");
    bad = font;
    bad.font.reserved = family | (0xFFFFu << 16);
    gf_kit_status(kit, (*kit).properties.set(label, gf_kit_text("font"), &bad), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown family stack is refused");
    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the font label");
}

/* A minimal TrueType face that passes the load checks (one empty glyph, a
 * format 4 cmap, the required tables): the bytes, big-endian, into font.
 * Returns the length written. The tables, in tag order, at 4-byte aligned
 * offsets after the 12-byte header and 9 records. */
enum { app_font_tables = 9, app_font_bytes = 12 + 16 * app_font_tables + 36 + 0 + 56 + 36 + 4 + 4 + 8 + 8 + 32 };

static void put16(uint8_t* at, uint32_t value) {
    at[0] = (uint8_t)((value >> 8) & 0xFFu);
    at[1] = (uint8_t)(value & 0xFFu);
}

static void put32(uint8_t* at, uint32_t value) {
    put16(at, value >> 16);
    put16(at + 2, value & 0xFFFFu);
}

static size_t minimal_app_font(uint8_t font[app_font_bytes]) {
    /* Tag, length (padded to 4 in the layout), in tag order. */
    static const char tags[app_font_tables][5] = {"cmap", "glyf", "head", "hhea", "hmtx",
                                                   "loca", "maxp", "name", "post"};
    static const uint32_t lengths[app_font_tables] = {36u, 0u, 54u, 36u, 4u, 4u, 6u, 6u, 32u};
    memset(font, 0, app_font_bytes);
    put32(font, 0x00010000u);
    put16(font + 4, app_font_tables);
    size_t offset = 12u + 16u * app_font_tables;
    uint32_t placed[app_font_tables];
    for (size_t index = 0u; index < app_font_tables; ++index) {
        uint8_t* const record = font + 12u + 16u * index;
        memcpy(record, tags[index], 4u);
        put32(record + 8, (uint32_t)offset);
        put32(record + 12, lengths[index]);
        placed[index] = (uint32_t)offset;
        offset += (lengths[index] + 3u) & ~3u;
    }
    /* cmap: version 0, one record (3, 1) at 12, a format 4 subtable with
     * the closing 0xFFFF segment only. */
    uint8_t* const cmap = font + placed[0];
    put16(cmap + 2, 1u);
    put16(cmap + 4, 3u);
    put16(cmap + 6, 1u);
    put32(cmap + 8, 12u);
    put16(cmap + 12, 4u);
    put16(cmap + 14, 24u);
    put16(cmap + 18, 2u);
    put16(cmap + 20, 2u);
    put16(cmap + 26, 0xFFFFu);
    put16(cmap + 30, 0xFFFFu);
    put16(cmap + 32, 1u);
    /* head: version, magic, unitsPerEm 1000, short loca offsets. */
    uint8_t* const head = font + placed[2];
    put32(head, 0x00010000u);
    put32(head + 12, 0x5F0F3CF5u);
    put16(head + 18, 1000u);
    /* hhea: version, ascender, descender, one metric. */
    uint8_t* const hhea = font + placed[3];
    put32(hhea, 0x00010000u);
    put16(hhea + 4, 800u);
    put16(hhea + 6, 0xFF38u);
    put16(hhea + 34, 1u);
    /* hmtx: the glyph's advance. */
    put16(font + placed[4], 500u);
    /* loca stays two zero offsets: the glyph is empty. maxp 0.5, one glyph. */
    put32(font + placed[6], 0x00005000u);
    put16(font + placed[6] + 4, 1u);
    /* name: format 0, no records, strings at 6. */
    put16(font + placed[7] + 4, 6u);
    /* post 3.0. */
    put32(font + placed[8], 0x00030000u);
    return offset;
}

/* Application font families (since 1.5, provisional): load, a gf_font that
 * carries the family, refusals and unload. */
static void app_fonts(gf_kit* kit) {
    uint8_t bytes[app_font_bytes];
    const size_t size = minimal_app_font(bytes);
    gf_kit_check(kit, size == app_font_bytes, "the minimal face fills its buffer");
    gf_app_font_face face;
    memset(&face, 0, sizeof(face));
    face.struct_size = (uint32_t)sizeof(face);
    face.weight = 400u;
    face.bytes.data = bytes;
    face.bytes.size = size;
    uint32_t family = 0u;
    gf_kit_status(kit, (*kit).fonts.load_family(gf_kit_text("Kit Application Face"), &face, 1u, &family),
                  GF_STATUS_OK, "load an application family");
    gf_kit_check(kit, family >= 256u, "an application family's id is above the bundled ids");
    uint32_t again = 0u;
    gf_kit_status(kit, (*kit).fonts.load_family(gf_kit_text("kit application face"), &face, 1u, &again),
                  GF_STATUS_INVALID_ARGUMENT, "a loaded family's name is taken, without case");
    gf_kit_status(kit, (*kit).fonts.load_family(gf_kit_text("Carlito"), &face, 1u, &again),
                  GF_STATUS_INVALID_ARGUMENT, "a bundled family's name is taken");
    gf_kit_status(kit, (*kit).fonts.load_family(gf_kit_text("Kit, Face"), &face, 1u, &again),
                  GF_STATUS_INVALID_ARGUMENT, "a name with a comma is refused");
    gf_kit_status(kit, (*kit).fonts.load_family(gf_kit_text("Kit Other"), &face, 0u, &again),
                  GF_STATUS_INVALID_ARGUMENT, "a family needs a face");
    gf_app_font_face broken = face;
    broken.bytes.data = one_pixel_png;
    broken.bytes.size = sizeof(one_pixel_png);
    gf_kit_status(kit, (*kit).fonts.load_family(gf_kit_text("Kit Other"), &broken, 1u, &again),
                  GF_STATUS_INVALID_ARGUMENT, "bytes that are not a font are refused");
    broken = face;
    broken.weight = 0u;
    gf_kit_status(kit, (*kit).fonts.load_family(gf_kit_text("Kit Other"), &broken, 1u, &again),
                  GF_STATUS_INVALID_ARGUMENT, "a weight of zero is refused");
    gf_kit_check(kit, again == 0u, "refused loads leave their output");

    /* A gf_font may carry the family: where it draws is decided when the
     * font is drawn (a stock label draws its bundled faces). */
    const gf_object label = gf_kit_create(kit, "label", "kit.proxies.app_font");
    gf_value font = blank_value();
    font.kind = GF_VALUE_KIND_FONT;
    font.font.role = GF_FONT_ROLE_CONTENT;
    font.font.weight = 400u;
    font.font.size = 12.0;
    font.font.reserved = family;
    gf_kit_status(kit, (*kit).properties.set(label, gf_kit_text("font"), &font), GF_STATUS_OK,
                  "a font may carry an application family");

    uint32_t deferred = 9u;
    gf_kit_status(kit, (*kit).fonts.unload_family(family, &deferred), GF_STATUS_OK, "unload the family");
    gf_kit_check(kit, deferred == 0u || deferred == 1u, "an unload says whether it was deferred");
    gf_kit_status(kit, (*kit).fonts.unload_family(family, NULL), GF_STATUS_NOT_FOUND,
                  "an unloaded family is not loaded");
    gf_kit_status(kit, (*kit).fonts.unload_family(3u, NULL), GF_STATUS_NOT_FOUND,
                  "a bundled family cannot be unloaded");
    gf_kit_status(kit, (*kit).properties.set(label, gf_kit_text("font"), &font), GF_STATUS_OK,
                  "a font naming an unloaded family is still a known family");
    gf_kit_status(kit, (*kit).fonts.load_family(gf_kit_text("Kit Application Face"), &face, 1u, &again),
                  GF_STATUS_OK, "an unloaded family's name is free again");
    gf_kit_check(kit, again != family, "an id is never reused");
    gf_kit_status(kit, (*kit).fonts.unload_family(again, NULL), GF_STATUS_OK, "unload the second family");
    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the application font label");
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

    /* read_elements reads a range in one call, its text packed (N15). */
    gf_value range[2];
    range[0] = blank_value();
    range[1] = blank_value();
    char packed[16];
    gf_kit_status(kit, (*kit).properties.read_elements(combo, gf_kit_text("items"), 1u, 2u, range, packed, 2u,
                                                       &required),
                  GF_STATUS_BUFFER_TOO_SMALL, "read_elements measures its text first");
    gf_kit_check(kit, required == 8u && range[0].kind == 0u, "a short buffer reports the total and writes no value");
    gf_kit_status(kit, (*kit).properties.read_elements(combo, gf_kit_text("items"), 1u, 2u, range, packed,
                                                       sizeof(packed), &required),
                  GF_STATUS_OK, "read two elements at once");
    gf_kit_check(kit, range[0].kind == GF_VALUE_KIND_TEXT && range[0].text.size == 3u &&
                          memcmp(range[0].text.data, "two", 3u) == 0 && range[1].text.size == 5u &&
                          memcmp(range[1].text.data, "three", 5u) == 0 && range[1].text.data == packed + 3,
                 "the range reads in order, its text one after another");
    gf_kit_status(kit, (*kit).properties.read_elements(combo, gf_kit_text("items"), 2u, 2u, range, packed,
                                                       sizeof(packed), &required),
                  GF_STATUS_NOT_FOUND, "a range past the count is not found");
    gf_kit_status(kit, (*kit).properties.read_elements(combo, gf_kit_text("text"), 0u, 0u, range, packed,
                                                       sizeof(packed), &required),
                  GF_STATUS_INVALID_ARGUMENT, "read_elements needs a collection");
    gf_kit_status(kit, (*kit).properties.read_elements(combo, gf_kit_text("items"), 0u,
                                                       GF_PROPERTY_MAXIMUM_ELEMENTS_PER_CALL + 1u, range, packed,
                                                       sizeof(packed), &required),
                  GF_STATUS_LIMIT_EXCEEDED, "read_elements bounds one call");

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
    app_fonts(kit);
    images(kit);
    collections(kit);
}
