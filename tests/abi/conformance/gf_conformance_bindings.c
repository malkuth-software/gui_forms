/* Kit part: data binding (GF_TABLE_BINDINGS): a binding source's records
 * and position, and a text box's text bound to one of their fields. */

#include "gf_conformance_internal.h"

#include <string.h>

static gf_value text_value(const char* text) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    value.kind = GF_VALUE_KIND_TEXT;
    value.text = gf_kit_text(text);
    return value;
}

/* Adds a record whose one field, title, holds title. */
static gf_status add_titled(gf_kit* kit, gf_object source, const char* stable_id, const char* title,
                            uint64_t* index) {
    const gf_value value = text_value(title);
    gf_binding_field field;
    field.name = gf_kit_text("title");
    field.value = &value;
    gf_binding_record record;
    memset(&record, 0, sizeof(record));
    record.struct_size = (uint32_t)sizeof(record);
    record.editable = 1u;
    record.stable_id = gf_kit_text(stable_id);
    record.fields = &field;
    record.field_count = 1u;
    return (*kit).bindings.add_record(source, &record, index);
}

/* Whether the source's current title is expected. */
static int current_title_is(gf_kit* kit, gf_object source, const char* expected) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    char text[64];
    uint64_t required = 0u;
    if ((*kit).bindings.current_field(source, gf_kit_text("title"), &value, text, sizeof(text), &required) !=
        GF_STATUS_OK) {
        return 0;
    }
    return value.kind == GF_VALUE_KIND_TEXT && gf_kit_view_equals(value.text, expected);
}

/* Whether the control's text is expected. */
static int text_is(gf_kit* kit, gf_object control, const char* expected) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    char text[64];
    uint64_t required = 0u;
    if ((*kit).properties.get(control, gf_kit_text("text"), &value, text, sizeof(text), &required) !=
        GF_STATUS_OK) {
        return 0;
    }
    return gf_kit_view_equals(value.text, expected);
}

/* N31: a C data provider. Rows live here; the source reads them in place. */
typedef struct kit_provider {
    uint64_t rows;
    const char* names[8];
    double amounts[8];
    const char* ids[8];
    uint32_t writes;
    uint32_t releases;
} kit_provider;

static gf_status GF_CALL provider_row_count(void* context, uint64_t* count) {
    *count = (*(kit_provider*)context).rows;
    return GF_STATUS_OK;
}

static gf_status GF_CALL provider_read_value(void* context, uint64_t row, uint64_t column, gf_value* value) {
    const kit_provider* provider = (const kit_provider*)context;
    if (row >= (*provider).rows) {
        return GF_STATUS_NOT_FOUND;
    }
    if (column == 0u) {
        (*value).kind = GF_VALUE_KIND_TEXT;
        (*value).text = gf_kit_text((*provider).names[row]);
    } else {
        (*value).kind = GF_VALUE_KIND_NUMBER;
        (*value).number = (*provider).amounts[row];
    }
    return GF_STATUS_OK;
}

static gf_status GF_CALL provider_read_stable_id(void* context, uint64_t row, gf_utf8_view* stable_id) {
    const kit_provider* provider = (const kit_provider*)context;
    if (row >= (*provider).rows) {
        return GF_STATUS_NOT_FOUND;
    }
    *stable_id = gf_kit_text((*provider).ids[row]);
    return GF_STATUS_OK;
}

static gf_status GF_CALL provider_write_value(void* context, uint64_t row, uint64_t column, const gf_value* value,
                                              uint32_t* accepted, gf_utf8_view* reason) {
    kit_provider* provider = (kit_provider*)context;
    if (column != 1u || (*value).kind != GF_VALUE_KIND_NUMBER || (*value).number < 0.0) {
        *accepted = 0u;
        *reason = gf_kit_text("amounts are not negative");
        return GF_STATUS_OK;
    }
    (*provider).amounts[row] = (*value).number;
    ++(*provider).writes;
    *accepted = 1u;
    return GF_STATUS_OK;
}

static void GF_CALL provider_release(void* context) {
    ++(*(kit_provider*)context).releases;
}

/* Whether the shown cell (row, 0) at generation is the name expected. */
static int shown_name_is(gf_kit* kit, gf_object source, uint64_t generation, uint64_t row, const char* expected) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    char text[64];
    uint64_t required = 0u;
    if ((*kit).bindings.read_cell(source, generation, row, 0u, &value, text, sizeof(text), &required) !=
        GF_STATUS_OK) {
        return 0;
    }
    return value.kind == GF_VALUE_KIND_TEXT && gf_kit_view_equals(value.text, expected);
}

static void kit_binding_provider(gf_kit* kit, gf_object source) {
    kit_provider provider;
    memset(&provider, 0, sizeof(provider));
    provider.rows = 3u;
    provider.names[0] = "Oslo";
    provider.names[1] = "Lima";
    provider.names[2] = "Bern";
    provider.amounts[0] = 2.0;
    provider.amounts[1] = 9.0;
    provider.amounts[2] = 5.0;
    provider.ids[0] = "city.oslo";
    provider.ids[1] = "city.lima";
    provider.ids[2] = "city.bern";
    provider.names[3] = "Rome";
    provider.amounts[3] = 7.0;
    provider.ids[3] = "city.rome";

    gf_binding_column columns[2];
    memset(columns, 0, sizeof(columns));
    columns[0].struct_size = (uint32_t)sizeof(gf_binding_column);
    columns[0].kind = GF_VALUE_KIND_TEXT;
    columns[0].name = gf_kit_text("name");
    columns[0].read_only = 1u;
    columns[1].struct_size = (uint32_t)sizeof(gf_binding_column);
    columns[1].kind = GF_VALUE_KIND_NUMBER;
    columns[1].name = gf_kit_text("amount");
    gf_binding_provider_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.struct_size = (uint32_t)sizeof(callbacks);
    callbacks.context = &provider;
    callbacks.row_count = &provider_row_count;
    callbacks.read_value = &provider_read_value;
    callbacks.read_stable_id = &provider_read_stable_id;
    callbacks.write_value = &provider_write_value;
    callbacks.release = &provider_release;

    uint64_t id = 0u;
    callbacks.flags = GF_BINDING_PROVIDER_SORTS;
    gf_kit_status(kit, (*kit).bindings.set_provider(source, columns, 2u, &callbacks, &id), GF_STATUS_INVALID_ARGUMENT,
                  "a flag needs its callback");
    callbacks.flags = 0u;
    columns[1].name = gf_kit_text("Name");
    gf_kit_status(kit, (*kit).bindings.set_provider(source, columns, 2u, &callbacks, &id), GF_STATUS_INVALID_ARGUMENT,
                  "column names are distinct after folding");
    columns[1].name = gf_kit_text("amount");
    gf_kit_status(kit, (*kit).bindings.set_provider(source, columns, 2u, &callbacks, &id), GF_STATUS_OK,
                  "bind a C provider");
    gf_kit_check(kit, id != 0u, "the provider has an id");

    uint64_t generation = 0u;
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).bindings.generation(source, &generation, &count), GF_STATUS_OK, "read the generation");
    gf_kit_check(kit, count == 3u && shown_name_is(kit, source, generation, 1u, "Lima"),
                 "the source reads the provider's rows in place");
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    char text[64];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).bindings.read_cell(source, generation, 3u, 0u, &value, text, sizeof(text), &required),
                  GF_STATUS_NOT_FOUND, "a row past the source's is not found");

    gf_value amount;
    memset(&amount, 0, sizeof(amount));
    amount.struct_size = (uint32_t)sizeof(amount);
    amount.kind = GF_VALUE_KIND_NUMBER;
    amount.number = 4.0;
    uint32_t accepted = 9u;
    gf_kit_status(kit, (*kit).bindings.set_current_field(source, gf_kit_text("amount"), &amount, &accepted),
                  GF_STATUS_OK, "write the current amount");
    gf_kit_check(kit, accepted == 1u && provider.writes == 1u && provider.amounts[0] == 4.0,
                 "the write reaches the provider's data");
    amount.number = -1.0;
    gf_kit_status(kit, (*kit).bindings.set_current_field(source, gf_kit_text("amount"), &amount, &accepted),
                  GF_STATUS_OK, "write a refused amount");
    gf_kit_check(kit, accepted == 0u && provider.amounts[0] == 4.0, "the provider refuses it");
    const gf_value renamed = text_value("Paris");
    gf_kit_status(kit, (*kit).bindings.set_current_field(source, gf_kit_text("name"), &renamed, &accepted),
                  GF_STATUS_OK, "write a read-only column");
    gf_kit_check(kit, accepted == 0u, "a read-only column is not written");

    gf_kit_status(kit, (*kit).bindings.read_cell(source, generation, 0u, 0u, &value, text, sizeof(text), &required),
                  GF_STATUS_STALE_HANDLE, "a read at a stale generation is refused");

    gf_binding_sort_key key;
    memset(&key, 0, sizeof(key));
    key.struct_size = (uint32_t)sizeof(key);
    key.direction = GF_BINDING_SORT_DESCENDING;
    key.column = gf_kit_text("missing");
    gf_kit_status(kit, (*kit).bindings.set_sort(source, &key, 1u), GF_STATUS_NOT_FOUND,
                  "a sort on an unknown column is not found");
    key.column = gf_kit_text("AMOUNT");
    gf_kit_status(kit, (*kit).bindings.set_sort(source, &key, 1u), GF_STATUS_OK, "sort by amount, descending");
    gf_kit_status(kit, (*kit).bindings.generation(source, &generation, &count), GF_STATUS_OK, "read after the sort");
    gf_kit_check(kit, shown_name_is(kit, source, generation, 0u, "Lima") &&
                      shown_name_is(kit, source, generation, 2u, "Oslo"),
                 "the source's own index sorts the provider's rows");

    gf_value floor;
    memset(&floor, 0, sizeof(floor));
    floor.struct_size = (uint32_t)sizeof(floor);
    floor.kind = GF_VALUE_KIND_NUMBER;
    floor.number = 4.5;
    gf_binding_predicate predicate;
    memset(&predicate, 0, sizeof(predicate));
    predicate.struct_size = (uint32_t)sizeof(predicate);
    predicate.op = GF_BINDING_PREDICATE_GREATER;
    predicate.column = gf_kit_text("amount");
    predicate.operand = &floor;
    gf_kit_status(kit, (*kit).bindings.set_filter(source, &predicate, 1u), GF_STATUS_OK, "filter amounts over 4.5");
    gf_kit_status(kit, (*kit).bindings.generation(source, &generation, &count), GF_STATUS_OK, "read after the filter");
    gf_kit_check(kit, count == 2u && shown_name_is(kit, source, generation, 1u, "Bern"), "the filter keeps two rows");

    provider.rows = 4u;
    gf_kit_status(kit, (*kit).bindings.notify(id, GF_BINDING_NOTIFY_INSERTED, 3u, 1u, 0u), GF_STATUS_OK,
                  "report an inserted row");
    gf_kit_status(kit, (*kit).bindings.generation(source, &generation, &count), GF_STATUS_OK, "read after the insert");
    gf_kit_check(kit, count == 3u && shown_name_is(kit, source, generation, 1u, "Rome"),
                 "the inserted row takes its place in the view");
    gf_kit_status(kit, (*kit).bindings.notify(id, 9u, 0u, 0u, 0u), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown notification kind is refused");
    gf_kit_status(kit, (*kit).bindings.notify(id + 1000u, GF_BINDING_NOTIFY_RESET, 0u, 0u, 0u), GF_STATUS_REMOVED,
                  "an id no source holds is removed");

    gf_kit_status(kit, (*kit).bindings.set_provider(source, NULL, 0u, NULL, &id), GF_STATUS_OK,
                  "bind an empty table again");
    gf_kit_check(kit, provider.releases == 1u && id == 0u, "the C provider is released once");
    gf_kit_status(kit, (*kit).bindings.set_sort(source, NULL, 0u), GF_STATUS_OK, "clear the sort");
    gf_kit_status(kit, (*kit).bindings.set_filter(source, NULL, 0u), GF_STATUS_OK, "clear the filter");
}

/* N31 batch 2: a provider that adds rows at its end and refuses a negative
 * amount; add_records over it is all or none. */
typedef struct kit_batch_provider {
    uint64_t rows;
    double amounts[8];
    char ids[8][16];
} kit_batch_provider;

static gf_status GF_CALL batch_row_count(void* context, uint64_t* count) {
    *count = (*(kit_batch_provider*)context).rows;
    return GF_STATUS_OK;
}

static gf_status GF_CALL batch_read_value(void* context, uint64_t row, uint64_t column, gf_value* value) {
    const kit_batch_provider* provider = (const kit_batch_provider*)context;
    if (row >= (*provider).rows || column != 0u) {
        return GF_STATUS_NOT_FOUND;
    }
    (*value).kind = GF_VALUE_KIND_NUMBER;
    (*value).number = (*provider).amounts[row];
    return GF_STATUS_OK;
}

static gf_status GF_CALL batch_read_stable_id(void* context, uint64_t row, gf_utf8_view* stable_id) {
    const kit_batch_provider* provider = (const kit_batch_provider*)context;
    if (row >= (*provider).rows) {
        return GF_STATUS_NOT_FOUND;
    }
    *stable_id = gf_kit_text((*provider).ids[row]);
    return GF_STATUS_OK;
}

static gf_status GF_CALL batch_write_value(void* context, uint64_t row, uint64_t column, const gf_value* value,
                                           uint32_t* accepted, gf_utf8_view* reason) {
    (void)context;
    (void)row;
    (void)column;
    (void)value;
    *accepted = 0u;
    *reason = gf_kit_text("read only");
    return GF_STATUS_OK;
}

static gf_status GF_CALL batch_insert_row(void* context, uint64_t row, gf_utf8_view stable_id, const gf_value* values,
                                          uint64_t value_count, uint32_t* accepted) {
    kit_batch_provider* provider = (kit_batch_provider*)context;
    *accepted = 0u;
    if (row != (*provider).rows || row >= 8u || value_count < 1u || stable_id.size >= 16u ||
        values[0].kind != GF_VALUE_KIND_NUMBER || values[0].number < 0.0) {
        return GF_STATUS_OK;
    }
    /* The id is lent for the call: copied. */
    memcpy((*provider).ids[row], stable_id.data, (size_t)stable_id.size);
    (*provider).ids[row][stable_id.size] = '\0';
    (*provider).amounts[row] = values[0].number;
    ++(*provider).rows;
    *accepted = 1u;
    return GF_STATUS_OK;
}

static gf_status GF_CALL batch_remove_rows(void* context, uint64_t row, uint64_t count, uint32_t* accepted) {
    kit_batch_provider* provider = (kit_batch_provider*)context;
    *accepted = 0u;
    if (row + count != (*provider).rows) {
        return GF_STATUS_OK;
    }
    (*provider).rows = row;
    *accepted = 1u;
    return GF_STATUS_OK;
}

static void GF_CALL batch_release(void* context) {
    (void)context;
}

static gf_binding_record amount_record(const char* stable_id, gf_binding_field* field, const gf_value* amount) {
    (*field).name = gf_kit_text("amount");
    (*field).value = amount;
    gf_binding_record record;
    memset(&record, 0, sizeof(record));
    record.struct_size = (uint32_t)sizeof(record);
    record.editable = 1u;
    record.stable_id = gf_kit_text(stable_id);
    record.fields = field;
    record.field_count = 1u;
    return record;
}

static void kit_binding_provider_batches(gf_kit* kit, gf_object source) {
    kit_batch_provider provider;
    memset(&provider, 0, sizeof(provider));
    gf_binding_column column;
    memset(&column, 0, sizeof(column));
    column.struct_size = (uint32_t)sizeof(column);
    column.kind = GF_VALUE_KIND_NUMBER;
    column.name = gf_kit_text("amount");
    gf_binding_provider_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.struct_size = (uint32_t)sizeof(callbacks);
    callbacks.flags = GF_BINDING_PROVIDER_INSERTS | GF_BINDING_PROVIDER_REMOVES;
    callbacks.context = &provider;
    callbacks.row_count = &batch_row_count;
    callbacks.read_value = &batch_read_value;
    callbacks.read_stable_id = &batch_read_stable_id;
    callbacks.write_value = &batch_write_value;
    callbacks.insert_row = &batch_insert_row;
    callbacks.remove_rows = &batch_remove_rows;
    callbacks.release = &batch_release;
    uint64_t id = 0u;
    gf_kit_status(kit, (*kit).bindings.set_provider(source, &column, 1u, &callbacks, &id), GF_STATUS_OK,
                  "bind a provider that inserts and removes");

    gf_value amounts[3];
    memset(amounts, 0, sizeof(amounts));
    for (size_t index = 0u; index < 3u; ++index) {
        amounts[index].struct_size = (uint32_t)sizeof(gf_value);
        amounts[index].kind = GF_VALUE_KIND_NUMBER;
        amounts[index].number = (double)index + 1.0;
    }
    amounts[2].number = -1.0;
    gf_binding_field fields[3];
    gf_binding_record records[3];
    records[0] = amount_record("batch.a", &fields[0], &amounts[0]);
    records[1] = amount_record("batch.b", &fields[1], &amounts[1]);
    records[2] = amount_record("batch.c", &fields[2], &amounts[2]);
    uint64_t first = 0u;
    gf_kit_status(kit, (*kit).bindings.add_records(source, records, 3u, &first), GF_STATUS_INVALID_ARGUMENT,
                  "a record the provider refuses fails the batch");
    uint64_t generation = 0u;
    uint64_t count = 9u;
    gf_kit_status(kit, (*kit).bindings.generation(source, &generation, &count), GF_STATUS_OK, "read after the refusal");
    gf_kit_check(kit, provider.rows == 0u && count == 0u, "a refused batch adds no record");
    gf_kit_status(kit, (*kit).bindings.add_records(source, records, 2u, &first), GF_STATUS_OK, "add an accepted batch");
    gf_kit_status(kit, (*kit).bindings.generation(source, &generation, &count), GF_STATUS_OK, "read after the batch");
    gf_kit_check(kit, provider.rows == 2u && count == 2u && first == 0u, "an accepted batch adds every record");

    callbacks.flags = GF_BINDING_PROVIDER_INSERTS;
    callbacks.remove_rows = NULL;
    gf_kit_status(kit, (*kit).bindings.set_provider(source, &column, 1u, &callbacks, &id), GF_STATUS_OK,
                  "bind a provider that only inserts");
    records[0] = amount_record("batch.d", &fields[0], &amounts[0]);
    records[1] = amount_record("batch.e", &fields[1], &amounts[1]);
    gf_kit_status(kit, (*kit).bindings.add_records(source, records, 2u, &first), GF_STATUS_INVALID_STATE,
                  "several records need a provider that can remove them again");
    gf_kit_check(kit, provider.rows == 2u, "the refused batch changed nothing");
    gf_kit_status(kit, (*kit).bindings.add_records(source, records, 1u, &first), GF_STATUS_OK,
                  "one record needs no undo");
    gf_kit_check(kit, provider.rows == 3u, "the one record is added");
    gf_kit_status(kit, (*kit).bindings.set_provider(source, NULL, 0u, NULL, &id), GF_STATUS_OK,
                  "bind an empty table after the batches");
}

void gf_kit_bindings(gf_kit* kit) {
    const gf_object root = gf_kit_create(kit, "panel", "kit.bindings.root");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "make the bindings window");
    gf_create_arguments arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.struct_size = (uint32_t)sizeof(arguments);
    arguments.owner = window;
    gf_object source = 0u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("binding_source"), gf_kit_text("kit.bindings.source"),
                                                  &arguments, &source),
                  GF_STATUS_OK, "make a binding source");
    const gf_object box = gf_kit_create(kit, "text_box", "kit.bindings.box");

    uint64_t index = 9u;
    gf_kit_status(kit, add_titled(kit, source, "first", "First", &index), GF_STATUS_OK, "add a record");
    gf_kit_check(kit, index == 0u, "the first record is at 0");
    gf_kit_status(kit, add_titled(kit, source, "second", "Second", &index), GF_STATUS_OK, "add another");
    gf_kit_check(kit, index == 1u, "the second record is at 1");
    gf_kit_status(kit, add_titled(kit, source, "first", "Again", &index), GF_STATUS_INVALID_ARGUMENT,
                  "a stable id is not repeated");
    int64_t position = -2;
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).bindings.position(source, &position, &count), GF_STATUS_OK, "read the position");
    gf_kit_check(kit, position == 0 && count == 2u, "the source is at its first record");

    gf_binding_arguments binding_arguments;
    memset(&binding_arguments, 0, sizeof(binding_arguments));
    binding_arguments.struct_size = (uint32_t)sizeof(binding_arguments);
    binding_arguments.source_update_mode = GF_SOURCE_UPDATE_ON_VALIDATION;
    binding_arguments.control_update_mode = GF_CONTROL_UPDATE_ON_PROPERTY_CHANGED;
    binding_arguments.target = box;
    binding_arguments.source = box;
    binding_arguments.property = gf_kit_text("Text");
    binding_arguments.data_member = gf_kit_text("title");
    gf_object binding = 0u;
    gf_kit_status(kit, (*kit).bindings.bind(&binding_arguments, &binding), GF_STATUS_WRONG_HANDLE_KIND,
                  "a binding's source is a binding source");
    binding_arguments.source = source;
    binding_arguments.property = gf_kit_text("no_such_property");
    gf_kit_status(kit, (*kit).bindings.bind(&binding_arguments, &binding), GF_STATUS_INVALID_ARGUMENT,
                  "a property that is not bindable is refused");
    binding_arguments.property = gf_kit_text("Text");
    binding_arguments.source_update_mode = 3u;
    gf_kit_status(kit, (*kit).bindings.bind(&binding_arguments, &binding), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown update mode is refused");
    binding_arguments.source_update_mode = GF_SOURCE_UPDATE_ON_VALIDATION;
    gf_kit_check(kit, binding == 0u, "a refused bind leaves its output");
    gf_kit_status(kit, (*kit).bindings.bind(&binding_arguments, &binding), GF_STATUS_OK, "bind the text");

    uint32_t updated = 9u;
    gf_kit_status(kit, (*kit).bindings.read_value(binding, &updated), GF_STATUS_OK, "read the source");
    gf_kit_check(kit, updated == 1u && text_is(kit, box, "First"), "the control shows the current field");
    uint32_t moved = 9u;
    gf_kit_status(kit, (*kit).bindings.set_position(source, 1, &moved), GF_STATUS_OK, "move to the second record");
    gf_kit_check(kit, moved == 1u, "the position moved");
    gf_kit_status(kit, (*kit).bindings.read_value(binding, &updated), GF_STATUS_OK, "read the source again");
    gf_kit_check(kit, text_is(kit, box, "Second"), "the control follows the position");
    gf_kit_status(kit, (*kit).bindings.set_position(source, 7, &moved), GF_STATUS_INVALID_ARGUMENT,
                  "a position past the records is refused");

    gf_kit_status(kit, gf_kit_set_text(kit, box, "text", "Edited"), GF_STATUS_OK, "edit the control");
    gf_kit_status(kit, (*kit).bindings.write_value(binding, &updated), GF_STATUS_OK, "write the source");
    gf_kit_check(kit, updated == 1u && current_title_is(kit, source, "Edited"), "the source holds the edit");
    const gf_value direct = text_value("Direct");
    uint32_t accepted = 9u;
    gf_kit_status(kit, (*kit).bindings.set_current_field(source, gf_kit_text("title"), &direct, &accepted),
                  GF_STATUS_OK, "set the current field");
    gf_kit_check(kit, accepted == 1u && current_title_is(kit, source, "Direct"), "the field holds the value");
    gf_value missing;
    memset(&missing, 0, sizeof(missing));
    missing.struct_size = (uint32_t)sizeof(missing);
    char text[16];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).bindings.current_field(source, gf_kit_text("no_field"), &missing, text, sizeof(text),
                                                     &required),
                  GF_STATUS_NOT_FOUND, "a missing field is not found");

    const gf_value third_title = text_value("Third");
    gf_binding_field fields[2];
    fields[0].name = gf_kit_text("title");
    fields[0].value = &third_title;
    fields[1].name = gf_kit_text("Title");
    fields[1].value = NULL;
    gf_binding_record third;
    memset(&third, 0, sizeof(third));
    third.struct_size = (uint32_t)sizeof(third);
    third.editable = 1u;
    third.stable_id = gf_kit_text("third");
    third.fields = fields;
    third.field_count = 2u;
    gf_kit_status(kit, (*kit).bindings.insert_record(source, 0u, &third), GF_STATUS_INVALID_ARGUMENT,
                  "a record's field names are distinct");
    third.field_count = 1u;
    gf_kit_status(kit, (*kit).bindings.insert_record(source, 9u, &third), GF_STATUS_INVALID_ARGUMENT,
                  "an insertion past the records is refused");
    gf_kit_status(kit, (*kit).bindings.insert_record(source, 0u, &third), GF_STATUS_OK, "insert a record");
    gf_kit_status(kit, (*kit).bindings.position(source, &position, &count), GF_STATUS_OK, "read the count");
    gf_kit_check(kit, count == 3u, "three records");
    uint32_t removed = 9u;
    gf_kit_status(kit, (*kit).bindings.remove_record(source, 5u, &removed), GF_STATUS_OK, "remove past the end");
    gf_kit_check(kit, removed == 0u, "nothing is removed past the end");
    gf_kit_status(kit, (*kit).bindings.remove_record(source, 0u, &removed), GF_STATUS_OK, "remove a record");
    gf_kit_check(kit, removed == 1u, "the record is removed");

    /* N31: records in bulk, validated whole, so a repeated id adds none. */
    const char* bulk_ids[3] = {"fourth", "fifth", "sixth"};
    const gf_value bulk_title = text_value("Bulk");
    gf_binding_field bulk_field;
    bulk_field.name = gf_kit_text("title");
    bulk_field.value = &bulk_title;
    gf_binding_record bulk[3];
    memset(bulk, 0, sizeof(bulk));
    for (uint32_t index = 0u; index < 3u; ++index) {
        bulk[index].struct_size = (uint32_t)sizeof(gf_binding_record);
        bulk[index].editable = 1u;
        bulk[index].stable_id = gf_kit_text(bulk_ids[index]);
        bulk[index].fields = &bulk_field;
        bulk[index].field_count = 1u;
    }
    uint64_t first = 99u;
    gf_kit_status(kit, (*kit).bindings.add_records(source, bulk, 3u, &first), GF_STATUS_OK, "add records in bulk");
    gf_kit_status(kit, (*kit).bindings.position(source, &position, &count), GF_STATUS_OK, "read the bulk count");
    gf_kit_check(kit, first == 2u && count == 5u, "the bulk records follow the two");
    bulk[0].stable_id = gf_kit_text("seventh");
    bulk[1].stable_id = gf_kit_text("fifth");
    gf_kit_status(kit, (*kit).bindings.add_records(source, bulk, 2u, &first), GF_STATUS_INVALID_ARGUMENT,
                  "a stable id already present refuses the batch");
    bulk[1].stable_id = gf_kit_text("seventh");
    gf_kit_status(kit, (*kit).bindings.add_records(source, bulk, 2u, &first), GF_STATUS_INVALID_ARGUMENT,
                  "a stable id repeated in the batch refuses it");
    gf_kit_status(kit, (*kit).bindings.position(source, &position, &count), GF_STATUS_OK, "read the count again");
    gf_kit_check(kit, count == 5u, "a refused batch adds none");
    gf_kit_status(kit, (*kit).bindings.add_records(source, bulk, 0u, &first), GF_STATUS_OK, "an empty batch");
    gf_kit_check(kit, first == 5u, "an empty batch writes first");

    gf_kit_status(kit, (*kit).bindings.unbind(binding), GF_STATUS_OK, "unbind");
    gf_kit_status(kit, (*kit).bindings.unbind(binding), GF_STATUS_DISPOSED, "unbinding disposes the binding");
    gf_kit_status(kit, (*kit).bindings.read_value(box, &updated), GF_STATUS_WRONG_HANDLE_KIND,
                  "a control is not a binding");
    gf_kit_status(kit, (*kit).bindings.clear_records(source), GF_STATUS_OK, "clear the records");
    gf_kit_status(kit, (*kit).bindings.position(source, &position, &count), GF_STATUS_OK, "read the empty source");
    gf_kit_check(kit, count == 0u && position == -1, "an empty source has no position");

    kit_binding_provider(kit, source);
    kit_binding_provider_batches(kit, source);

    gf_kit_status(kit, (*kit).objects.release(binding), GF_STATUS_OK, "release the binding");
    gf_kit_status(kit, (*kit).objects.release(box), GF_STATUS_OK, "release the text box");
    gf_kit_status(kit, (*kit).objects.release(source), GF_STATUS_OK, "release the source");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the bindings window");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the bindings root");
}

/* What the running root's ready callback saw. */
typedef struct bindings_run {
    gf_kit* kit;
    gf_object child;
    gf_object source;
    gf_object child_source;
    gf_status made_on_root;
    gf_status made_on_child;
    gf_status added;
    int64_t position;
    uint64_t count;
    gf_status close;
} bindings_run;

static void GF_CALL on_bindings_ready(void* context, gf_event* event) {
    bindings_run* run = (bindings_run*)context;
    gf_kit* kit = (*run).kit;
    gf_create_arguments arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.struct_size = (uint32_t)sizeof(arguments);
    arguments.owner = (*event).sender;
    (*run).made_on_root = (*kit).objects.create_with(gf_kit_text("binding_source"), gf_kit_text("kit.run.source"),
                                                     &arguments, &(*run).source);
    arguments.owner = (*run).child;
    (*run).made_on_child = (*kit).objects.create_with(gf_kit_text("BindingSource"),
                                                      gf_kit_text("kit.run.child-source"), &arguments,
                                                      &(*run).child_source);
    uint64_t index = 0u;
    (*run).added = add_titled(kit, (*run).source, "only", "Only", &index);
    (void)(*kit).bindings.position((*run).source, &(*run).position, &(*run).count);
    (*run).close = (*kit).windows.request_close((*event).sender);
}

/* A binding source made over windows.run's root (since 1.1): a C program
 * that runs a root, not a created window, binds data from its ready
 * callback. */
void gf_kit_bindings_running(gf_kit* kit) {
    bindings_run run;
    memset(&run, 0, sizeof(run));
    run.kit = kit;
    run.made_on_root = -1;
    run.made_on_child = -1;
    run.added = -1;
    run.position = -2;
    run.close = -1;
    const gf_object root = gf_kit_create(kit, "panel", "kit.run.bindings");
    run.child = gf_kit_create(kit, "label", "kit.run.bindings.label");
    gf_kit_status(kit, (*kit).objects.insert_child(root, run.child, 0u), GF_STATUS_OK, "build the run's tree");
    gf_create_arguments arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.struct_size = (uint32_t)sizeof(arguments);
    arguments.owner = root;
    gf_object early = 9u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("binding_source"), gf_kit_text("kit.run.early"),
                                                  &arguments, &early),
                  GF_STATUS_WRONG_HANDLE_KIND, "a root in no window is not a binding source's owner");
    gf_kit_check(kit, early == 9u, "the refusal leaves its output");

    const gf_callback ready = gf_kit_callback(&on_bindings_ready, NULL, &run);
    gf_run_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.title = gf_kit_text("GUI.Forms C ABI bindings on a run");
    options.ready = &ready;
    gf_kit_status(kit, (*kit).windows.run(root, &options), GF_STATUS_OK, "run the root");
    gf_kit_status(kit, run.made_on_root, GF_STATUS_OK, "a binding source over the running root");
    gf_kit_status(kit, run.made_on_child, GF_STATUS_OK, "a binding source over a control of the running tree");
    gf_kit_status(kit, run.added, GF_STATUS_OK, "the source takes a record during the run");
    gf_kit_check(kit, run.position == 0 && run.count == 1u, "the source is at its one record");
    gf_kit_status(kit, run.close, GF_STATUS_OK, "close the run");
    gf_kit_check(kit, gf_kit_state(kit, run.source) == GF_OBJECT_STATE_ALIVE,
                 "the source outlives the run's window, unbound");
    gf_kit_status(kit, (*kit).objects.release(run.child_source), GF_STATUS_OK, "release the child's source");
    gf_kit_status(kit, (*kit).objects.release(run.source), GF_STATUS_OK, "release the root's source");
    gf_kit_status(kit, (*kit).objects.release(run.child), GF_STATUS_OK, "release the run's label");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the run's root");
}
