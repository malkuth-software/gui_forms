# LinkPage: a retained custom control

The example control of [Custom controls](../../docs/user/custom-controls.md):
a scrolling page of wrapped paragraphs with links, painted by the control
itself.

- `ensure_layout` flows the text, and only when its key changes: the
  content revision, the flow width, the font, the text engine or the scale.
- `arrange` brings the layout up to date and lets `ScrollableControl` place
  the bars and keep the scroll position, from `painted_content_extent`.
- Paint draws the kept lines the damage crosses; hit testing finds them the
  same way, in control coordinates (`MouseEventArgs::location`).
- Hover and press repaint the link they touch and nothing else.

`tests/link_page_example_tests.cpp` in the tests repository drives it
headless over 2,000 paragraphs: 100 pointer moves over the scrolled page lay
out nothing, scrolling flows nothing, a resize flows once, and hover and
click land on the right link beside a side panel.

```sh
cmake -S examples/link_page -B build/link-page -DCMAKE_PREFIX_PATH="$PWD/build/install" \
  -DGUI_FORMS_EXAMPLE_NATIVE=ON
cmake --build build/link-page
build/link-page/link_page
```

The template harness's app template carries a copy under
`templates/app/reference/`; change both together.
