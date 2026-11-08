A keyed collection of images that buttons, trees and object views share.

An image list holds a window's icons by key and by index, as a Windows Forms
ImageList does, and adds variants: each entry may hold an image for several
visual states (`ImageVisualState`) and pixel densities, and `resolve` picks
the best one for a state and a display scale. `Button`, `TreeView` and
`ObjectView` take a shared list through `set_image_list` and name entries by
key. The images live in the window's image registry. PNG is the only encoded
format accepted; raw pixels can be registered with
`Window::load_bgra32_premultiplied` and added with `add_image`.

Parameters
----------
window
    The window whose image registry holds the images. The list belongs to
    every window sharing that registry (`Window::resources`) and is used
    only on their UI thread.
image_size
    Logical size, in layout units, at which consumers draw the images; 16 by
    16 by default. Each side must be finite and from 1 to 512, or
    `std::invalid_argument` is thrown.

Properties
----------
bound_window
    The window the list was created for, or null once that window has shut
    down or the list has been disposed. A list whose window has shut down
    keeps working while other windows still share its registry.
image_size : default ``{16.0, 16.0}``
    Logical drawing size of every image, independent of its pixels: a 2x
    variant of a 16 by 16 icon is a 32-pixel image drawn at 16 units.
    Changing it raises `changed` with `ImageListChangeKind::layout`. A side
    outside [1, 512] throws `std::invalid_argument`.
count
    Number of entries, that is, of keys; variants are not counted.
empty
    Whether the list has no entries.
revision : default ``1``
    A counter incremented by every change and carried in each
    `ImageListChange`.
tag
    Application data, kept as given and never interpreted. Setting it raises
    no event.

Events
------
changed
    Raised after every change, with an `ImageListChange` giving the `kind`
    (`added`, `replaced`, `removed`, `renamed`, `layout` or `reset`), the
    entry's `index` and `key` where they apply, and the new `revision`.
    Adding a variant to an existing key reports `added` with that key's
    index. `clear` and disposal report `reset`. A failed load raises nothing.

Methods
-------
belongs_to
    Whether the list's images are in the given window's registry: true for
    the window it was created for and for every window sharing that
    window's `ApplicationResources`. `Button`, `TreeView` and `ObjectView`
    use it to reject a list whose images another window cannot show.
add_png
    `add_png(key, encoded, density_scale = 1.0)` decodes a PNG into the
    window's registry and makes it the `normal` image of `key` at that
    density. A new key becomes a new entry at the end; an existing key gains
    a density variant. It returns the `ImageLoadResult`; when the PNG is
    rejected or the window's image quota is full, nothing changes. It throws
    `std::invalid_argument` for an invalid key or density, or when the key
    already has a `normal` image at that density, and `std::length_error`
    beyond 1024 entries or 32 variants in one entry.
set_variant_png
    `set_variant_png(key, state, density_scale, encoded)` adds or replaces
    the image of an existing key for one visual state and density. When the
    previous image there was loaded by the list, it is replaced in place, and
    on failure it stays as it was. Throws `std::out_of_range` when the key is
    not in the list.
add_image
    `add_image(key, image, density_scale = 1.0)` adds an image that is
    already in the window's registry as the `normal` image of `key`, and
    returns the entry's index. The list borrows the image: removing the entry
    leaves it in the registry. Throws `std::invalid_argument` when the image
    is not in this window's registry, plus the errors of `add_png`.
set_variant_image
    `set_variant_image(key, state, density_scale, image)` is the borrowing
    counterpart of `set_variant_png`. Replacing an image the list had loaded
    removes that image from the window.
key_at
    The key of the entry at an index. Throws `std::out_of_range` past the
    end.
index_of_key
    The index of a key, or an empty optional. Keys compare ignoring ASCII
    case.
contains_key
    Whether a key is in the list, ignoring ASCII case.
set_key_name
    `set_key_name(index, key)` renames an entry and raises `renamed`. The new
    key must not match another entry, ignoring case, or
    `std::invalid_argument` is thrown; an index past the end throws
    `std::out_of_range`.
remove_at
    Removes the entry at an index, with the images the list loaded for it,
    and raises `removed`. Returns false, raising nothing, when the index is
    past the end.
remove_by_key
    `remove_at` for the entry with a key. Returns false when the key is not
    in the list.
clear
    Removes every entry, with the images the list loaded, and raises one
    `reset`. On an empty list it does nothing.
resolve
    `resolve(index_or_key, state = ImageVisualState::normal,
    density_scale = 1.0)` returns the image to draw as an
    `ImageListResolution`, which converts to false when nothing matches. It
    tries the requested state first, then falls back: `pressed` and
    `selected` to `hot` and then `normal`; `hot` and `disabled` to `normal`.
    Within a state it prefers the exact density, then the nearest larger,
    then the nearest smaller. The result reports the state and density
    actually chosen and the image's size in source pixels. It never throws.
clear_tag
    Empties `tag`.

See Also
--------
Button : A push button that can show an image from a list.
TreeView : A hierarchical list whose nodes can show images from a list.
PictureBox : Shows one registered image.

Notes
-----
Keys must be nonempty, valid UTF-8 and at most 256 bytes, and are unique
ignoring ASCII case; the spelling first given is kept. Density scales must be
finite and from 0.5 to 8. An invalid key or density throws
`std::invalid_argument`.

Images loaded by `add_png` and `set_variant_png` belong to the list and are
removed from the window when their entry is removed and when the list is
cleared or disposed. Images added with `add_image` and `set_variant_image`
are borrowed and stay in the registry. `resolve` skips a variant whose image
is no longer in the registry.

Every member that changes the list must be called on the window's UI thread,
and throws `std::logic_error` from another thread, after disposal and once
no window uses its registry any more. Dispose the list to release its images at a known
moment; disposal raises `changed` with `reset`. A list destroyed without
disposal on the UI thread still releases its images, but raises no event.

Examples
--------
Register raw pixels with the window, add them under a key and resolve a
state that has no image of its own:

.. code-block:: cpp

   std::shared_ptr<gf::Control> root = gf::make_control<gf::Control>(gf::StableId("app"));
   gf::Window window(root, gf::Size{200.0, 100.0});
   const std::array<std::byte, 4> red{std::byte{0}, std::byte{0}, std::byte{255},
                                      std::byte{255}};   // one BGRA pixel
   gf::ImageLoadResult pixel = window.load_bgra32_premultiplied(1U, 1U, 4U, red);

   std::shared_ptr<gf::ImageList> icons = std::make_shared<gf::ImageList>(window);
   static_cast<void>((*icons).add_image("Open", pixel.image));
   gf::ImageListResolution hot = (*icons).resolve("open", gf::ImageVisualState::hot);
   // hot.image == pixel.image, and hot.resolved_state is ImageVisualState::normal

Share the list with a button, which draws the keyed image at `image_size`
beside its caption:

.. code-block:: cpp

   std::shared_ptr<gf::Button> open =
       gf::make_control<gf::Button>(gf::StableId("toolbar.open"), "Open");
   (*open).set_requested_bounds({0.0, 0.0, 120.0, 32.0});
   gf::Window window(open, gf::Size{120.0, 32.0});
   const std::array<std::byte, 4> blue{std::byte{200}, std::byte{80}, std::byte{20},
                                       std::byte{255}};
   gf::ImageLoadResult pixel = window.load_bgra32_premultiplied(1U, 1U, 4U, blue);

   std::shared_ptr<gf::ImageList> icons = std::make_shared<gf::ImageList>(window);
   static_cast<void>((*icons).add_image("open", pixel.image));
   (*open).set_image_list(icons);
   (*open).set_image_key("open");
   (*open).text_image_relation = gf::TextImageRelation::image_before_text;
