A box that displays one image from its window's image registry.

Load an image into the window, for example with `Window::load_png`, and give
the returned `ImageId` to `image`; `size_mode` decides whether the image
keeps its size, stretches, is centred, is scaled to fit, or sizes the box. The
box is for display only: it takes no focus or input, and pointer events pass
through to the control underneath. Use a `Button` with an `image` for a
clickable picture.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.

Properties
----------
image : default ``ImageId{}``
    The displayed image. The box stores only the ID; the window's registry
    owns the pixels. An ID that is not, or is no longer, live in the box's
    window is accepted and simply draws nothing. Setting a different ID
    raises `image_changed`.
size_mode : default ``PictureBoxSizeMode::normal``
    `normal` draws the image at its pixel size from the top-left corner;
    `stretch_image` stretches it to fill the box; `center_image` centres it
    at its pixel size; `zoom` scales it to fit inside the box, keeping its
    aspect ratio, and centres it; `auto_size` draws it like `normal` and
    makes the box's preferred size the image's size plus the border.
image_opacity : default ``1.0``
    Opacity of the image, from 0 to 1; other values throw
    `std::invalid_argument`. Zero draws no image.
has_valid_image
    Whether the box is in a window and `image` is live in that window's
    registry.
image_size
    The image's size in pixels, or zero when there is no valid image.
image_bounds
    Where the image is drawn, in the box's coordinates, for the current
    `size_mode`; empty when there is no valid image.

Events
------
image_changed
    Raised after `image` changes. It carries the new ID.

Methods
-------
clear_image
    Sets `image` to none, which raises `image_changed` if an image was set.

See Also
--------
ImageList : Image sets with state and density variants, for buttons.
Button : A push button that can show an image.
Panel : The container `PictureBox` derives from.

Notes
-----
The box fills itself with its background, opaque white unless you call
`set_background`, and then draws the image inside its border. A border style other than `BorderStyle::none`
insets the image by one unit on each side.

Except in `auto_size` mode the box does not size itself to its image: its
preferred size is its requested bounds, so give it a size.

Removing an image from the window while a box shows it is safe: the box stops
drawing it and reports no valid image.

The control publishes the accessibility role `image`. Its name and
description are the accessible name and description, and its value is the
image's pixel size, such as `"4 x 2"`. It is exposed when it has a valid
image or a name or description, so give a meaningful picture an accessible
name by writing `accessible_name`.

Examples
--------
Show an image scaled to fit, keeping its aspect ratio:

.. code-block:: cpp

   std::shared_ptr<gf::PictureBox> preview =
       gf::make_control<gf::PictureBox>(gf::StableId("preview"));
   (*preview).set_requested_bounds({0.0, 0.0, 100.0, 100.0});
   (*preview).size_mode = gf::PictureBoxSizeMode::zoom;
   (*preview).accessible_name = "Preview";
   gf::Window window(preview, gf::Size{100.0, 100.0});
   const std::vector<std::byte> pixels(4U * 2U * 4U, std::byte{0xff});   // 4 x 2, white
   const gf::ImageLoadResult loaded = window.load_bgra32_premultiplied(4U, 2U, 16U, pixels);
   (*preview).image = loaded.image;
   window.perform_layout();
   // (*preview).image_bounds() is now {0, 25, 100, 50}
