// The C# interop against the built library: the generated layout against the
// manifest, negotiation, an object handle's life, a refusal as an exception,
// and the callback thunks. A handler that throws is caught by its thunk under
// the library's frames, which it snapshots, and the thread's depth is as it
// was once the call returns (the C kit checks the same balance for C
// callbacks; tests/shadow_frames_tests.cpp for C++ unwinding).
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Text;
using GuiForms.Interop;

// An item a List<T> binds (BindingListProvider): its properties are the
// source's columns.
internal sealed class City
{
    public string Name { get; set; } = string.Empty;
    public double Population { get; set; }
    public bool Capital { get; set; }
}

internal static unsafe class Program
{
    private static int failures;

    private static void expect(bool condition, string what)
    {
        if (!condition)
        {
            Console.WriteLine("FAIL: " + what);
            ++failures;
        }
    }

    private static void throwing_handler(gf_event* @event)
    {
        throw new InvalidOperationException("thrown by a C# handler");
    }

    private static void layout()
    {
        int disagreements = Layout.check(Console.Out);
        expect(disagreements == 0, "the runtime lays every struct out as the manifest says");
    }

    private static void negotiation()
    {
        gf_abi_version_info info = default;
        info.struct_size = (uint)sizeof(gf_abi_version_info);
        Abi.check(Abi.runtime.version(&info));
        expect(info.abi_major == Constants.GF_ABI_MAJOR && info.abi_minor >= Constants.GF_ABI_MINOR,
               "the library serves the manifest's ABI version");
        Console.WriteLine($".NET {Environment.Version}: GUI.Forms {info.library_major}.{info.library_minor}." +
                          $"{info.library_patch}, C ABI {info.abi_major}.{info.abi_minor}");
        gf_runtime_table later = default;
        gf_status refused = Exports.gf_abi_get_table(Constants.GF_TABLE_RUNTIME, Constants.GF_ABI_MAJOR + 1u, 0u,
                                                     &later, (ulong)sizeof(gf_runtime_table));
        expect(refused == Constants.GF_STATUS_UNSUPPORTED_VERSION, "a later major is refused");
        // The tables are negotiated once and lent in place (N72): two reads
        // borrow the same table, not two copies.
        ref gf_objects_table first = ref System.Runtime.CompilerServices.Unsafe.AsRef(in Abi.objects);
        ref gf_objects_table second = ref System.Runtime.CompilerServices.Unsafe.AsRef(in Abi.objects);
        expect(System.Runtime.CompilerServices.Unsafe.AreSame(ref first, ref second),
               "every call borrows the one negotiated table");
        expect(IntPtr.Size == 8, "the binding runs 64-bit (N73)");
    }

    private static void ownership()
    {
        ObjectHandle label = ObjectHandle.create("label"u8, "csharp.label"u8);
        gf_object_state state = 99u;
        Abi.check(Abi.objects.state(label.value, &state));
        expect(state == Constants.GF_OBJECT_STATE_ALIVE, "a created label is alive");
        ObjectHandleView view = new ObjectHandleView(label);
        expect(view.type_id == "label" && view.stable_id == "csharp.label" && view.state == "alive",
               "the debugger view reads the object through the ABI (" + view.type_id + ", " + view.stable_id + ", " +
                   view.state + ")");
        gf_object raw = label.value;
        label.Dispose();
        gf_status after = Abi.objects.state(raw, &state);
        expect(after == Constants.GF_STATUS_STALE_HANDLE, "a released handle is stale");
    }

    private static void refusal()
    {
        try
        {
            ObjectHandle.create("no_such_control"u8, "csharp.none"u8).Dispose();
            expect(false, "an unknown control type is refused");
        }
        catch (GuiFormsException refused)
        {
            expect(refused.status == Constants.GF_STATUS_NOT_FOUND ||
                       refused.status == Constants.GF_STATUS_INVALID_ARGUMENT,
                   "the refusal carries its status (" + refused.status + ")");
            expect(refused.Message.Length > 0, "the refusal carries the error detail");
        }
    }

    private static void event_thunk()
    {
        using ObjectHandle check = ObjectHandle.create("check_box"u8, "csharp.check"u8);
        gf_callback callback = Callbacks.to_native(throwing_handler);
        gf_subscription subscription = 0;
        gf_status subscribed;
        fixed (byte* name = "check_state_changed"u8)
        {
            subscribed = Abi.events.subscribe(check.value, Utf8.view(name, 19), &callback, &subscription);
        }
        if (subscribed != Constants.GF_STATUS_OK)
        {
            Callbacks.free_unregistered(callback);
        }
        Abi.check(subscribed);

        uint depth_before = ShadowStack.depth();
        gf_value value = default;
        value.struct_size = (uint)sizeof(gf_value);
        value.kind = Constants.GF_VALUE_KIND_BOOLEAN;
        value.boolean = 1u;
        gf_status set;
        fixed (byte* property = "checked"u8)
        {
            set = Abi.properties.set(check.value, Utf8.view(property, 7), &value);
        }
        expect(set == Constants.GF_STATUS_OK, "a handler's exception does not fail the library's call");
        expect(ShadowStack.depth() == depth_before, "the thread's depth is as it was after the handler threw");

        try
        {
            Abi.check(set);
            expect(false, "the handler's exception is rethrown at the call that ran it");
        }
        catch (CallbackFailure failure)
        {
            expect(failure.InnerException is InvalidOperationException, "the failure carries the handler's exception");
            ShadowFrame[] frames = failure.frames;
            expect(frames.Length >= 2, "the failure holds the frames it was thrown under");
            if (frames.Length >= 2)
            {
                ShadowFrame outer = frames[0];
                ShadowFrame inner = frames[frames.Length - 1];
                expect(outer.kind == Constants.GF_SHADOW_FRAME_KIND_LIBRARY_CALL &&
                           outer.table_id == Constants.GF_TABLE_PROPERTIES,
                       "the outermost frame is the properties.set call");
                expect(inner.kind_name == "event_callback", "a frame names its kind for the debugger");
                expect(inner.kind == Constants.GF_SHADOW_FRAME_KIND_EVENT_CALLBACK && inner.subject == "csharp.check" &&
                           inner.name == "check_state_changed",
                       "the innermost frame is the event, with its sender and name (" + inner + ")");
            }
        }
        expect(CallbackFailure.take_pending().Length == 0, "a rethrown failure is no longer kept");
        Abi.check(Abi.events.disconnect(subscription));
    }

    // A window's help_requested (since 1.1) through a thunk that sets handled.
    private static void answering_handler(gf_event* @event)
    {
        for (ulong index = 0; index < @event->field_count; ++index)
        {
            gf_event_field* field = &@event->fields[index];
            ReadOnlySpan<byte> name = new ReadOnlySpan<byte>(field->name.data, checked((int)field->name.size));
            if (name.EndsWith("handled"u8))
            {
                gf_value handled = default;
                handled.struct_size = (uint)sizeof(gf_value);
                handled.kind = Constants.GF_VALUE_KIND_BOOLEAN;
                handled.boolean = 1u;
                Abi.check_status(Abi.events.write_field(@event, index, &handled));
            }
        }
    }

    private static void window_help()
    {
        ulong count = 0;
        Abi.check(Abi.windows.interaction_count(&count));
        expect(count == 11u, "a window raises eleven interactions");
        using ObjectHandle root = ObjectHandle.create("Panel"u8, "csharp.window"u8);
        gf_object made = 0;
        Abi.check(Abi.windows.create(root.value, &made));
        using ObjectHandle window = ObjectHandle.adopt(made);
        gf_callback callback = Callbacks.to_native(answering_handler);
        gf_subscription subscription = 0;
        gf_status subscribed;
        fixed (byte* name = "help_requested"u8)
        {
            subscribed = Abi.events.subscribe(window.value, Utf8.view(name, 14), &callback, &subscription);
        }
        if (subscribed != Constants.GF_STATUS_OK)
        {
            Callbacks.free_unregistered(callback);
        }
        Abi.check(subscribed);
        uint handled = 0;
        Abi.check(Abi.windows.request_help(window.value, &handled));
        expect(handled == 1u, "a C# help_requested handler answers the window's help request");
        Abi.check(Abi.events.disconnect(subscription));
    }

    private static void posted_thunk()
    {
        using ObjectHandle label = ObjectHandle.create("label"u8, "csharp.posted"u8);
        gf_callback callback = Callbacks.to_native(throwing_handler);
        gf_status posted = Abi.dispatch.begin_invoke(label.value, &callback);
        if (posted != Constants.GF_STATUS_OK)
        {
            Callbacks.free_unregistered(callback);
        }
        Abi.check(posted);
        ulong invoked = 0;
        gf_status ran = Abi.dispatch.run_pending(&invoked);
        expect(ran == Constants.GF_STATUS_OK && invoked == 1u, "the posted callback ran");
        expect(ShadowStack.depth() == 0u, "no frame is left after a posted callback threw");
        // dispatch.run_pending returns to this method, so the failure is kept
        // for it, never raised through unhandled (N70).
        expect(unhandled_raised == 0, "a failure under run_pending is not raised as unhandled");
        CallbackFailure[] kept = CallbackFailure.take_pending();
        expect(kept.Length == 1, "the posted callback's exception is kept");
        if (kept.Length == 1)
        {
            ShadowFrame[] frames = kept[0].frames;
            expect(frames.Length == 2 && frames[0].table_id == Constants.GF_TABLE_DISPATCH &&
                       frames[1].kind == Constants.GF_SHADOW_FRAME_KIND_POSTED_CALLBACK &&
                       frames[1].subject == "csharp.posted",
                   "a posted callback's failure holds dispatch.run_pending and the posted frame");
        }
    }

    // GF_TABLE_SUBSURFACES (since 1.1) without a window: a host's state, a
    // refused set_root, and content the host takes and gives back unstarted
    // (its build callback is released, never invoked). The C kit runs the
    // sub-surface's thread.
    private static void subsurfaces()
    {
        Abi.check(Exports.get_subsurfaces_table(out gf_subsurfaces_table table));
        using ObjectHandle host = ObjectHandle.create("sub_surface_host"u8, "csharp.subsurface"u8);
        gf_subsurface_state state = 99u;
        Abi.check(table.state(host.value, &state));
        expect(state == Constants.GF_SUBSURFACE_STATE_STOPPED, "a host without content has no thread");
        gf_status refused = table.set_root(host.value);
        expect(refused == Constants.GF_STATUS_INVALID_STATE, "set_root outside a build callback is refused");
        gf_subsurface_content content = default;
        content.struct_size = (uint)sizeof(gf_subsurface_content);
        content.build = Callbacks.to_native(throwing_handler);
        gf_status taken = table.set_content(host.value, &content);
        if (taken != Constants.GF_STATUS_OK)
        {
            Callbacks.free_unregistered(content.build);
        }
        Abi.check(taken);
        Abi.check(table.set_content(host.value, null));
        Abi.check(table.state(host.value, &state));
        expect(state == Constants.GF_SUBSURFACE_STATE_STOPPED, "content that never started leaves no thread");
    }

    // The application table: a configuration is checked before anything is
    // made, and closing with no application does nothing.
    private static void application()
    {
        Abi.check(Exports.get_application_table(out gf_application_table table));
        gf_application_config config = default;
        config.struct_size = (uint)sizeof(gf_application_config);
        config.theme_base = 99u;
        config.presentation_source = Constants.GF_PRESENTATION_SOURCE_DEFAULTS;
        gf_theme_report report = default;
        report.struct_size = (uint)sizeof(gf_theme_report);
        gf_status refused = table.create(&config, &report);
        expect(refused == Constants.GF_STATUS_INVALID_ARGUMENT, "an unknown theme base is refused");
        gf_application_state state = 99u;
        Abi.check(table.state(&state));
        expect(state == Constants.GF_APPLICATION_STATE_NONE, "a refused configuration makes no application");
        ulong alive = 99u;
        Abi.check(table.close(&alive));
        expect(alive == 0u, "closing with no application counts no window");
    }

    private static int unhandled_raised;

    private static void count_unhandled(object? sender, CallbackFailureEventArgs arguments)
    {
        ++unhandled_raised;
        arguments.handled = true;
    }

    // N70: a refused call carries the callback failures kept on its thread
    // in its exception, instead of leaving them for a later call.
    private static void refused_call_carries_failures()
    {
        using ObjectHandle check = ObjectHandle.create("check_box"u8, "csharp.carried"u8);
        gf_callback callback = Callbacks.to_native(throwing_handler);
        gf_subscription subscription = 0;
        gf_status subscribed;
        fixed (byte* name = "check_state_changed"u8)
        {
            subscribed = Abi.events.subscribe(check.value, Utf8.view(name, 19), &callback, &subscription);
        }
        if (subscribed != Constants.GF_STATUS_OK)
        {
            Callbacks.free_unregistered(callback);
        }
        Abi.check(subscribed);
        gf_value value = default;
        value.struct_size = (uint)sizeof(gf_value);
        value.kind = Constants.GF_VALUE_KIND_BOOLEAN;
        value.boolean = 1u;
        fixed (byte* property = "checked"u8)
        {
            // The handler throws; its failure is kept for this thread.
            expect(Abi.properties.set(check.value, Utf8.view(property, 7), &value) == Constants.GF_STATUS_OK,
                   "the handler ran");
        }
        try
        {
            Abi.check(Constants.GF_STATUS_INVALID_ARGUMENT);
            expect(false, "a refused status throws");
        }
        catch (GuiFormsException refused)
        {
            CallbackFailure[]? carried = refused.Data["callback_failures"] as CallbackFailure[];
            expect(carried != null && carried.Length == 1, "the refusal carries the kept failure");
        }
        expect(CallbackFailure.take_pending().Length == 0, "nothing is left for a later call");
        Abi.check(Abi.events.disconnect(subscription));
    }

    // A binding source over the window root made here, and its handle.
    private static ObjectHandle make_source(ObjectHandle window, ReadOnlySpan<byte> stable_id)
    {
        gf_create_arguments arguments = default;
        arguments.struct_size = (uint)sizeof(gf_create_arguments);
        arguments.owner = window.value;
        gf_object made = 0;
        fixed (byte* type = "binding_source"u8)
        fixed (byte* id = stable_id)
        {
            Abi.check(Abi.objects.create_with(Utf8.view(type, 14), Utf8.view(id, stable_id.Length), &arguments, &made));
        }
        return ObjectHandle.adopt(made);
    }

    // The current row's field as text or a number, through the bindings table.
    private static string current_text(ObjectHandle source, ReadOnlySpan<byte> field)
    {
        gf_value value = default;
        value.struct_size = (uint)sizeof(gf_value);
        byte* text = stackalloc byte[128];
        ulong required = 0;
        fixed (byte* name = field)
        {
            Abi.check(Abi.bindings.current_field(source.value, Utf8.view(name, field.Length), &value, text, 128u,
                                                 &required));
        }
        if (value.kind == Constants.GF_VALUE_KIND_TEXT)
        {
            return Encoding.UTF8.GetString(value.text.data, (int)value.text.size);
        }
        if (value.kind == Constants.GF_VALUE_KIND_NUMBER)
        {
            return value.number.ToString(System.Globalization.CultureInfo.InvariantCulture);
        }
        return "?";
    }

    private static void move_to(ObjectHandle source, long position)
    {
        uint moved = 0;
        Abi.check(Abi.bindings.set_position(source.value, position, &moved));
    }

    private static ulong shown_count(ObjectHandle source)
    {
        ulong generation = 0;
        ulong count = 0;
        Abi.check(Abi.bindings.generation(source.value, &generation, &count));
        return count;
    }

    // N31: a List<T> as a source's data provider: read in place, edited in
    // place, sorted by the source's own index; a BindingList<T>'s ListChanged
    // reaches the source without a call; set_provider releases the provider.
    private static void binding_list()
    {
        using ObjectHandle root = ObjectHandle.create("Panel"u8, "csharp.binding"u8);
        gf_object made = 0;
        Abi.check(Abi.windows.create(root.value, &made));
        using ObjectHandle window = ObjectHandle.adopt(made);
        using ObjectHandle source = make_source(window, "csharp.binding.source"u8);

        List<City> cities = new List<City>
        {
            new City { Name = "Oslo", Population = 0.7, Capital = true },
            new City { Name = "Lima", Population = 10.0, Capital = true },
            new City { Name = "Bern", Population = 0.1, Capital = true },
        };
        BindingListProvider provider = BindingListProvider.bind(source.value, cities);
        expect(provider.id != 0u && shown_count(source) == 3u, "a List<T> binds with its count");
        move_to(source, 1);
        expect(current_text(source, "name"u8) == "Lima", "the source reads the list's item in place");

        gf_value population = default;
        population.struct_size = (uint)sizeof(gf_value);
        population.kind = Constants.GF_VALUE_KIND_NUMBER;
        population.number = 11.5;
        uint accepted = 0;
        fixed (byte* name = "Population"u8)
        {
            Abi.check(Abi.bindings.set_current_field(source.value, Utf8.view(name, 10), &population, &accepted));
        }
        expect(accepted == 1u && cities[1].Population == 11.5, "an edit writes straight to the item's property");

        cities.Add(new City { Name = "Rome", Population = 2.8 });
        provider.notify_inserted(3, 1);
        expect(shown_count(source) == 4u, "a reported insertion reaches the source");

        gf_binding_sort_key key = default;
        key.struct_size = (uint)sizeof(gf_binding_sort_key);
        key.direction = Constants.GF_BINDING_SORT_DESCENDING;
        fixed (byte* column = "population"u8)
        {
            key.column = Utf8.view(column, 10);
            Abi.check(Abi.bindings.set_sort(source.value, &key, 1u));
        }
        move_to(source, 0);
        string largest = current_text(source, "name"u8);
        move_to(source, 3);
        string smallest = current_text(source, "name"u8);
        expect(largest == "Lima" && smallest == "Bern", "the source sorts a list that cannot sort itself");
        Abi.check(Abi.bindings.set_sort(source.value, null, 0u));

        BindingList<City> observed = new BindingList<City>(new List<City>(cities));
        ulong replaced = 0;
        BindingListProvider listening = BindingListProvider.bind(source.value, observed);
        expect(provider.id == 0u, "binding another provider releases the first");
        observed.Add(new City { Name = "Kyiv", Population = 3.0 });
        expect(shown_count(source) == 5u, "a BindingList<T>'s ListChanged reaches the source");
        observed[0].Name = "Paris";
        observed.ResetItem(0);
        move_to(source, 0);
        expect(current_text(source, "name"u8) == "Paris", "an item change reaches the source");
        Abi.check(Abi.bindings.set_provider(source.value, null, 0u, null, &replaced));
        expect(listening.id == 0u, "set_provider with no callbacks releases the provider");
    }

    // The name at a view position.
    private static string name_at(ObjectHandle source, long position)
    {
        move_to(source, position);
        return current_text(source, "name"u8);
    }

    // One predicate over a column, with an optional operand.
    private static void set_filter(ObjectHandle source, ReadOnlySpan<byte> column, gf_binding_predicate_operator op,
                                   gf_value* operand)
    {
        gf_binding_predicate predicate = default;
        predicate.struct_size = (uint)sizeof(gf_binding_predicate);
        predicate.op = op;
        predicate.operand = operand;
        fixed (byte* name = column)
        {
            predicate.column = Utf8.view(name, column.Length);
            Abi.check(Abi.bindings.set_filter(source.value, &predicate, 1u));
        }
    }

    // N31 batch 2: a reported move moves the rows without a reset; a
    // DataView (IBindingListView) filters itself from the structured
    // predicates; a BindingList<T> that cannot filter is filtered by the
    // source's own index.
    private static void binding_moves_and_filters()
    {
        using ObjectHandle root = ObjectHandle.create("Panel"u8, "csharp.filter"u8);
        gf_object made = 0;
        Abi.check(Abi.windows.create(root.value, &made));
        using ObjectHandle window = ObjectHandle.adopt(made);
        using ObjectHandle source = make_source(window, "csharp.filter.source"u8);

        List<City> cities = new List<City>
        {
            new City { Name = "Oslo", Population = 0.7 },
            new City { Name = "Lima", Population = 10.0 },
            new City { Name = "Bern", Population = 0.1 },
            new City { Name = "Rome", Population = 2.8 },
        };
        BindingListProvider provider = BindingListProvider.bind(source.value, cities);
        City moved = cities[0];
        cities.RemoveAt(0);
        cities.Insert(2, moved);
        provider.notify_moved(0, 1, 2);
        expect(shown_count(source) == 4u && name_at(source, 0) == "Lima" && name_at(source, 2) == "Oslo" &&
                   name_at(source, 3) == "Rome",
               "a reported move reaches the source in place");

        DataTable table = new DataTable("cities");
        table.Columns.Add("Name", typeof(string));
        table.Columns.Add("Population", typeof(double));
        table.Rows.Add("Oslo", 0.7);
        table.Rows.Add("Lima", 10.0);
        table.Rows.Add("O'Hare*", 2.8);
        table.Rows.Add("Rome", DBNull.Value);
        DataView view = new DataView(table);
        BindingListProvider viewing = BindingListProvider.bind(source.value, view);
        expect(provider.id == 0u && shown_count(source) == 4u, "a DataView binds through its ITypedList columns");

        gf_value bound = default;
        bound.struct_size = (uint)sizeof(gf_value);
        bound.kind = Constants.GF_VALUE_KIND_NUMBER;
        bound.number = 1.0;
        set_filter(source, "population"u8, Constants.GF_BINDING_PREDICATE_GREATER, &bound);
        expect(view.RowFilter == "[Population] > 1" && view.Count == 2 && shown_count(source) == 2u,
               "a DataView filters itself from the predicate");
        fixed (byte* pattern = "'H"u8)
        {
            gf_value text = default;
            text.struct_size = (uint)sizeof(gf_value);
            text.kind = Constants.GF_VALUE_KIND_TEXT;
            text.text = Utf8.view(pattern, 2);
            set_filter(source, "name"u8, Constants.GF_BINDING_PREDICATE_CONTAINS, &text);
        }
        expect(view.Count == 1 && shown_count(source) == 1u && name_at(source, 0) == "O'Hare*",
               "a quote in a contains operand is escaped for the grammar");
        set_filter(source, "population"u8, Constants.GF_BINDING_PREDICATE_IS_NULL, null);
        expect(view.Count == 1 && name_at(source, 0) == "Rome", "IS NULL reaches the DataView");
        Abi.check(Abi.bindings.set_filter(source.value, null, 0u));
        expect(string.IsNullOrEmpty(view.RowFilter) && shown_count(source) == 4u, "clearing the filter clears the view's");

        BindingList<City> plain = new BindingList<City>(new List<City>(cities));
        BindingListProvider listening = BindingListProvider.bind(source.value, plain);
        expect(viewing.id == 0u, "binding another provider releases the DataView's");
        set_filter(source, "population"u8, Constants.GF_BINDING_PREDICATE_GREATER, &bound);
        expect(shown_count(source) == 2u, "a list that cannot filter is filtered by the source");
        ulong replaced = 0;
        Abi.check(Abi.bindings.set_provider(source.value, null, 0u, null, &replaced));
        expect(listening.id == 0u, "set_provider with no callbacks releases the provider");
    }

    private static int Main()
    {
        Console.OutputEncoding = Encoding.UTF8;
        CallbackFailure.unhandled += count_unhandled;
        layout();
        negotiation();
        application();
        ownership();
        refusal();
        event_thunk();
        window_help();
        posted_thunk();
        refused_call_carries_failures();
        subsurfaces();
        binding_list();
        binding_moves_and_filters();
        expect(unhandled_raised == 0, "no failure in these checks ran under an event loop");
        Console.WriteLine(failures == 0 ? "gui_forms_abi_csharp: all checks passed" :
                                          $"gui_forms_abi_csharp: {failures} checks failed");
        return failures == 0 ? 0 : 1;
    }
}
