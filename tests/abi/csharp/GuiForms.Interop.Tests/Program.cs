// The C# interop against the built library: the generated layout against the
// manifest, negotiation, an object handle's life, a refusal as an exception,
// and the callback thunks. A handler that throws is caught by its thunk under
// the library's frames, which it snapshots, and the thread's depth is as it
// was once the call returns (the C kit checks the same balance for C
// callbacks; tests/shadow_frames_tests.cpp for C++ unwinding).
using System;
using System.Text;
using GuiForms.Interop;

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
        expect(unhandled_raised == 0, "no failure in these checks ran under an event loop");
        Console.WriteLine(failures == 0 ? "gui_forms_abi_csharp: all checks passed" :
                                          $"gui_forms_abi_csharp: {failures} checks failed");
        return failures == 0 ? 0 : 1;
    }
}
