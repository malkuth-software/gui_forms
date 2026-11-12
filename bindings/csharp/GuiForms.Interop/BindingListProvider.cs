// A .NET list as a binding source's data provider (bindings.set_provider,
// since 1.2, audit N31): the source reads the list in place instead of
// holding copies. The columns are the item type's PropertyDescriptors (an
// ITypedList's own when the list is one); an edit writes straight to the
// item's property; an IBindingList's ListChanged becomes the source's
// notifications, from whatever thread raised it; sorting is delegated to an
// IBindingList that sorts (IBindingListView for several keys), and the
// source keeps its own index otherwise. Filtering is delegated to an
// IBindingListView that filters: the structured predicates become a filter
// string in the DataView RowFilter grammar (a DataView is the framework's
// filtering list); a predicate that grammar cannot state, or a list that
// refuses the string, leaves the filter to the source's own index. A
// DataView compares text by its table's CaseSensitive and Locale, which
// may differ from the source's code-point case folding for some scripts.
//
// Lifetime: bind() hands the library a GCHandle to this provider and a
// native text buffer; the library calls release exactly once when the
// source lets the provider go (another set_provider, or its disposal), and
// release frees both and unsubscribes from ListChanged. Until then the
// provider and the list stay reachable through the handle.
//
// Failures: the provider is untrusted data to the source, so an exception
// in a read or write never crosses into the library and is not raised to a
// caller either: the thunk returns a refusal and the source shows a cell
// error (last_failure keeps the exception for diagnostics). Callbacks run on
// the source's UI thread only.
using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace GuiForms.Interop;

public sealed unsafe class BindingListProvider
{
    private readonly IList list;
    private readonly Type item_type;
    private readonly PropertyDescriptor[] properties;
    private readonly gf_value_kind[] kinds;
    private readonly PropertyDescriptor? id_property;
    private readonly IBindingList? binding_list;
    // Stable ids for reference-type items without an id property: one per
    // item for as long as the item lives, so an id follows its item through
    // inserts, removes and sorts.
    private readonly ConditionalWeakTable<object, string> identities = new ConditionalWeakTable<object, string>();
    private ulong next_identity;
    // The text a callback lends the library, valid until the next callback
    // (the lending rule). Owned here, freed by release.
    private byte* text;
    private nuint text_capacity;
    private GCHandle self;
    private ulong provider_id;
    private bool released;

    /// <summary>The last exception a read or write of the list threw, for diagnostics.</summary>
    public Exception? last_failure { get; private set; }

    /// <summary>The id bindings.notify reaches this provider by; zero once released.</summary>
    public ulong id => released ? 0u : provider_id;

    private BindingListProvider(IList list, Type item_type, PropertyDescriptor[] properties, string? id_property_name)
    {
        this.list = list;
        this.item_type = item_type;
        this.properties = properties;
        kinds = new gf_value_kind[properties.Length];
        for (int index = 0; index < properties.Length; ++index)
        {
            kinds[index] = kind_of(properties[index].PropertyType);
        }
        if (id_property_name != null)
        {
            foreach (PropertyDescriptor property in properties)
            {
                if (property.Name == id_property_name)
                {
                    id_property = property;
                }
            }
            if (id_property == null)
            {
                throw new ArgumentException("the items have no property " + id_property_name, nameof(id_property_name));
            }
        }
        binding_list = list as IBindingList;
    }

    /// <summary>
    /// Binds list to source as its provider. id_property names the item
    /// property whose text is each row's stable id; without one, an item of
    /// a reference type gets an id of its own ("item.N") and an item of a
    /// value type its index ("row.N", which does not follow the item).
    /// </summary>
    public static BindingListProvider bind(gf_object source, IList list, string? id_property = null)
    {
        ArgumentNullException.ThrowIfNull(list);
        Type item_type = item_type_of(list);
        PropertyDescriptor[] properties = properties_of(list, item_type);
        BindingListProvider provider = new BindingListProvider(list, item_type, properties, id_property);
        provider.attach(source);
        return provider;
    }

    /// <summary>Reports that anything may have changed (for a list that raises no ListChanged).</summary>
    public void notify_reset() => notify(Constants.GF_BINDING_NOTIFY_RESET, 0u, 0u, 0u);

    /// <summary>Reports count items inserted at index.</summary>
    public void notify_inserted(int index, int count) =>
        notify(Constants.GF_BINDING_NOTIFY_INSERTED, checked((ulong)index), checked((ulong)count), 0u);

    /// <summary>Reports count items removed at index.</summary>
    public void notify_removed(int index, int count) =>
        notify(Constants.GF_BINDING_NOTIFY_REMOVED, checked((ulong)index), checked((ulong)count), 0u);

    /// <summary>Reports count items at index moved so the first now stands at target.</summary>
    public void notify_moved(int index, int count, int target) =>
        notify(Constants.GF_BINDING_NOTIFY_MOVED, checked((ulong)index), checked((ulong)count), checked((ulong)target));

    /// <summary>Reports a change to the item at index (every property).</summary>
    public void notify_changed(int index) =>
        notify(Constants.GF_BINDING_NOTIFY_CHANGED, checked((ulong)index), 0u, Constants.GF_BINDING_WHOLE_ROW);

    // ---------------------------------------------------------------------
    // Binding.

    private static Type item_type_of(IList list)
    {
        foreach (Type implemented in list.GetType().GetInterfaces())
        {
            if (implemented.IsGenericType && implemented.GetGenericTypeDefinition() == typeof(IList<>))
            {
                return implemented.GetGenericArguments()[0];
            }
        }
        if (list.Count > 0 && list[0] != null)
        {
            return list[0]!.GetType();
        }
        return typeof(object);
    }

    private static PropertyDescriptor[] properties_of(IList list, Type item_type)
    {
        PropertyDescriptorCollection found = list is ITypedList typed ? typed.GetItemProperties(null)
                                                                       : TypeDescriptor.GetProperties(item_type);
        if (found.Count > (int)Constants.GF_BINDING_MAXIMUM_FIELDS)
        {
            throw new ArgumentException("the items have more properties than a source's columns");
        }
        PropertyDescriptor[] properties = new PropertyDescriptor[found.Count];
        found.CopyTo(properties, 0);
        return properties;
    }

    // The kind a property's values have in the source; TEXT (read-only, its
    // ToString) for a type with no gf_value form.
    private static gf_value_kind kind_of(Type type)
    {
        Type plain = Nullable.GetUnderlyingType(type) ?? type;
        if (plain == typeof(bool)) return Constants.GF_VALUE_KIND_BOOLEAN;
        if (plain.IsEnum) return Constants.GF_VALUE_KIND_SIGNED;
        if (plain == typeof(sbyte) || plain == typeof(short) || plain == typeof(int) || plain == typeof(long))
            return Constants.GF_VALUE_KIND_SIGNED;
        if (plain == typeof(byte) || plain == typeof(ushort) || plain == typeof(uint) || plain == typeof(ulong))
            return Constants.GF_VALUE_KIND_UNSIGNED;
        if (plain == typeof(float) || plain == typeof(double) || plain == typeof(decimal))
            return Constants.GF_VALUE_KIND_NUMBER;
        return Constants.GF_VALUE_KIND_TEXT;
    }

    private static bool has_form(Type type)
    {
        Type plain = Nullable.GetUnderlyingType(type) ?? type;
        return plain == typeof(string) || kind_of(plain) != Constants.GF_VALUE_KIND_TEXT;
    }

    private bool sorts =>
        binding_list != null && binding_list.SupportsSorting;

    private bool filters =>
        binding_list is IBindingListView view && view.SupportsFiltering;

    private bool inserts_and_removes =>
        !list.IsReadOnly && !list.IsFixedSize && (item_type.IsValueType || item_type.GetConstructor(Type.EmptyTypes) != null);

    private void attach(gf_object source)
    {
        // Every column name is encoded into one pinned array lent for the call.
        byte[][] names = new byte[properties.Length][];
        int total = 0;
        for (int index = 0; index < properties.Length; ++index)
        {
            names[index] = Encoding.UTF8.GetBytes(properties[index].Name);
            total += names[index].Length;
        }
        byte[] joined = new byte[Math.Max(total, 1)];
        gf_binding_column[] columns = new gf_binding_column[properties.Length];
        int offset = 0;
        for (int index = 0; index < properties.Length; ++index)
        {
            Buffer.BlockCopy(names[index], 0, joined, offset, names[index].Length);
            offset += names[index].Length;
        }
        self = GCHandle.Alloc(this);
        gf_binding_provider_callbacks callbacks = default;
        callbacks.struct_size = (uint)sizeof(gf_binding_provider_callbacks);
        callbacks.flags = (sorts ? Constants.GF_BINDING_PROVIDER_SORTS : 0u) |
                          (filters ? Constants.GF_BINDING_PROVIDER_FILTERS : 0u) |
                          (inserts_and_removes ? Constants.GF_BINDING_PROVIDER_INSERTS | Constants.GF_BINDING_PROVIDER_REMOVES : 0u);
        callbacks.context = (void*)GCHandle.ToIntPtr(self);
        callbacks.row_count = &row_count_thunk;
        callbacks.read_value = &read_value_thunk;
        callbacks.read_stable_id = &read_stable_id_thunk;
        callbacks.write_value = &write_value_thunk;
        if (sorts)
        {
            callbacks.apply_sort = &apply_sort_thunk;
        }
        if (filters)
        {
            callbacks.apply_filter = &apply_filter_thunk;
        }
        if (inserts_and_removes)
        {
            callbacks.insert_row = &insert_row_thunk;
            callbacks.remove_rows = &remove_rows_thunk;
        }
        callbacks.release = &release_thunk;
        gf_status status;
        ulong made = 0;
        gf_bindings_table table = Abi.bindings;
        fixed (byte* name_bytes = joined)
        fixed (gf_binding_column* column_records = columns)
        {
            offset = 0;
            for (int index = 0; index < properties.Length; ++index)
            {
                ref gf_binding_column column = ref columns[index];
                column.struct_size = (uint)sizeof(gf_binding_column);
                column.kind = kinds[index];
                column.name = Utf8.view(name_bytes + offset, names[index].Length);
                column.read_only = properties[index].IsReadOnly || !has_form(properties[index].PropertyType) ? 1u : 0u;
                offset += names[index].Length;
            }
            status = table.set_provider(source, column_records, (ulong)columns.Length, &callbacks, &made);
        }
        if (status != Constants.GF_STATUS_OK)
        {
            // Refused: the library never calls release, so the handle is ours.
            self.Free();
            Abi.check(status);
        }
        provider_id = made;
        if (binding_list != null)
        {
            binding_list.ListChanged += list_changed;
        }
    }

    private void notify(gf_binding_notification_kind kind, ulong row, ulong count, ulong target)
    {
        if (released)
        {
            return;
        }
        gf_status status = Abi.bindings.notify(provider_id, kind, row, count, target);
        if (status == Constants.GF_STATUS_REMOVED)
        {
            return;
        }
        Abi.check(status);
    }

    // ListChanged may come from any thread; notify is safe from any thread.
    private void list_changed(object? sender, ListChangedEventArgs arguments)
    {
        switch (arguments.ListChangedType)
        {
            case ListChangedType.ItemAdded:
                notify(Constants.GF_BINDING_NOTIFY_INSERTED, (ulong)arguments.NewIndex, 1u, 0u);
                break;
            case ListChangedType.ItemDeleted:
                notify(Constants.GF_BINDING_NOTIFY_REMOVED, (ulong)arguments.NewIndex, 1u, 0u);
                break;
            case ListChangedType.ItemMoved:
                notify(Constants.GF_BINDING_NOTIFY_MOVED, (ulong)arguments.OldIndex, 1u, (ulong)arguments.NewIndex);
                break;
            case ListChangedType.ItemChanged:
                notify(Constants.GF_BINDING_NOTIFY_CHANGED, (ulong)arguments.NewIndex, 0u,
                       column_of(arguments.PropertyDescriptor));
                break;
            default:
                notify(Constants.GF_BINDING_NOTIFY_RESET, 0u, 0u, 0u);
                break;
        }
    }

    private ulong column_of(PropertyDescriptor? changed)
    {
        if (changed == null)
        {
            return Constants.GF_BINDING_WHOLE_ROW;
        }
        for (int index = 0; index < properties.Length; ++index)
        {
            if (properties[index].Name == changed.Name)
            {
                return (ulong)index;
            }
        }
        return Constants.GF_BINDING_WHOLE_ROW;
    }

    // ---------------------------------------------------------------------
    // Answers.

    // Copies value's UTF-8 into the lent buffer; the view stays valid until
    // the next callback.
    private gf_utf8_view lend(string value)
    {
        int needed = Encoding.UTF8.GetByteCount(value);
        if ((nuint)needed > text_capacity)
        {
            nuint grown = Math.Max((nuint)needed, Math.Max(text_capacity * 2u, (nuint)256u));
            NativeMemory.Free(text);
            text = null;
            text_capacity = 0;
            text = (byte*)NativeMemory.Alloc(grown);
            text_capacity = grown;
        }
        int written = Encoding.UTF8.GetBytes(value, new Span<byte>(text, (int)text_capacity));
        return Utf8.view(text, written);
    }

    private string stable_id_of(int row)
    {
        object? item = list[row];
        if (id_property != null)
        {
            object? value = item == null ? null : id_property.GetValue(item);
            return value?.ToString() ?? string.Empty;
        }
        if (item == null || item_type.IsValueType)
        {
            return "row." + row.ToString(System.Globalization.CultureInfo.InvariantCulture);
        }
        if (!identities.TryGetValue(item, out string? identity))
        {
            ++next_identity;
            identity = "item." + next_identity.ToString(System.Globalization.CultureInfo.InvariantCulture);
            identities.Add(item, identity);
        }
        return identity;
    }

    private gf_status read(ulong row, ulong column, gf_value* value)
    {
        if (row >= (ulong)list.Count || column >= (ulong)properties.Length)
        {
            return Constants.GF_STATUS_NOT_FOUND;
        }
        object? item = list[(int)row];
        object? held = item == null ? null : properties[(int)column].GetValue(item);
        // A DataRowView answers DBNull for a null field.
        if (held == null || held is DBNull)
        {
            (*value).kind = Constants.GF_VALUE_KIND_NULL;
            return Constants.GF_STATUS_OK;
        }
        gf_value_kind kind = kinds[(int)column];
        (*value).kind = kind;
        if (kind == Constants.GF_VALUE_KIND_BOOLEAN)
        {
            (*value).boolean = (bool)held ? 1u : 0u;
        }
        else if (kind == Constants.GF_VALUE_KIND_SIGNED)
        {
            (*value).signed_integer = Convert.ToInt64(held, System.Globalization.CultureInfo.InvariantCulture);
        }
        else if (kind == Constants.GF_VALUE_KIND_UNSIGNED)
        {
            (*value).unsigned_integer = Convert.ToUInt64(held, System.Globalization.CultureInfo.InvariantCulture);
        }
        else if (kind == Constants.GF_VALUE_KIND_NUMBER)
        {
            (*value).number = Convert.ToDouble(held, System.Globalization.CultureInfo.InvariantCulture);
        }
        else
        {
            (*value).text = lend(held.ToString() ?? string.Empty);
        }
        return Constants.GF_STATUS_OK;
    }

    // value as the property's type, or null for a NULL value.
    private static object? converted(gf_value* value, Type type)
    {
        if ((*value).kind == Constants.GF_VALUE_KIND_NULL)
        {
            return null;
        }
        Type plain = Nullable.GetUnderlyingType(type) ?? type;
        object raw;
        if ((*value).kind == Constants.GF_VALUE_KIND_BOOLEAN) raw = (*value).boolean != 0u;
        else if ((*value).kind == Constants.GF_VALUE_KIND_SIGNED) raw = (*value).signed_integer;
        else if ((*value).kind == Constants.GF_VALUE_KIND_UNSIGNED) raw = (*value).unsigned_integer;
        else if ((*value).kind == Constants.GF_VALUE_KIND_NUMBER) raw = (*value).number;
        else if ((*value).kind == Constants.GF_VALUE_KIND_TEXT)
            raw = Encoding.UTF8.GetString((*value).text.data, checked((int)(*value).text.size));
        else throw new ArgumentException("the value has no form a .NET property takes");
        if (plain.IsEnum)
        {
            return Enum.ToObject(plain, raw);
        }
        return Convert.ChangeType(raw, plain, System.Globalization.CultureInfo.InvariantCulture);
    }

    // Sets column of the item at row; an item of a value type is a copy, so
    // it is stored back.
    private void store(int row, int column, object? value)
    {
        object? item = list[row];
        if (item == null)
        {
            throw new InvalidOperationException("the row has no item");
        }
        properties[column].SetValue(item, value);
        if (item_type.IsValueType)
        {
            list[row] = item;
        }
    }

    private gf_status write(ulong row, ulong column, gf_value* value, uint* accepted, gf_utf8_view* reason)
    {
        *accepted = 0u;
        if (row >= (ulong)list.Count || column >= (ulong)properties.Length)
        {
            *reason = lend("the row or column is past the list's");
            return Constants.GF_STATUS_OK;
        }
        try
        {
            object? incoming = converted(value, properties[(int)column].PropertyType);
            store((int)row, (int)column, incoming);
        }
        catch (Exception exception)
        {
            last_failure = exception;
            *reason = lend(exception.Message);
            return Constants.GF_STATUS_OK;
        }
        *accepted = 1u;
        return Constants.GF_STATUS_OK;
    }

    private gf_status sort(gf_binding_sort_key* keys, ulong count)
    {
        if (binding_list == null)
        {
            return Constants.GF_STATUS_UNSUPPORTED_CAPABILITY;
        }
        if (count == 0u)
        {
            binding_list.RemoveSort();
            return Constants.GF_STATUS_OK;
        }
        ListSortDescription[] descriptions = new ListSortDescription[checked((int)count)];
        for (int index = 0; index < descriptions.Length; ++index)
        {
            gf_binding_sort_key* key = keys + index;
            string name = Encoding.UTF8.GetString((*key).column.data, checked((int)(*key).column.size));
            PropertyDescriptor? property = find_property(name);
            if (property == null)
            {
                return Constants.GF_STATUS_NOT_FOUND;
            }
            ListSortDirection direction = (*key).direction == Constants.GF_BINDING_SORT_DESCENDING
                ? ListSortDirection.Descending : ListSortDirection.Ascending;
            descriptions[index] = new ListSortDescription(property, direction);
        }
        if (descriptions.Length == 1)
        {
            binding_list.ApplySort(descriptions[0].PropertyDescriptor!, descriptions[0].SortDirection);
            return Constants.GF_STATUS_OK;
        }
        if (binding_list is IBindingListView view && view.SupportsAdvancedSorting)
        {
            view.ApplySort(new ListSortDescriptionCollection(descriptions));
            return Constants.GF_STATUS_OK;
        }
        // Several keys and no advanced sorting: the source sorts.
        return Constants.GF_STATUS_UNSUPPORTED_CAPABILITY;
    }

    private gf_status filter(gf_binding_predicate* predicates, ulong count)
    {
        if (binding_list is not IBindingListView view || !view.SupportsFiltering)
        {
            return Constants.GF_STATUS_UNSUPPORTED_CAPABILITY;
        }
        if (count == 0u)
        {
            view.RemoveFilter();
            return Constants.GF_STATUS_OK;
        }
        StringBuilder expression = new StringBuilder();
        for (ulong index = 0u; index < count; ++index)
        {
            gf_binding_predicate* predicate = predicates + index;
            string name = Encoding.UTF8.GetString((*predicate).column.data, checked((int)(*predicate).column.size));
            PropertyDescriptor? property = find_property(name);
            if (property == null)
            {
                return Constants.GF_STATUS_NOT_FOUND;
            }
            string? clause = filter_clause(property.Name, (*predicate).op, (*predicate).operand);
            if (clause == null)
            {
                // Not stated in the grammar: the source filters.
                return Constants.GF_STATUS_UNSUPPORTED_CAPABILITY;
            }
            if (index > 0u)
            {
                expression.Append(" AND ");
            }
            expression.Append(clause);
        }
        // A list with another grammar throws here; the thunk turns that into
        // a refusal and the source filters.
        view.Filter = expression.ToString();
        return Constants.GF_STATUS_OK;
    }

    // One predicate as a DataView RowFilter clause, or null when the grammar
    // cannot state it (an operand the comparison has no literal for).
    private static string? filter_clause(string column, gf_binding_predicate_operator op, gf_value* operand)
    {
        string name = "[" + column.Replace("\\", "\\\\").Replace("]", "\\]") + "]";
        if (op == Constants.GF_BINDING_PREDICATE_IS_NULL)
        {
            return name + " IS NULL";
        }
        if (op == Constants.GF_BINDING_PREDICATE_IS_NOT_NULL)
        {
            return name + " IS NOT NULL";
        }
        if (operand == null || (*operand).kind == Constants.GF_VALUE_KIND_NULL)
        {
            return null;
        }
        if (op == Constants.GF_BINDING_PREDICATE_CONTAINS || op == Constants.GF_BINDING_PREDICATE_STARTS_WITH)
        {
            if ((*operand).kind != Constants.GF_VALUE_KIND_TEXT)
            {
                return null;
            }
            string text = Encoding.UTF8.GetString((*operand).text.data, checked((int)(*operand).text.size));
            string pattern = like_escaped(text);
            return op == Constants.GF_BINDING_PREDICATE_CONTAINS ? name + " LIKE '%" + pattern + "%'"
                                                                 : name + " LIKE '" + pattern + "%'";
        }
        string? literal = filter_literal(operand);
        if (literal == null)
        {
            return null;
        }
        string? comparison = null;
        if (op == Constants.GF_BINDING_PREDICATE_EQUAL) comparison = " = ";
        else if (op == Constants.GF_BINDING_PREDICATE_NOT_EQUAL) comparison = " <> ";
        else if (op == Constants.GF_BINDING_PREDICATE_LESS) comparison = " < ";
        else if (op == Constants.GF_BINDING_PREDICATE_LESS_OR_EQUAL) comparison = " <= ";
        else if (op == Constants.GF_BINDING_PREDICATE_GREATER) comparison = " > ";
        else if (op == Constants.GF_BINDING_PREDICATE_GREATER_OR_EQUAL) comparison = " >= ";
        if (comparison == null)
        {
            return null;
        }
        return name + comparison + literal;
    }

    private static string? filter_literal(gf_value* operand)
    {
        System.Globalization.CultureInfo invariant = System.Globalization.CultureInfo.InvariantCulture;
        gf_value_kind kind = (*operand).kind;
        if (kind == Constants.GF_VALUE_KIND_BOOLEAN) return (*operand).boolean != 0u ? "true" : "false";
        if (kind == Constants.GF_VALUE_KIND_SIGNED) return (*operand).signed_integer.ToString(invariant);
        if (kind == Constants.GF_VALUE_KIND_UNSIGNED) return (*operand).unsigned_integer.ToString(invariant);
        if (kind == Constants.GF_VALUE_KIND_NUMBER)
        {
            double number = (*operand).number;
            if (double.IsNaN(number) || double.IsInfinity(number))
            {
                return null;
            }
            return number.ToString("R", invariant);
        }
        if (kind == Constants.GF_VALUE_KIND_TEXT)
        {
            string text = Encoding.UTF8.GetString((*operand).text.data, checked((int)(*operand).text.size));
            return "'" + text.Replace("'", "''") + "'";
        }
        return null;
    }

    // Text for inside a LIKE pattern: quotes doubled, and the wildcards and
    // brackets taken literally by bracketing them.
    private static string like_escaped(string text)
    {
        StringBuilder escaped = new StringBuilder(text.Length + 8);
        foreach (char character in text)
        {
            if (character == '*' || character == '%' || character == '[' || character == ']')
            {
                escaped.Append('[').Append(character).Append(']');
            }
            else if (character == '\'')
            {
                escaped.Append("''");
            }
            else
            {
                escaped.Append(character);
            }
        }
        return escaped.ToString();
    }

    // A column's descriptor by a name in any case (the source's spelling is
    // its canonical, folded one).
    private PropertyDescriptor? find_property(string name)
    {
        foreach (PropertyDescriptor property in properties)
        {
            if (string.Equals(property.Name, name, StringComparison.OrdinalIgnoreCase))
            {
                return property;
            }
        }
        return null;
    }

    private gf_status insert(ulong row, gf_utf8_view stable_id, gf_value* values, ulong value_count, uint* accepted)
    {
        *accepted = 0u;
        if (row > (ulong)list.Count)
        {
            return Constants.GF_STATUS_OK;
        }
        object item = Activator.CreateInstance(item_type)!;
        int known = (int)Math.Min(value_count, (ulong)properties.Length);
        for (int column = 0; column < known; ++column)
        {
            if (properties[column].IsReadOnly || (values[column]).kind == Constants.GF_VALUE_KIND_NULL)
            {
                continue;
            }
            properties[column].SetValue(item, converted(values + column, properties[column].PropertyType));
        }
        string identity = Encoding.UTF8.GetString(stable_id.data, checked((int)stable_id.size));
        if (id_property != null && !id_property.IsReadOnly)
        {
            id_property.SetValue(item, identity);
        }
        // The source reports the insertion itself: a list that raises
        // ListChanged for it is not heard twice.
        unsubscribe();
        try
        {
            list.Insert((int)row, item);
        }
        finally
        {
            subscribe();
        }
        if (id_property == null && !item_type.IsValueType)
        {
            identities.AddOrUpdate(item, identity);
        }
        *accepted = 1u;
        return Constants.GF_STATUS_OK;
    }

    private gf_status remove(ulong row, ulong count, uint* accepted)
    {
        *accepted = 0u;
        if (row >= (ulong)list.Count || count > (ulong)list.Count - row)
        {
            return Constants.GF_STATUS_OK;
        }
        unsubscribe();
        try
        {
            for (ulong removed = 0; removed < count; ++removed)
            {
                list.RemoveAt((int)row);
            }
        }
        finally
        {
            subscribe();
        }
        *accepted = 1u;
        return Constants.GF_STATUS_OK;
    }

    private void subscribe()
    {
        if (binding_list != null && !released)
        {
            binding_list.ListChanged += list_changed;
        }
    }

    private void unsubscribe()
    {
        if (binding_list != null)
        {
            binding_list.ListChanged -= list_changed;
        }
    }

    private void release()
    {
        released = true;
        unsubscribe();
        NativeMemory.Free(text);
        text = null;
        text_capacity = 0;
        self.Free();
    }

    // ---------------------------------------------------------------------
    // Thunks: the library's entry points. No exception crosses back.

    private static BindingListProvider from(void* context) =>
        (BindingListProvider)GCHandle.FromIntPtr((IntPtr)context).Target!;

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static gf_status row_count_thunk(void* context, ulong* count)
    {
        BindingListProvider provider = from(context);
        try
        {
            *count = (ulong)provider.list.Count;
            return Constants.GF_STATUS_OK;
        }
        catch (Exception exception)
        {
            provider.last_failure = exception;
            return Constants.GF_STATUS_INVALID_STATE;
        }
    }

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static gf_status read_value_thunk(void* context, ulong row, ulong column, gf_value* value)
    {
        BindingListProvider provider = from(context);
        try
        {
            return provider.read(row, column, value);
        }
        catch (Exception exception)
        {
            provider.last_failure = exception;
            return Constants.GF_STATUS_INVALID_STATE;
        }
    }

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static gf_status read_stable_id_thunk(void* context, ulong row, gf_utf8_view* stable_id)
    {
        BindingListProvider provider = from(context);
        try
        {
            if (row >= (ulong)provider.list.Count)
            {
                return Constants.GF_STATUS_NOT_FOUND;
            }
            *stable_id = provider.lend(provider.stable_id_of((int)row));
            return Constants.GF_STATUS_OK;
        }
        catch (Exception exception)
        {
            provider.last_failure = exception;
            return Constants.GF_STATUS_INVALID_STATE;
        }
    }

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static gf_status write_value_thunk(void* context, ulong row, ulong column, gf_value* value, uint* accepted,
                                               gf_utf8_view* reason)
    {
        BindingListProvider provider = from(context);
        try
        {
            return provider.write(row, column, value, accepted, reason);
        }
        catch (Exception exception)
        {
            provider.last_failure = exception;
            *accepted = 0u;
            return Constants.GF_STATUS_INVALID_STATE;
        }
    }

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static gf_status apply_sort_thunk(void* context, gf_binding_sort_key* keys, ulong count)
    {
        BindingListProvider provider = from(context);
        try
        {
            return provider.sort(keys, count);
        }
        catch (Exception exception)
        {
            provider.last_failure = exception;
            return Constants.GF_STATUS_INVALID_STATE;
        }
    }

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static gf_status apply_filter_thunk(void* context, gf_binding_predicate* predicates, ulong count)
    {
        BindingListProvider provider = from(context);
        try
        {
            return provider.filter(predicates, count);
        }
        catch (Exception exception)
        {
            provider.last_failure = exception;
            return Constants.GF_STATUS_INVALID_STATE;
        }
    }

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static gf_status insert_row_thunk(void* context, ulong row, gf_utf8_view stable_id, gf_value* values,
                                              ulong value_count, uint* accepted)
    {
        BindingListProvider provider = from(context);
        try
        {
            return provider.insert(row, stable_id, values, value_count, accepted);
        }
        catch (Exception exception)
        {
            provider.last_failure = exception;
            *accepted = 0u;
            return Constants.GF_STATUS_INVALID_STATE;
        }
    }

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static gf_status remove_rows_thunk(void* context, ulong row, ulong count, uint* accepted)
    {
        BindingListProvider provider = from(context);
        try
        {
            return provider.remove(row, count, accepted);
        }
        catch (Exception exception)
        {
            provider.last_failure = exception;
            *accepted = 0u;
            return Constants.GF_STATUS_INVALID_STATE;
        }
    }

    [DebuggerStepThrough]
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void release_thunk(void* context)
    {
        try
        {
            from(context).release();
        }
        catch (Exception exception)
        {
            CallbackFailure.record(exception);
        }
    }
}
