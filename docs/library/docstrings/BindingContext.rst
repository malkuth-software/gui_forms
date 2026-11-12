A registry of the binding sources used in one window and their currency managers.

A binding context gives code that works with several sources, such as a
navigator bar or a form designer, one place to find the `CurrencyManager` of
each `BindingSource` in a window, as a Windows Forms BindingContext does. It
registers a source the first time it is asked for its manager and reports
registrations and removals through `collection_changed`. The context holds
its sources weakly and forgets a source when the source is disposed. Each
source owns its own currency manager, so a context is optional: code that
already holds a source can use `BindingSource::currency_manager` directly.

Parameters
----------
window
    The window whose sources the context registers. Construct the context on
    that window's UI thread.

Properties
----------
size
    The number of registered sources.

Events
------
collection_changed
    Raised after a source is registered or removed. The
    `BindingContextChange` carries a pointer to the source and whether it was
    added. When a source is disposed, its removal is raised while it is being
    disposed; do not use the pointer after the handler returns.

Methods
-------
add
    Registers a source, as `manager` does, without returning its manager.
manager
    `manager(source)` returns the source's `CurrencyManager`, registering the
    source and raising `collection_changed` the first time. The source must
    be alive and belong to the context's window, or `std::invalid_argument`
    is thrown; a disposed context, or one whose window has shut down, throws
    `std::logic_error`.
contains
    Whether a source is registered.
remove
    Unregisters a source and raises `collection_changed`. Returns false when
    the source was not registered.
clear
    Unregisters every source, raising `collection_changed` once for each, in
    no particular order.

See Also
--------
BindingSource : The rows whose currency managers the context holds.
Binding : Connects a control property to a field of a source.

Notes
-----
Registering a source again does nothing and raises nothing. The context never
keeps a source alive; destroying a source disposes it, which removes it from
the context.

Use the context on its window's UI thread; `manager`, `add`, `remove` and
`clear` throw `std::logic_error` from another thread. Disposal unregisters
every source without raising `collection_changed`, and destroying an
undisposed context disposes it.

Examples
--------
Register a source and move its current record through the context:

.. code-block:: cpp

   std::shared_ptr<gf::Control> root = gf::make_control<gf::Control>(gf::StableId("app"));
   gf::Window window(root, gf::Size{200.0, 100.0});
   std::shared_ptr<gf::BindingSource> orders = std::make_shared<gf::BindingSource>(window);
   gf::BindingTable& table = *(*orders).table();
   static_cast<void>(table.add_column(gf::BindingColumn{"number", gf::BindingValueKind::signed_integer}));
   table.assign({gf::BindingRow{"order.1001", {std::int64_t{1001}}},
                 gf::BindingRow{"order.1002", {std::int64_t{1002}}}});

   gf::BindingContext context(window);
   gf::CurrencyManager& manager = context.manager(orders);   // registers orders
   static_cast<void>(manager.set_position(1));
   // (*orders).position() is 1, and context.contains(*orders) is true

Track the registered sources with an owned handler:

.. code-block:: cpp

   struct SourceCounter final : gf::Component {
       int sources = 0;
       void on_change(const gf::BindingContextChange& change) {
           sources += change.added ? 1 : -1;
       }
   };

   void example() {
       std::shared_ptr<gf::Control> root = gf::make_control<gf::Control>(gf::StableId("app"));
       gf::Window window(root, gf::Size{200.0, 100.0});
       std::shared_ptr<gf::BindingSource> customers = std::make_shared<gf::BindingSource>(window);

       gf::BindingContext context(window);
       SourceCounter counter;
       gf::on(context.collection_changed(), counter, &SourceCounter::on_change);
       context.add(customers);        // counter.sources is 1
       (*customers).dispose();        // counter.sources is 0
   }
