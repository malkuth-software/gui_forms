# Properties

A property is a member you assign to and read from. Controls are held by
`std::shared_ptr`, so a property is reached through the pointer:

```cpp
(*ok).text = "OK";                                 // the write
const std::string caption = (*ok).text.read();     // the read
(*ok).enabled = false;
(*ok).dock = gf::DockStyle::bottom;
```

- **One write path.** Every write compares, stores if different, marks the
  control dirty and raises the property's change event (`text_changed()`,
  `visible_changed()`). An equal write does nothing.
- **One read.** `.read()` returns the declared type. There is no conversion
  operator and no `==` on a property, so a forgotten `.read()` does not
  compile.
- **Views.** A value owned elsewhere, such as a text box's text, has the same
  spelling; its read returns a copy.
- **Collections.** A list's `items` is assigned whole or edited in place:
  `(*list).items.append("Violet");`.
- **Child properties.** A container gives each direct child a property:
  `(*table).area[*ok] = "footer";`, `(*table).cell[*title] = gf::GridCell{.row = 0U, .column = 0U};`,
  `(*flow).flow_break[*button] = true;`.
- **Conversions.** `(*ok).visible.as_type(gf::types::string)` converts at
  compile time (total conversions only); `as_type(property_type)` returns
  `std::optional<gf::BindingValue>` for a type known at run time.
  `type()` describes the property; it is not a read.
- **By name**, for tools: `property_value("padding")`,
  `set_property_value("padding", value)`.

There are no getter and setter pairs for properties. Members that stay
functions are operations (`perform_click()`), structure (`add_child`),
records with identities (`set_items` of menus and strips) and writes that
return a result.

Full page: [Properties](https://gui-forms.readthedocs.io/user/properties.html).
