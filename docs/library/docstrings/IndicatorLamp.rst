An indicator lamp: a round lens in a bezel that shows off, on or error,
with an optional legend beside it, such as a coolant or fault lamp on a
machine panel.

Every theme can show a lamp. The default theme and the familiar themes
style its lens through ``indicator-lamp::part(lens):state(on)`` and
``:state(error)`` rules, and a theme without such rules draws the lit lens
with its built-in recipes (a progress fill for on, a notice for error), so
on, error and off stay distinct in any theme. A change of state warms the
lens up or cools it down over a few frames, and at once under reduced
motion.

A lamp can also show baked light: name a bake with `light_planes` and the
lens is composited from it, read through the window's resources
(`ApplicationResources::set_light_plane_directory`), with the bake's lamps
lit in `lamp_color` at the warm-up's power, so one bake serves every
colour. Where the bake is missing, and in forced high contrast, the
theme's lens is drawn instead.

Parameters
----------
stable_id
    Identity of the control, unique within its window.
text
    The legend beside the lens; empty for a lens alone.

Properties
----------
state : default ``IndicatorLampState::off``
    Whether the lamp is off, on or showing an error.
text : default empty
    The legend beside the lens. It names the lamp to assistive technology
    unless an accessible name is set. Invalid UTF-8 throws
    `std::invalid_argument`.
light_planes : default empty
    The baked light planes the lens is composited from, by name: a bake in
    the windows' light-plane directory (``<directory>/<name>.f32``). A
    name is 1 to 64 lower-case letters, digits and ``_``; anything else
    throws `std::invalid_argument`. With the bake present the lens takes
    the bake's aspect (the largest such rectangle in the client area
    without a legend, else in the bezel's square) and its whole look, its
    body included, and the bezel gives way.
lamp_color : default empty
    The colour the lens lights in while on, as sRGB. With a bake it is the
    bake's lamp light at unit power (`engine_light::lamp_colour_from_srgb`),
    lamp amber when empty; without one it fills the lit lens over the
    theme's look as an authored colour, and not in forced high contrast.
    An error always lights in the error colour (with a bake, lamp red).
light_planes_shown
    Whether the lens is composited from its bake now.
power
    How lit the lens is now, from 0 to 1: the warm-up's or cool-down's
    current power, or the state's settled one.
changing
    Whether a warm-up or cool-down is running.

See Also
--------
Badge : A count or dot beside what it counts.

Notes
-----
The lamp is never clickable and takes no focus: the pointer reaches what
lies beneath it. It lights only its own lens: the lit lens is drawn
clipped to the lens, so its light reaches neither the bezel, the legend nor
a neighbour. The parts are ``bezel`` (a well), ``lens`` (an indicator) and
``label``; the custom states ``on`` and ``error`` name the lit states. It
publishes the accessibility role `static_text`, named by its accessible
name or else its legend, with the state as its value (``off``, ``on`` or
``error``), so the state never rests on colour alone. The warm-up is the
light planes' strike envelope (`engine_light::lamp_power`) and the
cool-down `engine_light::lamp_cooling_power`. A baked lens is one part
(`PartPaint::light_planes`), composited at the window's scale and kept
between paints (`engine_light::LightPlaneRaster`); it is transparent where
the bake's coverage is 0, so its light never reaches past the lens.

Examples
--------
A fault lamp that lights red when a check fails:

.. code-block:: cpp

   std::shared_ptr<gf::IndicatorLamp> fault =
       gf::make_control<gf::IndicatorLamp>(gf::StableId("panel.fault"), "Fault");
   (*fault).state = gf::IndicatorLampState::error;
   // fault->semantic_descriptor().value is "error"
