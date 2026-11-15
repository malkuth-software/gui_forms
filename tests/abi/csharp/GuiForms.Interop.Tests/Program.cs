// The C# interop against the built library: the generated layout against the
// manifest, negotiation, an object handle's life, a refusal as an exception,
// the callback thunks, and a native exception reflected as a NativeException
// with its frames (C ABI 1.2), composite controls (C ABI 1.5). A handler that throws is caught by its thunk under
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
            // A refusal that was no exception has no native type or trace.
            expect(refused.native_detail == null && !refused.Data.Contains("native_exception_type"),
                   "a plain refusal carries no native detail");
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

    // Exception reflection (since 1.2): a handler that calls into the library
    // and catches the native exception it raises there.
    private static NativeException? caught_native;

    private static void raising_handler(gf_event* @event)
    {
        try
        {
            fixed (byte* message = "thrown inside the library"u8)
            {
                Abi.check(Abi.diagnostics.raise_native_exception(Utf8.view(message, 25)));
            }
        }
        catch (NativeException native)
        {
            caught_native = native;
        }
    }

    // Off, a native exception is GF_STATUS_INTERNAL; on, Abi.check throws a
    // NativeException that carries the frames it was thrown under, three deep
    // here (dispatch.run_pending, the posted callback, the raise), through
    // the named thunks.
    private static void native_exception_reflection()
    {
        expect(!NativeException.reflection, "exception reflection is off until a program turns it on");
        gf_status unreflected;
        fixed (byte* message = "unreflected"u8)
        {
            unreflected = Abi.diagnostics.raise_native_exception(Utf8.view(message, 11));
        }
        expect(unreflected == Constants.GF_STATUS_INTERNAL, "without reflection a native exception is internal");
        try
        {
            Abi.check(unreflected);
            expect(false, "an internal failure is refused");
        }
        catch (NativeException)
        {
            expect(false, "without reflection no NativeException is raised");
        }
        catch (GuiFormsException refused)
        {
            expect(refused.status == Constants.GF_STATUS_INTERNAL && refused.Message == "unreflected",
                   "without reflection the refusal carries the exception's text (" + refused.Message + ")");
        }

        NativeException.reflection = true;
        try
        {
            expect(NativeException.reflection, "exception reflection reads on");
            using ObjectHandle label = ObjectHandle.create("label"u8, "csharp.native"u8);
            gf_callback callback = Callbacks.to_native(raising_handler);
            gf_status posted = Abi.dispatch.begin_invoke(label.value, &callback);
            if (posted != Constants.GF_STATUS_OK)
            {
                Callbacks.free_unregistered(callback);
            }
            Abi.check(posted);
            ulong invoked = 0;
            Abi.check(Abi.dispatch.run_pending(&invoked));
            expect(invoked == 1u, "the raising callback ran");
        }
        finally
        {
            NativeException.reflection = false;
        }
        NativeException? native = caught_native;
        expect(native != null, "the handler caught a NativeException");
        if (native == null)
        {
            return;
        }
        expect(native.status == Constants.GF_STATUS_NATIVE_EXCEPTION && native.Message == "thrown inside the library" &&
                   native.site == "c_abi" && native.table_id == Constants.GF_TABLE_DIAGNOSTICS &&
                   native.slot_index == 7u,
               "the exception carries the snapshot's status, text, site and function (" + native.Message + ")");
        ShadowFrame[] frames = native.frames;
        expect(frames.Length == 3 && native.depth == 3u, "the exception carries the three frames of the throw");
        if (frames.Length == 3)
        {
            expect(frames[0].kind == Constants.GF_SHADOW_FRAME_KIND_LIBRARY_CALL &&
                       frames[0].table_id == Constants.GF_TABLE_DISPATCH,
                   "the outermost frame is dispatch.run_pending");
            expect(frames[1].kind == Constants.GF_SHADOW_FRAME_KIND_POSTED_CALLBACK && frames[1].subject == "csharp.native",
                   "the middle frame is the posted callback on its target (" + frames[1] + ")");
            expect(frames[2].kind == Constants.GF_SHADOW_FRAME_KIND_LIBRARY_CALL &&
                       frames[2].table_id == Constants.GF_TABLE_DIAGNOSTICS && frames[2].slot_index == 7u,
                   "the innermost frame is the raise the exception unwound");
        }
        string trace = native.StackTrace ?? string.Empty;
        expect(trace.Contains("NativeFrames.posted_callback") && trace.Contains("NativeFrames.library_call"),
               "the stack trace reads the library's frames as named thunks");
        expect(native.Data["shadow_frames"] is string, "the exception's data carries the frames as text");
        // S4 (C ABI 1.5): a development build of the library keeps the C++
        // type and the trace at the throw; a release build keeps neither.
        FailureDetail? detail = native.native_detail;
        if (detail != null)
        {
            expect(detail.exception_type.Contains("runtime_error") &&
                       (string?)native.Data["native_exception_type"] == detail.exception_type,
                   "the native detail names the C++ type (" + detail.exception_type + ")");
            expect(detail.trace.Length <= (int)Constants.GF_FAILURE_TRACE_CAPACITY,
                   "the native trace fits its capacity");
            expect(detail.trace.Length == 0 || native.Data["native_trace"] is string,
                   "the exception's data carries the trace as text");
            // Filed directly, never parked: no frames were lost at a park.
            expect(!detail.frames_lost_at_park && !native.Data.Contains("native_frames_lost_at_park"),
                   "an unparked failure is not marked as having lost frames at a park");
        }
        expect(CallbackFailure.take_pending().Length == 0, "a caught native exception leaves no callback failure");
        expect(ShadowStack.depth() == 0u, "no frame is left open after the reflected failure");
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

    // A composite control's document: a boolean a command toggles and a
    // label child bound to a text property.
    private const string card_document =
        "<control xmlns=\"urn:gui-forms:control:1\" name=\"csharp-card\" version=\"1.0\" base=\"user-control\" family=\"card\">\n" +
        "  <property name=\"title\" kind=\"text\" default=\"\" category=\"Appearance\"/>\n" +
        "  <property name=\"checked\" kind=\"boolean\" default=\"false\" category=\"Behavior\"/>\n" +
        "  <command name=\"toggle\" enabled=\"enabled(self)\" run=\"toggle(checked)\"/>\n" +
        "  <part name=\"face\" role=\"face\" box=\"inset(region, 0)\"/>\n" +
        "  <children>\n" +
        "    <flex-panel id=\"@header\" dock=\"top\">\n" +
        "      <label id=\"@title\" text=\"bind(self.title, one_way)\"/>\n" +
        "    </flex-panel>\n" +
        "  </children>\n" +
        "  <focus target=\"none\"/>\n" +
        "  <semantics role=\"group\" name=\"property(title)\" actions=\"press(toggle)\" children=\"exposed\"/>\n" +
        "</control>\n";

    // Composite controls (C ABI 1.5): a refused document throws with its
    // diagnostics, a loaded one makes a control whose command runs and
    // whose child is found; the definition goes before the control.
    private static void composites()
    {
        bool refused = false;
        try
        {
            using CompositeDefinition wrong = CompositeDefinition.load("csharp-card.control", card_document, "label");
        }
        catch (GuiFormsException error)
        {
            refused = error.status == Constants.GF_STATUS_INVALID_ARGUMENT;
        }
        expect(refused, "a document naming a type not listed is refused");
        ObjectHandle card;
        using (CompositeDefinition definition =
                   CompositeDefinition.load("csharp-card.control", card_document, "flex_panel", "label"))
        {
            card = definition.create("csharp.composite.card");
        }
        using (card)
        {
            expect(Composite.perform(card, "toggle"), "the composite's command runs");
            gf_value got = default;
            got.struct_size = (uint)sizeof(gf_value);
            byte* text = stackalloc byte[64];
            ulong required = 0;
            fixed (byte* property = "checked"u8)
            {
                Abi.check(Abi.properties.get(card.value, Utf8.view(property, 7), &got, text, 64, &required));
            }
            expect(got.kind == Constants.GF_VALUE_KIND_BOOLEAN && got.boolean == 1u, "the command toggled the property");
            using ObjectHandle? title = Composite.child(card, "title");
            expect(title != null, "the title child is found");
            expect(Composite.child(card, "nothing") == null, "an unknown local id is not found");
            bool undeclared = false;
            try
            {
                Composite.perform(card, "explode");
            }
            catch (GuiFormsException error)
            {
                undeclared = error.status == Constants.GF_STATUS_NOT_FOUND;
            }
            expect(undeclared, "an undeclared command throws not found");
        }
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

    // GF_TABLE_HELP's language files (since 1.2, roadmap row 2.13): a help
    // field set as a property, the template export, a user override read
    // back with its origin, and the way back.
    private const string help_built_in =
        "<locale xml:lang=\"en\"><controls><control name=\"csharp\"><control name=\"help\" text=\"Help\" " +
        "help-line=\"The C# help panel.\"><help keywords=\"help; csharp\">The panel C# reads.</help>" +
        "</control></control></controls></locale>";

    private const string help_user =
        "<locale xml:lang=\"en\"><controls><control name=\"csharp\"><control name=\"help\" " +
        "help-line=\"The user's own words.\"/></control></controls></locale>";

    private static gf_status help_load(delegate* unmanaged[Cdecl]<gf_object, gf_utf8_view, gf_status> load,
                                       gf_object system, string xml)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(xml);
        fixed (byte* data = bytes)
        {
            return load(system, Utf8.view(data, bytes.Length));
        }
    }

    private static string help_field(in gf_help_table table, gf_object system, string stable_id,
                                     gf_locale_field field, out gf_locale_origin origin)
    {
        byte[] id = Encoding.UTF8.GetBytes(stable_id);
        byte[] text = new byte[512];
        ulong required = 0;
        gf_locale_origin found = 99u;
        fixed (byte* id_data = id)
        fixed (byte* text_data = text)
        {
            Abi.check(table.read_field(system, Utf8.view(id_data, id.Length), field, text_data, (ulong)text.Length,
                                       &required, &found));
        }
        origin = found;
        return Encoding.UTF8.GetString(text, 0, checked((int)required));
    }

    private static void help_language_files()
    {
        Abi.check(Exports.get_help_table(out gf_help_table table));
        expect(table.struct_size >= (uint)sizeof(gf_help_table), "the help table carries the language-file slots");
        using ObjectHandle system = ObjectHandle.create("help_book_system"u8, "csharp.help.system"u8);
        using ObjectHandle root = ObjectHandle.create("panel"u8, "csharp.help"u8);
        using ObjectHandle label = ObjectHandle.create("label"u8, "csharp.help.caption"u8);
        Abi.check(Abi.objects.insert_child(root.value, label.value, 0u));
        gf_object made = 0;
        Abi.check(Abi.windows.create(root.value, &made));
        using ObjectHandle window = ObjectHandle.adopt(made);
        Abi.check(table.attach(system.value, window.value));
        Abi.check(help_load(table.load_built_in, system.value, help_built_in));

        byte[] line = Encoding.UTF8.GetBytes("Set from C#.");
        gf_value value = default;
        value.struct_size = (uint)sizeof(gf_value);
        value.kind = Constants.GF_VALUE_KIND_TEXT;
        fixed (byte* line_data = line)
        fixed (byte* property = "help_line"u8)
        {
            value.text = Utf8.view(line_data, line.Length);
            Abi.check(Abi.properties.set(label.value, Utf8.view(property, 9), &value));
        }
        string caption = help_field(table, system.value, "csharp.help.caption", Constants.GF_LOCALE_FIELD_HELP_LINE,
                                    out gf_locale_origin caption_origin);
        expect(caption == "Set from C#." && caption_origin == Constants.GF_LOCALE_ORIGIN_PROGRAM,
               "a help line set as a property is the program's value (" + caption + ")");

        ulong required = 0;
        byte[] empty = Array.Empty<byte>();
        fixed (byte* none = empty)
        {
            gf_status sized = table.export_template(system.value, Utf8.view(none, 0), null, 0u, &required);
            expect(sized == Constants.GF_STATUS_BUFFER_TOO_SMALL && required > 0u, "the template's size comes first");
        }
        byte[] exported = new byte[checked((int)required)];
        fixed (byte* none = empty)
        fixed (byte* data = exported)
        {
            Abi.check(table.export_template(system.value, Utf8.view(none, 0), data, required, &required));
        }
        string template = Encoding.UTF8.GetString(exported);
        expect(template.Contains("help-line=\"The C# help panel.\"") && template.Contains("Set from C#.") &&
                   template.Contains("keywords=\"help; csharp\""),
               "the template lists the built-in and program help fields");

        Abi.check(help_load(table.load_user_overrides, system.value, help_user));
        string overridden = help_field(table, system.value, "csharp.help", Constants.GF_LOCALE_FIELD_HELP_LINE,
                                       out gf_locale_origin user_origin);
        expect(overridden == "The user's own words." && user_origin == Constants.GF_LOCALE_ORIGIN_USER,
               "the user's override reads back");
        Abi.check(table.use_built_in_language(system.value, 1u));
        string rescued = help_field(table, system.value, "csharp.help", Constants.GF_LOCALE_FIELD_HELP_LINE,
                                    out gf_locale_origin rescued_origin);
        expect(rescued == "The C# help panel." && rescued_origin == Constants.GF_LOCALE_ORIGIN_BUILT_IN,
               "the way back shows the built-in text");
        Abi.check(table.use_built_in_language(system.value, 0u));
        Abi.check(table.clear_user_overrides(system.value));
        gf_status refused = help_load(table.load_translation, system.value, "<locale/>");
        expect(refused == Constants.GF_STATUS_INVALID_ARGUMENT, "a translation without a language is refused");
        Abi.check(table.detach(system.value, window.value));
    }

    // GF_TABLE_FONTS through FontFamilies: a name resolves once to a bundled
    // family's id (a Windows Forms name through the substitution table), the
    // id reads its name back (Font.FontFamily.Name), and a font carries the
    // id in its reserved word's low half beside its stack.
    private static void font_families()
    {
        uint selawik = FontFamilies.resolve("Selawik");
        expect(selawik != 0, "a bundled family has an id");
        expect(FontFamilies.name(selawik) == "Selawik", "the id reads its name back");
        expect(FontFamilies.resolve("Segoe UI") == selawik, "a Windows Forms name resolves through the substitutions");
        uint carlito = FontFamilies.resolve("carlito");
        expect(carlito != 0 && carlito != selawik && FontFamilies.name(carlito) == "Carlito",
               "a role's family has its own id and name");
        uint arimo = FontFamilies.resolve("Arimo");
        expect(arimo != 0 && FontFamilies.resolve("Arial") == arimo,
               "a metric match the substitutions name resolves to its bundled family");
        uint semibold = FontFamilies.resolve("Segoe UI Semibold");
        expect(semibold != 0 && semibold != selawik && FontFamilies.name(semibold) == "Selawik Semibold",
               "a substitution finds its family by name before the face it shares");
        expect(FontFamilies.resolve("Wingdings") == 0, "a name without a bundled family is unmapped");
        expect(FontFamilies.name(0) == null && FontFamilies.name(200) == null, "no name for zero or an unknown id");
        gf_font font = default;
        font.reserved = 0x00050000u;
        gf_font familied = FontFamilies.with_family(font, selawik);
        expect(FontFamilies.family_of(familied) == selawik && (familied.reserved >> 16) == 5u,
               "a font carries its family beside its stack");
    }

    // A minimal TrueType face that passes the library's load checks: one
    // empty glyph, a format 4 cmap with only the closing segment, and the
    // required tables in tag order at 4-byte aligned offsets (as the C
    // conformance kit's minimal_app_font).
    private static byte[] minimal_app_font()
    {
        string[] tags = { "cmap", "glyf", "head", "hhea", "hmtx", "loca", "maxp", "name", "post" };
        int[] lengths = { 36, 0, 54, 36, 4, 4, 6, 6, 32 };
        int offset = 12 + 16 * tags.Length;
        int[] placed = new int[tags.Length];
        for (int index = 0; index < tags.Length; index++)
        {
            placed[index] = offset;
            offset += (lengths[index] + 3) & ~3;
        }
        byte[] font = new byte[offset];
        void put16(int at, uint value)
        {
            font[at] = (byte)(value >> 8);
            font[at + 1] = (byte)value;
        }
        void put32(int at, uint value)
        {
            put16(at, value >> 16);
            put16(at + 2, value & 0xFFFFu);
        }
        put32(0, 0x00010000u);
        put16(4, (uint)tags.Length);
        for (int index = 0; index < tags.Length; index++)
        {
            int record = 12 + 16 * index;
            for (int character = 0; character < 4; character++)
            {
                font[record + character] = (byte)tags[index][character];
            }
            put32(record + 8, (uint)placed[index]);
            put32(record + 12, (uint)lengths[index]);
        }
        int cmap = placed[0];
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
        int head = placed[2];
        put32(head, 0x00010000u);
        put32(head + 12, 0x5F0F3CF5u);
        put16(head + 18, 1000u);
        int hhea = placed[3];
        put32(hhea, 0x00010000u);
        put16(hhea + 4, 800u);
        put16(hhea + 6, 0xFF38u);
        put16(hhea + 34, 1u);
        put16(placed[4], 500u);
        put32(placed[6], 0x00005000u);
        put16(placed[6] + 4, 1u);
        put16(placed[7] + 4, 6u);
        put32(placed[8], 0x00030000u);
        return font;
    }

    // AppFontFamily (C ABI 1.5, provisional): the program's own bytes load as
    // a family with an id above the bundled ones, a font carries it like a
    // bundled family, refusals throw with their reason, and disposing the
    // handle unloads it so the name is free again.
    private static void app_font_families()
    {
        byte[] bytes = minimal_app_font();
        AppFontFace[] faces = { new AppFontFace(bytes) };
        uint first;
        using (AppFontFamily family = AppFontFamily.load("CSharp Application Face", faces))
        {
            first = family.id;
            expect(first >= 256u, "an application family's id is above the bundled ids");
            gf_font font = FontFamilies.with_family(default, family.id);
            expect(FontFamilies.family_of(font) == family.id, "a font carries the application family");
            try
            {
                AppFontFamily.load("csharp application face", faces).Dispose();
                expect(false, "a loaded family's name is taken");
            }
            catch (GuiFormsException refused)
            {
                expect(refused.status == Constants.GF_STATUS_INVALID_ARGUMENT && refused.Message.Length > 0,
                       "a taken name is refused with its reason");
            }
            try
            {
                AppFontFamily.load("CSharp Broken Face", new[] { new AppFontFace(new byte[] { 1, 2, 3, 4 }) }).Dispose();
                expect(false, "bytes that are not a font are refused");
            }
            catch (GuiFormsException refused)
            {
                expect(refused.status == Constants.GF_STATUS_INVALID_ARGUMENT, "a face that fails the checks is refused");
            }
        }
        using AppFontFamily again = AppFontFamily.load("CSharp Application Face", faces);
        expect(again.id != 0u && again.id != first, "a disposed family's name is free and its id is not reused");
        again.unload();
        expect(again.id == 0u && !again.unload(), "an unloaded handle holds nothing");
    }

    // The content table (C ABI 1.4): a content surface's channel from the
    // caller's own code, a refusal as a result rather than a failed call,
    // and a content text editor's text.
    private static void content()
    {
        ref readonly gf_content_table table = ref Abi.content;
        using ObjectHandle surface = ObjectHandle.create("content_surface"u8, "csharp.content.surface"u8);
        gf_content_sink sink = 0;
        Abi.check(table.open_content(surface.value, 1u, &sink));
        expect(sink != 0, "open_content gives a sink");
        byte[] pixels = new byte[2 * 2 * 4];
        gf_content_publish_result result = Constants.GF_CONTENT_PUBLISH_REVOKED;
        fixed (byte* data = pixels)
        {
            gf_content_frame frame = default;
            frame.struct_size = (uint)sizeof(gf_content_frame);
            frame.pixel_format = Constants.GF_CONTENT_PIXEL_FORMAT_BGRA32_PREMULTIPLIED_SRGB;
            frame.width = 2u;
            frame.height = 2u;
            frame.row_bytes = 8u;
            frame.opaque = 1u;
            frame.damage.width = 2u;
            frame.damage.height = 2u;
            frame.pixels.data = data;
            frame.pixels.size = (ulong)pixels.Length;
            Abi.check(table.sink_publish(sink, &frame, &result));
            expect(result == Constants.GF_CONTENT_PUBLISH_PUBLISHED, "a valid frame is published");
            frame.height = 0u;
            Abi.check(table.sink_publish(sink, &frame, &result));
            expect(result == Constants.GF_CONTENT_PUBLISH_REFUSED_EMPTY, "an empty frame is a refusal");
        }
        gf_content_statistics statistics = default;
        statistics.struct_size = (uint)sizeof(gf_content_statistics);
        Abi.check(table.statistics(surface.value, &statistics));
        expect(statistics.frames_published == 1 && statistics.frames_refused == 1,
               "the statistics count the frame and the refusal");
        Abi.check(table.sink_release(sink));
        expect(table.sink_release(sink) == Constants.GF_STATUS_STALE_HANDLE, "a released sink is stale");
        Abi.check(table.close_content(surface.value));

        using ObjectHandle editor = ObjectHandle.create("content_text_editor"u8, "csharp.content.editor"u8);
        byte[] text = Encoding.UTF8.GetBytes("caf\u00e9");
        fixed (byte* data = text)
        {
            Abi.check(table.editor_set_text(editor.value, Utf8.view(data, text.Length)));
        }
        byte[] read = new byte[16];
        ulong required = 0;
        fixed (byte* buffer = read)
        {
            Abi.check(table.editor_text(editor.value, buffer, (ulong)read.Length, &required));
        }
        expect(Encoding.UTF8.GetString(read, 0, checked((int)required)) == "caf\u00e9", "the editor's text reads back");
        expect(table.open_content(editor.value, 0u, &sink) == Constants.GF_STATUS_WRONG_HANDLE_KIND,
               "an editor is not a content surface");

        // The provisional editor slots: Tab, an edit at the selection and
        // the composition (none open).
        Abi.check(table.editor_set_accepts_tab(editor.value, 1u));
        uint applied = 0u;
        fixed (byte* bang = "!"u8)
        {
            Abi.check(table.editor_replace_selection(editor.value, Utf8.view(bang, 1), &applied));
        }
        expect(applied == 1u, "replace_selection inserts at the caret");
        uint composing = 9u;
        ulong marked_start = 0;
        ulong marked_end = 0;
        Abi.check(table.editor_composition_range(editor.value, &composing, &marked_start, &marked_end));
        expect(composing == 0u, "no composition is open");
        Abi.check(table.editor_delete_selection(editor.value, &applied));
        expect(applied == 0u, "an empty selection deletes nothing");
    }

    // GF_TABLE_LIST_VIEW (since 1.5) from C#: one column and two items
    // through the table, read back, and the header style as a property.
    private static void list_view_items()
    {
        Abi.check(Exports.get_list_view_table(out gf_list_view_table table));
        expect(table.struct_size >= (uint)sizeof(gf_list_view_table), "the list view table is served whole");
        using ObjectHandle view = ObjectHandle.create("list_view"u8, "csharp.list"u8);
        gf_object made = 0;
        Abi.check(Abi.windows.create(view.value, &made));
        using ObjectHandle window = ObjectHandle.adopt(made);

        fixed (byte* column_id = "name"u8)
        fixed (byte* column_text = "Name"u8)
        fixed (byte* first_id = "first"u8)
        fixed (byte* first_text = "First"u8)
        fixed (byte* second_id = "second"u8)
        fixed (byte* second_text = "Second"u8)
        {
            gf_list_view_column column = default;
            column.struct_size = (uint)sizeof(gf_list_view_column);
            column.stable_id = Utf8.view(column_id, 4);
            column.text = Utf8.view(column_text, 4);
            Abi.check(table.set_columns(view.value, &column, 1u));
            gf_list_view_item* items = stackalloc gf_list_view_item[2];
            items[0] = default;
            items[0].struct_size = (uint)sizeof(gf_list_view_item);
            items[0].stable_id = Utf8.view(first_id, 5);
            items[0].text = Utf8.view(first_text, 5);
            items[1] = items[0];
            items[1].stable_id = Utf8.view(second_id, 6);
            items[1].text = Utf8.view(second_text, 6);
            items[1].@checked = 1u;
            Abi.check(table.set_items(view.value, items, 2u));
        }
        ulong count = 0;
        Abi.check(table.item_count(view.value, &count));
        expect(count == 2u, "two items were set from C#");
        byte[] text = new byte[64];
        gf_list_view_item read = default;
        read.struct_size = (uint)sizeof(gf_list_view_item);
        ulong required = 0;
        fixed (byte* text_data = text)
        {
            Abi.check(table.item_at(view.value, 1u, &read, text_data, (ulong)text.Length, &required));
        }
        expect(read.@checked == 1u && read.text.size == 6u && required == 12u, "the second item reads back checked");

        gf_value style = default;
        style.struct_size = (uint)sizeof(gf_value);
        style.kind = Constants.GF_VALUE_KIND_ENUMERATION;
        style.signed_integer = 2;
        fixed (byte* property = "header_style"u8)
        {
            Abi.check(Abi.properties.set(view.value, Utf8.view(property, 12), &style));
        }
        Abi.check(table.set_selected(view.value, 0u, 1u));
        ulong* selected = stackalloc ulong[2];
        Abi.check(table.selected_items(view.value, selected, 2u, &count));
        expect(count == 1u && selected[0] == 0u, "the first item is selected from C#");
    }

    // A fixed-size array field (gf_abi.h, "Event fields", since 1.5) as the
    // library lends one, a plot's camera target: COLLECTION, three NUMBER
    // records in place. EventFields reads it by name; a wrong count, a wrong
    // element kind and an ordinary collection (no elements in place) are
    // refused with the output untouched.
    private static void event_field_arrays()
    {
        gf_value* elements = stackalloc gf_value[3];
        for (int index = 0; index < 3; ++index)
        {
            elements[index] = default;
            elements[index].struct_size = (uint)sizeof(gf_value);
            elements[index].kind = Constants.GF_VALUE_KIND_NUMBER;
            elements[index].number = 0.25 * (index + 1);
        }
        gf_value target = default;
        target.struct_size = (uint)sizeof(gf_value);
        target.kind = Constants.GF_VALUE_KIND_COLLECTION;
        target.unsigned_integer = 3u;
        target.elements = elements;
        target.element_count = 3u;
        gf_value ordinary = default;
        ordinary.struct_size = (uint)sizeof(gf_value);
        ordinary.kind = Constants.GF_VALUE_KIND_COLLECTION;
        ordinary.unsigned_integer = 2u;
        gf_event_field* fields = stackalloc gf_event_field[2];
        fixed (byte* target_name = "target"u8)
        fixed (byte* previous_name = "previous"u8)
        {
            fields[0] = default;
            fields[0].name = Utf8.view(target_name, 6);
            fields[0].kind = Constants.GF_VALUE_KIND_COLLECTION;
            fields[0].value = &target;
            fields[1] = default;
            fields[1].name = Utf8.view(previous_name, 8);
            fields[1].kind = Constants.GF_VALUE_KIND_COLLECTION;
            fields[1].value = &ordinary;
            gf_event native = default;
            native.struct_size = (uint)sizeof(gf_event);
            native.field_count = 2u;
            native.fields = fields;

            gf_event_field* found = EventFields.named(&native, "target"u8);
            expect(found == &fields[0], "a field is found by its name");
            expect(EventFields.named(&native, "targe"u8) == null, "a prefix of a name is not the field");
            expect(EventFields.fixed_count(found) == 3, "a fixed-size array field lends its count in place");
            Span<double> read = stackalloc double[3];
            expect(EventFields.read_numbers(found, read) && read[0] == 0.25 && read[1] == 0.5 && read[2] == 0.75,
                   "a fixed-size array's numbers are read in order");
            Span<double> short_read = stackalloc double[2] { -1.0, -1.0 };
            expect(!EventFields.read_numbers(found, short_read) && short_read[0] == -1.0,
                   "a fixed-size array is read only with its own count");
            gf_event_field* previous = EventFields.named(&native, "previous"u8);
            expect(EventFields.fixed_count(previous) == -1,
                   "an ordinary collection lends no elements in place (events.field_element reads them)");
            elements[1].kind = Constants.GF_VALUE_KIND_SIGNED;
            Span<double> untouched = stackalloc double[3] { -1.0, -1.0, -1.0 };
            expect(!EventFields.read_numbers(found, untouched) && untouched[0] == -1.0,
                   "an element of another kind refuses the read and leaves the output untouched");
        }
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
        native_exception_reflection();
        subsurfaces();
        binding_list();
        binding_moves_and_filters();
        help_language_files();
        font_families();
        app_font_families();
        content();
        list_view_items();
        event_field_arrays();
        composites();
        expect(unhandled_raised == 0, "no failure in these checks ran under an event loop");
        Console.WriteLine(failures == 0 ? "gui_forms_abi_csharp: all checks passed" :
                                          $"gui_forms_abi_csharp: {failures} checks failed");
        return failures == 0 ? 0 : 1;
    }
}
