# Control schema

Generated from `manifests/control-schema.json` by `python3 tools/control_schema.py render`;
do not edit. The manifest is written by `gui_forms_gallery_pages_tests` from the run-time
registry (stage T1), so this page lists what the library publishes.

Every name here is a registered name, stored in snake_case (owner decision 8). Theme
and form documents, which are CSS-family, write the same words in kebab-case
(`scroll_thumb` is `::part(scroll-thumb)`, `auto_size_mode` is `auto-size-mode`);
PropertyGrid and C# show them in PascalCase (`AutoSizeMode`). C++ reads the anatomy as
typed records (`(*bar).parts().thumb`) and registers handlers through each type's events
(`(*button).clicked()`). An interaction's payload is the handler's argument types; *by
reference* marks a payload the handler receives by mutable reference.

Every control also has these properties: `accessible_description`, `accessible_name`, `align_self`, `allow_drop`, `anchor`, `auto_scroll_offset`, `auto_size`, `auto_size_mode`, `back_color`, `bounds`, `causes_validation`, `classes`, `cursor`, `dock`, `enabled`, `focusable`, `fore_color`, `help`, `help_line`, `hit_test_transparent`, `justify_self`, `keywords`, `layout_direction`, `margin`, `maximum_size`, `minimum_size`, `name`, `padding`, `right_to_left`, `tab_index`, `tab_stop`, `tool_tip_text`, `visible`.

Every control also raises these interactions: `name_changed`, `visible_changed`, `enabled_changed`, `right_to_left_changed`, `auto_size_changed`, `causes_validation_changed`, `validating`, `validated`, `tool_tip_text_changed`, `initialization_completed`, `pointer_observed`, `pointer_preview_observed`, `mouse_down`, `mouse_up`, `mouse_move`, `mouse_wheel`, `mouse_enter`, `mouse_hover`, `mouse_leave`, `mouse_capture_changed`, `click`, `mouse_click`, `double_click`, `mouse_double_click`, `context_menu_requested`, `focus_observed`, `arranged_bounds_changed`, `help_requested`, `preview_key_down`, `key_down`, `key_press`, `key_up`.

## Vocabulary

The atoms controls declare and themes address (design language §8.5 and §12.7).

Part roles: `region`, `shadow`, `face`, `edge`, `keyline`, `perimeter`, `sheen`, `seam`, `divider`, `caption`, `cap`, `well`, `track`, `fill`, `thumb`, `actuator`, `indicator`, `activity`, `glyph`, `icon`, `label`, `content`, `caret`, `disclosure`, `grip`, `chip`, `focus`, `preselect`, `selection`, `validation`, `drop`, `image`, `plot`, `graticule`, `trace`, `axis`, `tick`, `tick_label`, `grid_major`, `grid_minor`, `legend`, `legend_entry`, `marker`, `crosshair`, `annotation`, `color_scale`, `series`, `arc`, `needle`, `beak`, `scrim`.

States: `normal`, `hot`, `pressed`, `captured`, `focused`, `focus_visible`, `preselect`, `selected`, `checked`, `indeterminate`, `current`, `active_tool`, `enabled`, `disabled`, `unavailable`, `read_only`, `invalid`, `pending`, `open`, `closed`, `expanding`, `collapsing`, `interrupted`, `drop_accepted`, `drop_rejected`, `key`, `secondary_participating`, `drag_proximate`, `deactivated`, `high_contrast`, `reduced_motion`.

Admitted custom states: `modified`, `pinned`.

Marks: `check`, `indeterminate`, `radio_dot`, `toggle_knob`, `arrow_up`, `arrow_down`, `arrow_left`, `arrow_right`, `chevron_down`, `chevron_right`, `close`, `grip_dots`, `dialog_launcher`, `more`, `overflow`, `customize`, `pin`, `dock_shape`, `underline`, `pencil`, `asterisk`, `error`, `search`, `information`, `warning`, `success`.

Engine inputs: `pointer_x`, `pointer_y`, `time_in_state`, `display_scale`, `text_scale`.

Values: `accent_index`, `text_orientation`, `dock_region`, `content_color`, `authored_color`, `text_tone`, `pointed_face`, `part_profile`, `arc_angles`.

Recipe families: `window`, `panel`, `card`, `button`, `accent_button`, `command_button`, `choice`, `editor`, `menu_item`, `selection`, `progress`, `tool_tip`, `link`, `visited_link`, `notice`, `badge`.

Semantic roles: `generic`, `group`, `static_text`, `button`, `check_box`, `radio_button`, `link`, `text_box`, `list`, `list_item`, `check_list_item`, `combo_box`, `slider`, `scroll_bar`, `progress_bar`, `image`, `tab_group`, `tab`, `split_pane`, `numeric_field`, `tool_tip`, `date_picker`, `calendar`, `date_cell`, `property_grid`, `property_group`, `property_row`, `menu_bar`, `menu_bar_item`, `menu`, `menu_item`, `separator`, `toolbar`, `radio_group`, `split_button`, `menu_button`, `pane`, `tab_panel`, `tree`, `tree_item`, `table`, `row`, `cell`, `column_header`, `row_header`, `tree_grid`, `dialog`, `status_bar`, `live_region`, `meter`, `toggle_switch`, `search_box`, `figure`.

Semantic actions: `focus`, `press`, `select`, `increment`, `decrement`, `expand`, `collapse`, `show_menu`, `set_value`, `close`.

Semantic states: `enabled`, `visible`, `focusable`, `focused`, `selected`, `checked`, `mixed`, `read_only`, `expanded`, `busy`, `protected_content`, `invalid`, `checkable`, `multiselectable`.

Semantic node fields: `position_in_set`, `set_size`, `has_popup`, `controlled_panel`, `orientation`, `level`, `modal`, `row_index`, `column_index`, `row_count`, `column_count`, `access_key`, `small_step`, `large_step`, `text_selection`, `multiline`, `tool_tip`, `announces_on_appearance`.

## Engine

Theme engine 0.3 (T2): the primitives a theme's rules may select, the atoms each declares against, and its cost. Cascade levels, lowest first: `built_in`, `base_sheet`, `user_theme`, `theme_sheet`, `class_rule`, `stable_id_rule`, `inline_style`, `important_base_sheet`, `important_user_theme`, `important_theme_sheet`, `forced_high_contrast`, `important_inline_style`.

| Primitive | Since | Paints | Reads | Cost per device pixel | Parameters |
|---|---|---|---|---|---|
| `bulge` | 0.1 | `face` | `pressed`, `active_tool` | 320 | `amplitude`, `exponent`, `light_colour`, `light_peak`, `shade_colour`, `shade_peak`, `crown_colour`, `crown_peak`, `gold_light_colour`, `gold_light_peak`, `gold_shade_colour`, `gold_shade_peak`, `gold_crown_colour`, `gold_crown_peak` |
| `dithered_bulge` | 0.1 | `face` | `pressed`, `active_tool` | 280 | `amplitude`, `exponent`, `lit_1`, `lit_2`, `lit_3`, `shaded_1`, `shaded_2`, `shaded_3`, `lit_gain`, `shade_gain`, `crown_gain`, `gold_lit_1`, `gold_lit_2`, `gold_lit_3`, `gold_body_1`, `gold_body_2`, `gold_body_3`, `gold_shaded_1`, `gold_shaded_2`, `gold_shaded_3` |
| `pill_plate` | 0.2 | `face`, `actuator`, `thumb`, `chip` | `hot`, `pressed`, `disabled`, `preselect` | 256 | `shape`, `corner_radius`, `face_top`, `face_bottom`, `border`, `upper_line`, `lower_line`, `contact_shadow`, `hot_face_top`, `hot_face_bottom`, `hot_border`, `pressed_face_top`, `pressed_face_bottom`, `pressed_upper_line`, `pressed_lower_line`, `disabled_face_top`, `disabled_face_bottom`, `disabled_border`, `disabled_upper_line`, `perimeter`, `text_padding` |
| `gold_plate` | 0.2 | `face`, `actuator`, `chip` | `pressed` | 192 | `shape`, `corner_radius`, `face_top`, `face_bottom`, `border`, `pressed_face_top`, `pressed_face_bottom`, `upper_line`, `left_line`, `lower_line`, `text_padding` |
| `inset_well` | 0.2 | `well`, `track`, `plot` | none | 160 | `shape`, `corner_radius`, `face_top`, `face_bottom`, `border`, `inner_shade`, `outer_highlight`, `text_padding` |
| `etched_divider` | 0.2 | `divider`, `graticule`, `axis`, `tick`, `grid_major`, `grid_minor`, `crosshair` | none | 32 | `shade_line`, `light_line` |
| `seam_line` | 0.2 | `seam` | none | 16 | `line` |
| `grip` | 0.2 | `grip` | none | 16 | `dot` |
| `material` | 0.2 | `region`, `face`, `actuator`, `well`, `track`, `thumb`, `chip`, `plot` | `hot`, `pressed`, `captured`, `selected`, `checked`, `current`, `open`, `active_tool`, `focus_visible`, `preselect`, `disabled`, `unavailable` | 512 | `recipe`, `shape` |
| `overlay_shadow` | 0.2 | `shadow` | none | 512 | none |
| `sheen` | 0.2 | `sheen` | none | 16 | `colour`, `width`, `rest` |
| `glass` | 0.2 | `region`, `face`, `well`, `plot` | none | 48 | `blur`, `tint`, `corner`, `recipe`, `layers` |

Bounded expressions: at most 64 operations and a stack of 16, from `constant`, `input`, `state`, `parameter`, `negate`, `add`, `subtract`, `multiply`, `divide`, `minimum`, `maximum`, `less`, `less_equal`, `greater`, `greater_equal`, `equal`, `not_equal`, `clamp`, `mix`, `smoothstep`, `choose`.

Transitions run on `opacity`, `scale`, `face`, `translate_x`, `translate_y` and on the primitives' animated parameters: at most 8 states a machine and 6 transitions a part. Ambient effects: at most 30 frames per second over at most 0.1 of the window on everyday surfaces.

## `button_base` (ButtonBase, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `icon` | icon |  |  |
| `label` | label |  |  |
| `focus` | focus |  |  |

States: `normal`, `hot`, `pressed`, `enabled`, `disabled`, `focused`, `focus_visible`, `open`, `closed`.

Properties beyond every control's: `content_padding` (insets), `font` (font), `image` (image), `text` (text), `text_alignment` (enumeration).

| Interaction | Payload |
|---|---|
| `expanded_changed` | `std::optional<bool>` |
| `clicked` | `ButtonBase`, by reference |
| `text_changed` | `std::string` |

## `button` (Button, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `icon` | icon |  |  |
| `label` | label |  |  |
| `preselect` | preselect |  |  |
| `focus` | focus |  |  |

States: `normal`, `hot`, `pressed`, `preselect`, `selected`, `enabled`, `disabled`, `focused`, `focus_visible`, `open`, `closed`.

Properties beyond every control's: `content_padding` (insets), `flat_border_width` (number), `font` (font), `image` (image), `text` (text), `text_alignment` (enumeration), `visual_style` (enumeration).

| Interaction | Payload |
|---|---|
| `expanded_changed` | `std::optional<bool>` |
| `clicked` | `ButtonBase`, by reference |
| `text_changed` | `std::string` |
| `dialog_result_changed` | `DialogResult` |
| `selected_changed` | `bool` |

## `drop_down_button` (DropDownButton, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `icon` | icon |  |  |
| `label` | label |  |  |
| `drop_down` | actuator |  | pressed |
| `disclosure` | disclosure |  |  |
| `preselect` | preselect |  |  |
| `focus` | focus |  |  |

States: `normal`, `hot`, `pressed`, `preselect`, `selected`, `enabled`, `disabled`, `focused`, `focus_visible`, `open`, `closed`.

Properties beyond every control's: `content_padding` (insets), `flat_border_width` (number), `font` (font), `image` (image), `text` (text), `text_alignment` (enumeration), `visual_style` (enumeration).

| Interaction | Payload |
|---|---|
| `expanded_changed` | `std::optional<bool>` |
| `clicked` | `ButtonBase`, by reference |
| `text_changed` | `std::string` |
| `dialog_result_changed` | `DialogResult` |
| `selected_changed` | `bool` |
| `drop_down_requested` | `DropDownButton`, by reference |
| `drop_down_close_requested` | `DropDownButton`, by reference |

## `link_label` (LinkLabel, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `label` | label |  |  |
| `underline` | glyph |  |  |
| `focus` | focus |  |  |

States: `normal`, `hot`, `pressed`, `enabled`, `disabled`, `focused`, `focus_visible`, `open`, `closed`. Application-defined: `visited`.

Properties beyond every control's: `content_padding` (insets), `font` (font), `image` (image), `text` (text), `text_alignment` (enumeration).

| Interaction | Payload |
|---|---|
| `expanded_changed` | `std::optional<bool>` |
| `clicked` | `ButtonBase`, by reference |
| `text_changed` | `std::string` |

## `command_bar` (CommandBar, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `invoked` | `CommandItem` |

## `text_box` (TextBox, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `well` | well |  |  |
| `content` | content |  |  |
| `caret` | caret |  |  |
| `selection` | selection |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`, `read_only`, `invalid`, `pending`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `multiline` (boolean), `placeholder_text` (text), `read_only` (boolean), `text` (text), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `text_changed` | `std::string` |
| `selection_changed` | `TextSelection` |
| `committed` | `std::string` |
| `cancelled` | none |
| `auto_complete_open_changed` | `bool` |
| `auto_complete_accepted` | `std::string` |

## `token_box` (TokenBox, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `well` | well |  |  |
| `field` | well | `text_box` |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `token`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `token_face` | chip |  |  |
| `token_label` | label |  |  |
| `token_close` | glyph |  | hot, pressed |
| `token_focus` | focus |  |  |

Item states: `selected`, `hot`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `token_added` | `Token` |
| `token_removed` | `Token` |
| `selected_token_changed` | `std::optional<TokenId>` |

## `search_field` (SearchField, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `well` | well |  |  |
| `icon` | glyph |  |  |
| `content` | content | `text_box` |  |
| `actuator` | glyph |  | hot, pressed |
| `activity` | activity |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`, `pending`.

Properties beyond every control's: `busy` (boolean), `placeholder_text` (text), `text` (text).

| Interaction | Payload |
|---|---|
| `text_changed` | `std::string` |
| `search_requested` | `std::string` |
| `cleared` | none |

## `masked_text_box` (MaskedTextBox, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `field` | well | `text_box` |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`, `read_only`, `invalid`, `pending`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `mask_input_rejected` | `MaskInputRejection` |
| `text_changed` | `std::string` |
| `mask_changed` | `std::string` |
| `overwrite_mode_changed` | `bool` |
| `type_validation_completed` | `MaskValidation` |

## `rich_text_box` (RichTextBox, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `well` | well |  |  |
| `bar` | region |  |  |
| `content` | content |  |  |
| `caret` | caret |  |  |
| `selection` | selection |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`, `read_only`, `invalid`, `pending`.

Item system `bar_button`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `button` | face |  |  |
| `button_label` | label |  |  |
| `button_glyph` | glyph |  |  |

Item states: `hot`, `pressed`, `checked`, `indeterminate`, `disabled`, `open`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `text` (text), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `text_changed` | `std::string` |
| `content_changed` | none |
| `selection_changed` | `TextSelection` |
| `link_clicked` | `std::string` |

## `label` (Label, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `label` | label |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `alignment` (enumeration), `font` (font), `maximum_lines` (unsigned_integer), `text` (text), `text_wrapping` (enumeration), `vertical_alignment` (enumeration).

| Interaction | Payload |
|---|---|
| `text_changed` | `std::string` |

## `text_block` (TextBlock, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `text` | label |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `font` (font), `line_height` (number), `text` (text), `text_align` (enumeration).

| Interaction | Payload |
|---|---|
| `text_changed` | `std::string` |

## `badge` (Badge, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `chip` | chip |  |  |
| `label` | label |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`. Application-defined: `dot`.

Properties beyond every control's: `count` (unsigned_integer), `maximum` (unsigned_integer), `mode` (enumeration), `show_zero` (boolean).

Interactions beyond every control's: none.

## `info_banner` (InfoBanner, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `keyline` | keyline |  |  |
| `icon` | glyph |  |  |
| `caption` | caption |  |  |
| `label` | label |  |  |
| `actuator` | glyph |  | hot, pressed |
| `content` | content |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`. Application-defined: `information`, `warning`, `success`, `error`.

Properties beyond every control's: `closable` (boolean), `message` (text), `severity` (enumeration), `title` (text).

| Interaction | Payload |
|---|---|
| `closed` | none |

## `empty_state` (EmptyState, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `icon` | icon |  |  |
| `caption` | caption |  |  |
| `label` | label |  |  |
| `content` | content |  |  |

States: `normal`, `enabled`, `disabled`.

Properties beyond every control's: `description` (text), `image` (image), `title` (text).

Interactions beyond every control's: none.

## `toast` (Toast, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `icon` | glyph |  |  |
| `label` | label |  |  |
| `actuator` | actuator | `button` |  |
| `close` | glyph |  | hot, pressed |

States: `normal`, `enabled`, `disabled`. Application-defined: `information`, `warning`, `success`, `error`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `action_invoked` | none |

## `picture_box` (PictureBox, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `image` | icon |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `image` (image), `image_opacity` (number), `size_mode` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `image_changed` | `ImageId` |

## `progress_bar` (ProgressBar, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `track` | track |  |  |
| `fill` | fill |  |  |
| `activity` | activity |  |  |

States: `normal`, `indeterminate`, `enabled`, `disabled`, `reduced_motion`.

Properties beyond every control's: `effect` (text), `effect_input` (enumeration), `effect_seed` (unsigned_integer), `maximum` (number), `minimum` (number), `value` (number), `visual_style` (enumeration).

| Interaction | Payload |
|---|---|
| `range_changed` | `double, double` |
| `value_changed` | `double` |
| `scroll` | `RangeScrollEvent` |

## `progress_ring` (ProgressRing, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `track` | track |  |  |
| `fill` | fill |  |  |
| `activity` | activity |  |  |

States: `normal`, `indeterminate`, `enabled`, `disabled`, `reduced_motion`.

Properties beyond every control's: `indeterminate` (boolean), `maximum` (number), `value` (number).

Interactions beyond every control's: none.

## `spinner` (Spinner, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `activity` | activity |  |  |

States: `normal`, `enabled`, `disabled`, `reduced_motion`.

Properties beyond every control's: `active` (boolean).

Interactions beyond every control's: none.

## `sweep_scope` (SweepScope, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `caption` | caption |  |  |
| `plot` | well |  |  |
| `graticule` | divider |  |  |
| `indicator` | indicator |  |  |
| `activity` | activity |  |  |

States: `normal`, `enabled`, `disabled`, `reduced_motion`.

Properties beyond every control's: `caption` (text), `maximum` (number), `minimum` (number), `sweep_period` (number), `value` (number).

| Interaction | Payload |
|---|---|
| `range_changed` | `double, double` |
| `value_changed` | `double` |
| `scroll` | `RangeScrollEvent` |

## `sparkline` (Sparkline, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `plot` | plot |  |  |
| `series` | series |  |  |
| `marker` | marker |  |  |

States: `normal`, `enabled`, `disabled`.

Properties beyond every control's: `capacity` (unsigned_integer), `fill` (boolean).

Interactions beyond every control's: none.

## `plot_view` (PlotView, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `title` | caption |  |  |
| `plot` | plot |  |  |
| `x_axis` | axis |  |  |
| `y_axis` | axis |  |  |
| `legend` | legend |  |  |
| `crosshair` | crosshair |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `crosshair` (boolean), `live` (boolean), `show_grid` (boolean), `show_legend` (boolean), `title` (text).

| Interaction | Payload |
|---|---|
| `view_changed` | `PlotViewRanges` |
| `selection_changed` | `PlotQuery` |
| `point_picked` | `PlotPointPick` |
| `legend_toggled` | `std::size_t` |

## `plot_axis_item` (PlotAxisItem, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_fit` (boolean), `direction` (enumeration), `follow_span` (number), `label` (text), `maximum` (number), `minimum` (number), `scale` (enumeration).

Interactions beyond every control's: none.

## `plot_series_item` (PlotSeriesItem, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `hidden` (boolean), `kind` (enumeration), `label` (text), `source` (text), `x_axis` (unsigned_integer), `y_axis` (unsigned_integer).

Interactions beyond every control's: none.

## `plot_annotation_item` (PlotAnnotationItem, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `text` (text), `x` (number), `y` (number).

Interactions beyond every control's: none.

## `list_box` (ListBox, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `well` | well |  |  |
| `content` | content |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `row` | region |  |  |
| `label` | label |  |  |
| `row_focus` | focus |  |  |

Item states: `selected`, `hot`, `current`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `items` (collection), `selection_mode` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `selection_changed` | `ListSelectionChange` |
| `item_activated` | `std::size_t` |

## `checked_list_box` (CheckedListBox, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `well` | well |  |  |
| `content` | content |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `row` | region |  |  |
| `label` | label |  |  |
| `row_focus` | focus |  |  |
| `check_well` | well |  |  |
| `check` | indicator |  |  |

Item states: `selected`, `hot`, `current`, `checked`, `indeterminate`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `items` (collection), `selection_mode` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `selection_changed` | `ListSelectionChange` |
| `item_activated` | `std::size_t` |
| `item_checking` | `ItemCheckEvent`, by reference |
| `item_check_state_changed` | `std::size_t, CheckState` |

## `combo_box` (ComboBox, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `well` | well |  |  |
| `content` | content |  |  |
| `divider` | divider |  |  |
| `drop_down` | actuator | `button` |  |
| `disclosure` | disclosure |  |  |

States: `normal`, `hot`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`, `open`, `closed`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `drop_down_filter` (enumeration), `drop_down_style` (enumeration), `items` (collection), `placeholder_text` (text), `selected_index` (signed_integer), `text` (text), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `selected_index_changed` | `std::optional<std::size_t>` |
| `drop_down_changed` | `bool` |
| `items_changed` | none |
| `text_changed` | `std::string` |

## `tree_view` (TreeView, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `content` | content |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `row` | region |  |  |
| `expander` | disclosure |  |  |
| `icon` | icon |  |  |
| `label` | label |  |  |
| `row_focus` | focus |  |  |

Item states: `selected`, `hot`, `current`, `open`, `closed`, `disabled`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `selection_changed` | `TreeSelectionChange` |
| `expansion_changed` | `TreeExpansionChange` |
| `item_activated` | `std::string` |

## `object_view` (ObjectView, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `header` | face |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `cell` | region |  |  |
| `glyph` | icon |  |  |
| `name` | label |  |  |
| `secondary` | content |  |  |
| `cell_focus` | focus |  |  |

Item states: `selected`, `hot`, `current`, `disabled`.

Item system `column`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `column_header` | region |  |  |
| `column_label` | label |  |  |
| `sort_glyph` | glyph |  |  |
| `column_divider` | divider |  |  |
| `column_grip` | grip |  |  |
| `column_focus` | focus |  |  |

Item states: `pressed`, `captured`, `current`. Application-defined: `sorted_ascending`, `sorted_descending`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `sort_requested` | `ObjectDetailsSort` |
| `selection_changed` | `ObjectSelectionChange` |
| `item_activated` | `std::string` |
| `context_requested` | `ObjectContextRequest` |

## `list_view` (ListView, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `well` | well |  |  |
| `header` | face |  |  |
| `vertical_scroll_bar` | track | `v_scroll_bar` |  |
| `horizontal_scroll_bar` | track | `h_scroll_bar` |  |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `insertion_mark` | drop |  | drop_accepted |
| `label_editor` | well | `text_box` |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `cell` | region |  |  |
| `item_face` | face |  |  |
| `check_well` | well |  |  |
| `check` | indicator |  |  |
| `icon` | icon |  |  |
| `text` | content |  |  |
| `cell_focus` | focus |  |  |

Item states: `selected`, `hot`, `current`, `checked`, `disabled`, `focus_visible`.

Item system `sub_item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `sub_item_cell` | region |  |  |
| `sub_item_text` | content |  |  |

Item states: `selected`, `disabled`.

Item system `column`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `column_header` | face |  |  |
| `column_label` | label |  |  |
| `sort_glyph` | glyph |  |  |
| `column_divider` | divider |  |  |
| `column_grip` | grip |  |  |

Item states: `hot`, `pressed`, `captured`. Application-defined: `sorted_ascending`, `sorted_descending`.

Item system `group`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `group_band` | region |  |  |
| `group_caption` | caption |  |  |
| `group_divider` | divider |  |  |

Item states: `disabled`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `draw_item_content` | `ItemContentPaint`, by reference |
| `selection_changed` | `ListViewSelectionChange` |
| `item_activated` | `ListViewItemActivation` |
| `item_checking` | `ListViewItemCheck`, by reference |
| `item_checked` | `ListViewItemCheck` |
| `column_click` | `ListViewColumnClick` |
| `column_width_changed` | `ListViewColumnWidthChange` |
| `before_label_edit` | `ListViewLabelEdit`, by reference |
| `after_label_edit` | `ListViewLabelEdit`, by reference |

## `tree_list_view` (TreeListView, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `well` | well |  |  |
| `header` | face |  |  |
| `vertical_scroll_bar` | track | `v_scroll_bar` |  |
| `horizontal_scroll_bar` | track | `h_scroll_bar` |  |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `row`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `row` | region |  |  |
| `row_face` | face |  |  |
| `expander` | disclosure |  |  |
| `icon` | icon |  |  |
| `text` | content |  |  |
| `row_focus` | focus |  |  |

Item states: `selected`, `hot`, `current`, `open`, `closed`, `disabled`, `focus_visible`.

Item system `cell`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `cell` | region |  |  |
| `cell_text` | content |  |  |

Item states: `selected`, `disabled`.

Item system `column`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `column_header` | face |  |  |
| `column_label` | label |  |  |
| `sort_glyph` | glyph |  |  |
| `column_divider` | divider |  |  |
| `column_grip` | grip |  |  |

Item states: `hot`, `pressed`, `captured`. Application-defined: `sorted_ascending`, `sorted_descending`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `selection_changed` | `TreeListSelectionChange` |
| `expansion_changed` | `TreeListExpansionChange` |
| `item_activated` | `TreeListActivation` |
| `sort_changed` | `TreeListSortChange` |
| `column_width_changed` | `TreeListColumnWidthChange` |
| `column_moved` | `TreeListColumnMove` |

## `data_grid_view` (DataGridView, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `well` | well |  |  |
| `edge` | edge |  |  |
| `column_header_band` | face |  |  |
| `row_header_band` | face |  |  |
| `top_left_header` | face |  |  |
| `horizontal_scroll_bar` | track | `h_scroll_bar` |  |
| `vertical_scroll_bar` | track | `v_scroll_bar` |  |
| `scroll_corner` | face |  |  |
| `editor` | well | `text_box` |  |
| `focus` | focus |  |  |
| `column_drop_indicator` | drop |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`, `read_only`.

Item system `row`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `row_header` | face |  |  |
| `row_header_label` | label |  |  |
| `row_glyph` | glyph |  |  |
| `row_error` | glyph |  |  |
| `row_rule` | divider |  |  |
| `row_grip` | grip |  |  |

Item states: `selected`, `current`, `read_only`, `invalid`, `captured`.

Item system `cell`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `cell` | region |  |  |
| `cell_selection` | selection |  |  |
| `cell_content` | content |  |  |
| `cell_image` | image |  |  |
| `cell_link` | content |  |  |
| `cell_link_underline` | glyph |  |  |
| `cell_check_well` | well | `check_box` |  |
| `cell_check_indicator` | indicator |  |  |
| `cell_button_face` | face | `button` |  |
| `cell_button_label` | label |  |  |
| `cell_drop_down` | actuator | `button` |  |
| `cell_drop_down_glyph` | glyph |  |  |
| `cell_vertical_rule` | divider |  |  |
| `cell_horizontal_rule` | divider |  |  |
| `cell_focus` | focus |  |  |
| `cell_error` | glyph |  |  |

Item states: `selected`, `current`, `read_only`, `invalid`, `checked`, `pressed`, `open`, `focus_visible`. Application-defined: `visited`.

Item system `column`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `column_header` | face |  |  |
| `column_label` | label |  |  |
| `sort_glyph` | glyph |  |  |
| `column_divider` | divider |  |  |
| `column_grip` | grip |  |  |

Item states: `hot`, `pressed`, `captured`. Application-defined: `sorted_ascending`, `sorted_descending`.

Properties beyond every control's: `allow_user_to_add_rows` (boolean), `multi_select` (boolean), `read_only` (boolean), `selection_mode` (enumeration).

| Interaction | Payload |
|---|---|
| `draw_item_content` | `ItemContentPaint`, by reference |
| `selection_changed` | none |
| `current_cell_changed` | `std::optional<DataGridViewCellAddress>` |
| `cell_begin_edit` | `DataGridViewCellCancelEvent`, by reference |
| `cell_end_edit` | `DataGridViewCellEvent` |
| `cell_validating` | `DataGridViewCellValidatingEvent`, by reference |
| `cell_value_changed` | `DataGridViewCellEvent` |
| `cell_content_click` | `DataGridViewCellEvent` |
| `row_order_mode_changed` | `DataGridViewRowOrderModeChange` |
| `data_error` | `DataGridViewDataErrorEvent`, by reference |
| `cell_validated` | `DataGridViewCellEvent` |
| `current_cell_dirty_state_changed` | `bool` |
| `row_validating` | `DataGridViewRowCancelEvent`, by reference |
| `row_validated` | `DataGridViewRowEvent` |
| `cell_enter` | `DataGridViewCellEvent` |
| `cell_leave` | `DataGridViewCellEvent` |
| `row_enter` | `DataGridViewRowEvent` |
| `row_leave` | `DataGridViewRowEvent` |
| `default_values_needed` | `DataGridViewRowValuesEvent`, by reference |
| `user_added_row` | `DataGridViewRowEvent` |
| `user_deleting_row` | `DataGridViewRowCancelEvent`, by reference |
| `user_deleted_row` | `DataGridViewRowEvent` |
| `row_edit_cancelled` | `DataGridViewRowEvent` |
| `cell_error_text_changed` | `DataGridViewCellEvent` |
| `row_error_text_changed` | `DataGridViewRowEvent` |
| `cell_error_text_needed` | `DataGridViewCellErrorTextNeededEvent`, by reference |
| `row_error_text_needed` | `DataGridViewRowErrorTextNeededEvent`, by reference |
| `column_header_click` | `std::size_t` |
| `sort_compare` | `DataGridViewSortCompareEvent`, by reference |
| `sorted` | none |
| `column_width_changed` | `std::size_t` |
| `column_display_index_changed` | `std::size_t` |
| `row_height_changed` | `DataGridViewRowEvent` |
| `cell_formatting` | `DataGridViewCellFormattingEvent`, by reference |
| `cell_parsing` | `DataGridViewCellParsingEvent`, by reference |

## `choice_group` (ChoiceGroup, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `track` | track |  |  |
| `selection` | selection |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`. Application-defined: `segmented`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `presentation` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `changed` | `int` |
| `activated` | `int` |

## `breadcrumb_trail` (BreadcrumbTrail, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `segment` | face | `button` |  |
| `segment_label` | label |  |  |
| `segment_cap` | cap |  |  |
| `segment_focus` | focus |  |  |

Item states: `hot`, `current`, `disabled`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `segment_activated` | `std::string` |
| `overflow_activated` | none |
| `edit_committed` | `std::string` |
| `edit_started` | `std::string` |
| `edit_cancelled` | none |
| `edit_completion_requested` | none |

## `check_box` (CheckBox, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `well` | well |  |  |
| `indicator` | indicator |  |  |
| `icon` | icon |  |  |
| `label` | label |  |  |
| `focus` | focus |  |  |

States: `normal`, `hot`, `pressed`, `checked`, `indeterminate`, `enabled`, `disabled`, `focused`, `focus_visible`, `open`, `closed`.

Properties beyond every control's: `check_state` (enumeration), `checked` (boolean), `content_padding` (insets), `font` (font), `image` (image), `indicator_style` (enumeration), `text` (text), `text_alignment` (enumeration), `three_state` (boolean).

| Interaction | Payload |
|---|---|
| `expanded_changed` | `std::optional<bool>` |
| `clicked` | `ButtonBase`, by reference |
| `text_changed` | `std::string` |
| `check_state_changed` | `CheckState` |
| `checked_changed` | `bool` |

## `radio_button` (RadioButton, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `well` | well |  |  |
| `indicator` | indicator |  |  |
| `icon` | icon |  |  |
| `label` | label |  |  |
| `focus` | focus |  |  |

States: `normal`, `hot`, `pressed`, `checked`, `enabled`, `disabled`, `focused`, `focus_visible`, `open`, `closed`.

Properties beyond every control's: `checked` (boolean), `content_padding` (insets), `font` (font), `group_name` (text), `image` (image), `indicator_style` (enumeration), `text` (text), `text_alignment` (enumeration).

| Interaction | Payload |
|---|---|
| `expanded_changed` | `std::optional<bool>` |
| `clicked` | `ButtonBase`, by reference |
| `text_changed` | `std::string` |
| `checked_changed` | `bool` |

## `toggle_switch` (ToggleSwitch, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `track` | track |  |  |
| `thumb` | indicator |  |  |
| `label` | label |  |  |
| `state` | label |  |  |
| `focus` | focus |  |  |

States: `normal`, `hot`, `pressed`, `checked`, `enabled`, `disabled`, `focused`, `focus_visible`, `open`, `closed`.

Properties beyond every control's: `checked` (boolean), `content_padding` (insets), `font` (font), `image` (image), `off_text` (text), `on_text` (text), `text` (text), `text_alignment` (enumeration).

| Interaction | Payload |
|---|---|
| `expanded_changed` | `std::optional<bool>` |
| `clicked` | `ButtonBase`, by reference |
| `text_changed` | `std::string` |
| `checked_changed` | `bool` |

## `range_control` (RangeControl, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `maximum` (number), `minimum` (number), `value` (number).

| Interaction | Payload |
|---|---|
| `range_changed` | `double, double` |
| `value_changed` | `double` |
| `scroll` | `RangeScrollEvent` |

## `track_bar` (TrackBar, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `track` | track |  |  |
| `fill` | fill |  |  |
| `ticks` | glyph |  |  |
| `thumb` | thumb |  | pressed, captured |
| `focus` | focus |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `maximum` (number), `minimum` (number), `thumb_extent` (number), `tick_frequency` (number), `value` (number), `visual_style` (enumeration).

| Interaction | Payload |
|---|---|
| `range_changed` | `double, double` |
| `value_changed` | `double` |
| `scroll` | `RangeScrollEvent` |

## `value_bar` (ValueBar, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `track` | track |  |  |
| `fill` | fill |  |  |
| `label` | label |  |  |
| `value` | label |  |  |
| `decrement_arrow` | actuator | `button` | hot, pressed |
| `increment_arrow` | actuator | `button` | hot, pressed |
| `decrement_glyph` | glyph |  | hot, pressed |
| `increment_glyph` | glyph |  | hot, pressed |
| `overrange` | indicator |  |  |
| `validation` | validation |  |  |
| `focus` | focus |  |  |

States: `normal`, `hot`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`, `invalid`.

Properties beyond every control's: `default_value` (number), `label` (text), `maximum` (number), `minimum` (number), `precision` (signed_integer), `presentation` (enumeration), `scrub_rate` (number), `snap_step` (number), `soft_maximum` (number), `soft_minimum` (number), `unit` (enumeration), `value` (number).

| Interaction | Payload |
|---|---|
| `range_changed` | `double, double` |
| `value_changed` | `double` |
| `scroll` | `RangeScrollEvent` |
| `value_committed` | `double` |

## `numeric_up_down` (NumericUpDown, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `field` | well | `text_box` |  |
| `increment_arrow` | actuator | `button` | hot, pressed |
| `decrement_arrow` | actuator | `button` | hot, pressed |
| `increment_glyph` | glyph |  | hot, pressed |
| `decrement_glyph` | glyph |  | hot, pressed |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`, `invalid`, `pending`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `decimal_places` (unsigned_integer), `increment` (number), `maximum` (number), `minimum` (number), `scrub` (boolean), `text_alignment` (enumeration), `thousands_separator` (boolean), `value` (number), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `value_changed` | `double` |

## `domain_up_down` (DomainUpDown, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `field` | well | `text_box` |  |
| `up_arrow` | actuator | `button` | hot, pressed |
| `down_arrow` | actuator | `button` | hot, pressed |
| `up_glyph` | glyph |  | hot, pressed |
| `down_glyph` | glyph |  | hot, pressed |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`, `read_only`, `invalid`, `pending`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `items` (collection), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `selected_index_changed` | `std::optional<std::size_t>` |

## `color_value_editor` (ColorValueEditor, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `swatch` | chip |  |  |
| `field` | well | `text_box` |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`, `invalid`, `pending`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `value_changed` | `Color` |
| `edit_failed` | `PropertyEditorInputError` |

## `flags_value_editor` (FlagsValueEditor, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `content` | content |  |  |
| `divider` | divider |  |  |
| `drop_down` | actuator | `button` |  |
| `disclosure` | disclosure |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`, `open`, `closed`, `invalid`, `pending`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `checked_flags` (collection), `enumeration_name` (text), `flag_names` (collection), `none_name` (text), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `value_changed` | `PropertyEnumValue` |
| `drop_down_changed` | `bool` |

## `property_grid` (PropertyGrid, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `property_sort` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `selected_object_changed` | `Control::Ptr` |
| `property_value_changed` | `PropertyGridValueChange` |
| `edit_failed` | `PropertyGridEditError` |

## `property_list` (PropertyList, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `group_header` | face |  |  |
| `group_expander` | disclosure |  |  |
| `group_title` | caption |  |  |
| `property_row` | region |  |  |
| `property_name` | label |  |  |
| `property_value` | content |  |  |
| `row_expander` | disclosure |  |  |
| `validation` | validation |  |  |
| `row_divider` | divider |  |  |

Item states: `open`, `closed`, `invalid`, `read_only`, `disabled`. Application-defined: `group_header`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `value_changed` | `PropertyValueChange` |
| `value_committed` | `PropertyValueChange` |
| `group_changed` | `PropertyGroupChange` |
| `row_expansion_changed` | `PropertyRowExpansionChange` |
| `reset_requested` | `PropertyResetRequest` |

## `date_time_picker` (DateTimePicker, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `content` | content |  |  |
| `drop_down` | actuator | `button` |  |
| `disclosure` | disclosure |  |  |
| `increment_arrow` | actuator | `button` | pressed |
| `decrement_arrow` | actuator | `button` | pressed |
| `well` | well |  | pressed |
| `indicator` | indicator |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`, `open`, `closed`, `checked`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `value_changed` | `DateTimeValue` |
| `checked_changed` | `bool` |
| `drop_down_changed` | `bool` |

## `month_calendar` (MonthCalendar, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `previous_arrow` | actuator | `button` | hot, pressed, disabled |
| `previous_glyph` | glyph |  | hot, pressed, disabled |
| `next_arrow` | actuator | `button` | hot, pressed, disabled |
| `next_glyph` | glyph |  | hot, pressed, disabled |
| `today_label` | label |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `month`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `month_title` | label |  |  |
| `weekday_rule` | divider |  |  |

Item states: none.

Item system `weekday`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `weekday_label` | label |  |  |

Item states: none.

Item system `week`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `week_label` | label |  |  |

Item states: none.

Item system `day`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `day_face` | face |  |  |
| `day_text` | content |  |  |
| `day_marker` | perimeter |  | current |
| `day_focus` | focus |  |  |

Item states: `hot`, `pressed`, `selected`, `current`, `disabled`, `focus_visible`. Application-defined: `bolded`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `date_changed` | `CalendarDateRange` |
| `date_selected` | `CalendarDateRange` |

## `panel` (Panel, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |

## `group_box` (GroupBox, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `caption` | caption |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `font` (font), `text` (text), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `text_changed` | `std::string` |

## `scaled_group_box` (ScaledGroupBox, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `caption` | caption |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `design_size` (size), `font` (font), `text` (text), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `text_changed` | `std::string` |

## `material_panel` (MaterialPanel, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `material_changed` | `SurfaceMaterial` |

## `card` (Card, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `hot`, `pressed`, `captured`, `selected`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `activated` | `Card`, by reference |
| `selected_changed` | `bool` |

## `expandable_sections` (ExpandableSections, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `toggled` | `int, bool` |

## `container_control` (ContainerControl, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |

## `user_control` (UserControl, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |
| `loaded` | none |

## `tab_control` (TabControl, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `display` | face |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `tab` | face |  |  |
| `tab_label` | label |  |  |
| `tab_focus` | focus |  |  |
| `tab_seam` | seam |  |  |
| `tab_icon` | icon |  |  |

Item states: `selected`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |
| `draw_item_content` | `ItemContentPaint`, by reference |
| `selected_index_changed` | `TabSelectionChange` |
| `deselecting` | `TabControlCancelEvent`, by reference |
| `selecting` | `TabControlCancelEvent`, by reference |
| `deselected` | `TabControlEvent` |
| `selected` | `TabControlEvent` |

## `page_strip` (PageStrip, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `rail` | face |  |  |
| `scroll_back` | actuator | `button` | hot, pressed, disabled |
| `scroll_back_glyph` | glyph |  | hot, pressed, disabled |
| `scroll_forward` | actuator | `button` | hot, pressed, disabled |
| `scroll_forward_glyph` | glyph |  | hot, pressed, disabled |
| `list_button` | actuator | `button` | hot, pressed, open |
| `list_count` | label |  | hot, pressed, open |
| `list_glyph` | glyph |  | hot, pressed, open |
| `insertion_mark` | drop |  | drop_accepted |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `page`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `tab` | face |  |  |
| `tab_icon` | icon |  |  |
| `tab_label` | label |  |  |
| `tab_modified` | glyph |  |  |
| `tab_close` | actuator | `button` | hot, pressed |
| `tab_close_glyph` | glyph |  | hot, pressed |
| `tab_pin` | actuator | `button` | hot, pressed, checked |
| `tab_pin_glyph` | glyph |  | hot, pressed, checked |
| `tab_seam` | seam |  |  |
| `tab_focus` | focus |  |  |

Item states: `hot`, `pressed`, `captured`, `selected`, `disabled`, `focus_visible`. Application-defined: `pinned`, `modified`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `draw_item_content` | `ItemContentPaint`, by reference |
| `page_invoked` | `PageInvocation` |
| `page_close_requested` | `PageCloseRequest`, by reference |
| `page_pin_requested` | `PagePinRequest`, by reference |
| `page_drag_started` | `PageDragStart`, by reference |
| `page_context_menu_requested` | `PageContextMenuRequest`, by reference |
| `page_double_clicked` | `PageDoubleClick` |

## `page_view` (PageView, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `strip` | region | `page_strip` |  |
| `content` | well |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `content_requested` | `PageContentRequest` |

## `navigation_pane` (NavigationPane, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `header` | face |  |  |
| `header_icon` | icon |  |  |
| `header_label` | caption |  |  |
| `collapse_button` | actuator | `button` | hot, pressed, focus_visible |
| `collapse_glyph` | glyph |  | hot, pressed, focus_visible |
| `content` | well |  |  |
| `grip_bar` | face |  | hot, captured, focus_visible |
| `grip` | grip |  | hot, captured, focus_visible |
| `button_area` | face |  |  |
| `overflow_area` | face |  |  |
| `configure_button` | actuator | `button` | hot, pressed, open, focus_visible |
| `configure_glyph` | glyph |  | hot, pressed, open, focus_visible |
| `collapsed_bar` | face |  |  |
| `collapsed_label` | label |  | hot, pressed, open, focus_visible |
| `overlay_shadow` | shadow |  |  |
| `overlay_face` | face |  |  |
| `overlay_header` | face |  |  |
| `overlay_label` | caption |  |  |
| `overlay_content` | well |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `nav_page`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `nav_page_face` | face |  |  |
| `nav_page_icon` | icon |  |  |
| `nav_page_label` | label |  |  |

Item states: `hot`, `pressed`, `selected`, `focus_visible`.

Item system `nav_overflow`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `nav_overflow_face` | face |  |  |
| `nav_overflow_icon` | icon |  |  |
| `nav_overflow_label` | label |  |  |

Item states: `hot`, `pressed`, `selected`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `options_requested` | `NavigationPaneOptionsRequest` |
| `state_changed` | `NavigationPaneState` |
| `overlay_open_changed` | `bool` |

## `dock_site` (DockSite, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `document_area` | well |  |  |
| `focus` | focus |  |  |
| `drag_stamp` | face |  |  |
| `drag_stamp_label` | label |  |  |
| `drag_stamp_image` | image |  |  |
| `drop_area` | drop |  | drop_accepted |
| `drop_cue` | indicator |  | drop_rejected |
| `flyout_shadow` | shadow |  |  |
| `flyout_face` | face |  |  |
| `flyout_grip` | grip |  | hot, captured |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `seam`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `seam_bar` | seam |  |  |
| `seam_grip` | grip |  |  |

Item states: `hot`, `captured`, `focus_visible`, `disabled`.

Item system `stack`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `stack_area` | region | `page_view` |  |
| `caption` | face |  |  |
| `caption_accent` | keyline |  |  |
| `caption_key` | chip |  |  |
| `caption_label` | caption |  |  |
| `caption_grip` | grip |  |  |
| `caption_menu` | actuator | `button` | hot, pressed, open, disabled |
| `caption_menu_glyph` | glyph |  | disabled |
| `caption_pin` | actuator | `button` | hot, pressed, checked, disabled |
| `caption_pin_glyph` | glyph |  | checked, disabled |
| `caption_close` | actuator | `button` | hot, pressed, disabled |
| `caption_close_glyph` | glyph |  | disabled |

Item states: `current`, `disabled`.

Item system `drop_tile`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `tile` | actuator |  |  |
| `tile_glyph` | glyph |  |  |

Item states: `selected`, `disabled`, `expanding`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `drag_ended` | `DockDragOutcome` |
| `flyout_changed` | `DockFlyoutChange` |

## `dock_navigator` (DockNavigator, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `tool_heading` | caption |  |  |
| `document_heading` | caption |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `entry`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `entry_face` | face | `menu_item` |  |
| `entry_label` | label |  |  |

Item states: `hot`, `selected`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `chosen` | `DockNavigatorChoice` |
| `cancelled` | none |

## `tab_page` (TabPage, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |

## `flex_panel` (FlexPanel, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `style` (text), `wheel_scrolls` (boolean).

Properties it provides to its children: `item_style` (text).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |

## `grid_panel` (GridPanel, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `style` (text), `wheel_scrolls` (boolean).

Properties it provides to its children: `area` (text), `cell` (point), `item_style` (text).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |

## `block_panel` (BlockPanel, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `style` (text), `wheel_scrolls` (boolean).

Properties it provides to its children: `item_style` (text).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |

## `flow_layout_panel_compat` (FlowLayoutPanelCompat, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `cross_alignment` (enumeration), `flow_direction` (enumeration), `item_spacing` (size), `main_alignment` (enumeration), `wheel_scrolls` (boolean), `wrap_contents` (boolean).

Properties it provides to its children: `flow_break` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |

## `table_layout_panel_compat` (TableLayoutPanelCompat, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `cell_grid` | divider |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `cell_border_style` (enumeration), `column_count` (unsigned_integer), `column_styles` (collection), `grow_style` (enumeration), `row_count` (unsigned_integer), `row_styles` (collection), `track_spacing` (size).

Properties it provides to its children: `cell` (point), `column_span` (unsigned_integer), `row_span` (unsigned_integer).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |

## `responsive_track_panel` (ResponsiveTrackPanel, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `orientation` (enumeration), `track_gap` (number), `track_specs` (collection).

Properties it provides to its children: `child_track` (unsigned_integer).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |

## `scaled_panel` (ScaledPanel, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `design_size` (size), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |

## `split_container` (SplitContainer, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `seam` | seam |  | hot, pressed, captured |
| `grip` | grip |  | open, closed |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`, `hot`, `open`, `closed`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |
| `splitter_changed` | `SplitChangeEvent` |
| `drag_started` | `DragInteraction` |
| `drag_target_changed` | `DragInteraction` |
| `drag_dropped` | `DragInteraction` |
| `drag_ended` | `DragInteraction` |

## `splitter_panel` (SplitterPanel, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |

## `splitter` (Splitter, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |

States: `normal`, `hot`, `pressed`, `captured`, `enabled`, `disabled`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `splitter_moving` | `SplitterMoveEvent`, by reference |
| `splitter_moved` | `SplitterMoveEvent` |
| `drag_started` | `DragInteraction` |
| `drag_target_changed` | `DragInteraction` |
| `drag_dropped` | `DragInteraction` |
| `drag_ended` | `DragInteraction` |

## `command_overflow_panel` (CommandOverflowPanel, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `group_gap` (number).

Properties it provides to its children: `group_spec` (text).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |

## `master_detail_view` (MasterDetailView, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `auto_validate_changed` | `AutoValidate` |
| `presentation_changed` | `MasterDetailPresentationChange` |

## `scrollable_control` (ScrollableControl, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |

## `scroll_bar` (ScrollBar, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `decrement_arrow` | actuator | `button` | hot, pressed |
| `increment_arrow` | actuator | `button` | hot, pressed |
| `decrement_glyph` | glyph |  | hot, pressed |
| `increment_glyph` | glyph |  | hot, pressed |
| `track` | track |  | hot, pressed |
| `thumb` | thumb |  | hot, pressed, captured |
| `focus` | focus |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `large_change` (number), `maximum` (number), `minimum` (number), `value` (number).

| Interaction | Payload |
|---|---|
| `range_changed` | `double, double` |
| `value_changed` | `double` |
| `scroll` | `RangeScrollEvent` |

## `h_scroll_bar` (HScrollBar, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `decrement_arrow` | actuator | `button` | hot, pressed |
| `increment_arrow` | actuator | `button` | hot, pressed |
| `decrement_glyph` | glyph |  | hot, pressed |
| `increment_glyph` | glyph |  | hot, pressed |
| `track` | track |  | hot, pressed |
| `thumb` | thumb |  | hot, pressed, captured |
| `focus` | focus |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `large_change` (number), `maximum` (number), `minimum` (number), `value` (number).

| Interaction | Payload |
|---|---|
| `range_changed` | `double, double` |
| `value_changed` | `double` |
| `scroll` | `RangeScrollEvent` |

## `v_scroll_bar` (VScrollBar, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `decrement_arrow` | actuator | `button` | hot, pressed |
| `increment_arrow` | actuator | `button` | hot, pressed |
| `decrement_glyph` | glyph |  | hot, pressed |
| `increment_glyph` | glyph |  | hot, pressed |
| `track` | track |  | hot, pressed |
| `thumb` | thumb |  | hot, pressed, captured |
| `focus` | focus |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `large_change` (number), `maximum` (number), `minimum` (number), `value` (number).

| Interaction | Payload |
|---|---|
| `range_changed` | `double, double` |
| `value_changed` | `double` |
| `scroll` | `RangeScrollEvent` |

## `menu_strip` (MenuStrip, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `divider` | divider |  |  |
| `focus` | focus |  |  |
| `gutter` | well |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `menu_item` | face |  |  |
| `menu_label` | label |  |  |
| `menu_focus` | focus |  |  |

Item states: `hot`, `open`, `selected`, `disabled`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `item_invoked` | `MenuStripInvocation` |
| `open_changed` | `std::optional<std::size_t>` |

## `tool_strip` (ToolStrip, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `grip` | grip |  |  |
| `overflow_button` | actuator | `button` | hot, open |
| `overflow_glyph` | glyph |  | hot, open |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `item_face` | face |  |  |
| `item_icon` | icon |  |  |
| `item_label` | label |  |  |
| `item_arrow` | actuator | `button` | hot, pressed |
| `item_arrow_glyph` | glyph |  |  |
| `item_separator` | divider |  |  |
| `item_focus` | focus |  |  |
| `item_combo_box` | well | `combo_box` |  |
| `item_text_box` | well | `text_box` |  |
| `item_progress_bar` | track | `progress_bar` |  |

Item states: `hot`, `pressed`, `checked`, `open`, `disabled`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `item_clicked` | `ToolStripItemClick` |
| `drop_down_item_invoked` | `ToolStripDropDownInvocation` |
| `item_focus_changed` | `ToolStripItemFocus` |

## `ribbon_font_control` (RibbonFontControl, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `family_field` | well | `combo_box` |  |
| `size_field` | well | `combo_box` |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `value_changed` | `FontChoice` |

## `ribbon_color_picker` (RibbonColorPicker, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face | `button` | hot, pressed, disabled, open |
| `icon` | icon |  | hot, pressed, disabled, open |
| `color_bar` | well |  |  |
| `color_sample` | fill |  |  |
| `arrow` | actuator | `button` | hot, pressed, disabled, open |
| `arrow_glyph` | glyph |  | hot, pressed, disabled, open |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`, `open`.

Item system `palette_entry`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `entry_face` | face |  | hot, selected, focus_visible |
| `entry_well` | well |  | hot, selected, focus_visible |
| `entry_sample` | fill |  | hot, selected, focus_visible |
| `entry_label` | label |  | hot, selected, focus_visible |
| `entry_focus` | focus |  | hot, selected, focus_visible |

Item states: `hot`, `selected`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `value_changed` | `std::optional<Color>` |
| `palette_open_changed` | `bool` |

## `backstage_view` (BackstageView, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `column` | face |  |  |
| `page` | face |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `backstage_item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `item_face` | face |  |  |
| `item_icon` | icon |  |  |
| `item_label` | label |  |  |
| `item_focus` | focus |  |  |

Item states: `hot`, `pressed`, `selected`, `disabled`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `topic_changed` | `BackstageTopicChange` |
| `close_requested` | none |

## `ribbon_gallery` (RibbonGallery, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `well` | well |  |  |
| `item_viewport` | region |  |  |
| `scroll_up` | actuator | `button` | hot, pressed, disabled |
| `scroll_up_glyph` | glyph |  | hot, pressed, disabled |
| `scroll_down` | actuator | `button` | hot, pressed, disabled |
| `scroll_down_glyph` | glyph |  | hot, pressed, disabled |
| `more_button` | actuator | `button` | hot, pressed, open |
| `more_glyph` | glyph |  | open |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `gallery_item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `item_face` | face |  |  |
| `item_selection` | selection |  |  |
| `item_icon` | icon |  |  |
| `item_label` | label |  |  |
| `item_focus` | focus |  |  |

Item states: `hot`, `pressed`, `selected`, `current`, `disabled`, `focus_visible`.

Item system `gallery_category`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `category_caption` | caption |  |  |
| `category_divider` | divider |  |  |

Item states: none.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `selection_changed` | `std::optional<std::string>` |
| `overlay_open_changed` | `bool` |

## `status_strip` (StatusStrip, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `sizing_grip` | grip |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `item_face` | face |  |  |
| `item_icon` | icon |  |  |
| `item_label` | label |  |  |
| `item_arrow` | actuator | `button` | hot, pressed |
| `item_arrow_glyph` | glyph |  |  |
| `item_separator` | divider |  |  |
| `item_focus` | focus |  |  |
| `item_combo_box` | well | `combo_box` |  |
| `item_text_box` | well | `text_box` |  |
| `item_progress_bar` | track | `progress_bar` |  |

Item states: `hot`, `pressed`, `checked`, `open`, `disabled`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `item_clicked` | `ToolStripItemClick` |
| `drop_down_item_invoked` | `ToolStripDropDownInvocation` |
| `item_focus_changed` | `ToolStripItemFocus` |

## `quick_access_toolbar` (QuickAccessToolbar, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `grip` | grip |  |  |
| `overflow_button` | actuator | `button` | hot, open |
| `overflow_glyph` | glyph |  | hot, open |
| `customize_button` | actuator | `button` | hot, open |
| `customize_glyph` | glyph |  | hot, open |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `item_face` | face |  |  |
| `item_icon` | icon |  |  |
| `item_label` | label |  |  |
| `item_arrow` | actuator | `button` | hot, pressed |
| `item_arrow_glyph` | glyph |  |  |
| `item_separator` | divider |  |  |
| `item_focus` | focus |  |  |
| `item_combo_box` | well | `combo_box` |  |
| `item_text_box` | well | `text_box` |  |
| `item_progress_bar` | track | `progress_bar` |  |

Item states: `hot`, `pressed`, `checked`, `open`, `disabled`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `item_clicked` | `ToolStripItemClick` |
| `drop_down_item_invoked` | `ToolStripDropDownInvocation` |
| `item_focus_changed` | `ToolStripItemFocus` |
| `customized` | `QuickAccessToolbarCustomization` |
| `placement_changed` | `QuickAccessToolbarPlacementChange` |

## `tool_strip_panel` (ToolStripPanel, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: none.

Interactions beyond every control's: none.

## `tool_strip_container` (ToolStripContainer, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `top_panel` | face | `tool_strip_panel` |  |
| `bottom_panel` | face | `tool_strip_panel` |  |
| `left_panel` | face | `tool_strip_panel` |  |
| `right_panel` | face | `tool_strip_panel` |  |
| `content_panel` | region | `panel` |  |
| `drag_stamp` | face |  |  |
| `drag_stamp_image` | image |  |  |
| `drop_marker` | drop |  | drop_accepted |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `strip_moved` | `ToolStripMove` |

## `binding_navigator` (BindingNavigator, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `grip` | grip |  |  |
| `overflow_button` | actuator | `button` | hot, open |
| `overflow_glyph` | glyph |  | hot, open |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `item`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `item_face` | face |  |  |
| `item_icon` | icon |  |  |
| `item_label` | label |  |  |
| `item_arrow` | actuator | `button` | hot, pressed |
| `item_arrow_glyph` | glyph |  |  |
| `item_separator` | divider |  |  |
| `item_focus` | focus |  |  |
| `item_combo_box` | well | `combo_box` |  |
| `item_text_box` | well | `text_box` |  |
| `item_progress_bar` | track | `progress_bar` |  |

Item states: `hot`, `pressed`, `checked`, `open`, `disabled`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `item_clicked` | `ToolStripItemClick` |
| `drop_down_item_invoked` | `ToolStripDropDownInvocation` |
| `item_focus_changed` | `ToolStripItemFocus` |
| `adding_new` | `BindingNavigatorAddNew`, by reference |

## `ribbon` (Ribbon, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `tab_rail` | face |  |  |
| `application_button` | face | `button` | hot, pressed, open, focus_visible |
| `application_label` | label |  | hot, pressed, open, focus_visible |
| `command_plane` | face |  |  |
| `scroll_left` | actuator | `button` | hot, pressed |
| `scroll_left_glyph` | glyph |  | hot, pressed |
| `scroll_right` | actuator | `button` | hot, pressed |
| `scroll_right_glyph` | glyph |  | hot, pressed |
| `help_button` | actuator | `button` | hot, pressed, focus_visible |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `tab`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `tab` | face |  |  |
| `tab_hover` | face |  |  |
| `tab_label` | label |  |  |
| `tab_focus` | focus |  |  |

Item states: `hot`, `selected`, `focus_visible`, `disabled`.

Item system `group`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `group_region` | region |  |  |
| `group_caption` | caption |  |  |
| `group_divider` | divider |  |  |
| `group_launcher` | actuator | `button` | hot, pressed, focus_visible |
| `group_launcher_glyph` | glyph |  | hot, pressed, focus_visible |
| `group_launcher_focus` | focus |  | hot, pressed, focus_visible |
| `group_button` | face | `button` | hot, pressed, open, focus_visible |
| `group_button_icon` | icon |  | hot, pressed, open, focus_visible |
| `group_button_label` | label |  | hot, pressed, open, focus_visible |
| `group_button_glyph` | glyph |  | hot, pressed, open, focus_visible |
| `group_button_focus` | focus |  | hot, pressed, open, focus_visible |

Item states: none.

Item system `command`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `command_face` | face |  |  |
| `command_icon` | icon |  |  |
| `command_label` | label |  |  |
| `command_arrow` | actuator | `button` | hot, pressed |
| `command_arrow_glyph` | glyph |  |  |
| `command_focus` | focus |  |  |
| `command_combo_box` | well | `combo_box` |  |
| `command_numeric_up_down` | well | `numeric_up_down` |  |
| `command_check_box` | indicator | `check_box` |  |
| `command_text_box` | well | `text_box` |  |
| `command_gallery` | well | `ribbon_gallery` |  |
| `command_font_control` | well | `ribbon_font_control` |  |
| `command_color_picker` | actuator | `ribbon_color_picker` |  |

Item states: `hot`, `pressed`, `checked`, `open`, `disabled`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `command_clicked` | `RibbonCommandClick` |
| `drop_down_item_invoked` | `RibbonDropDownInvocation` |
| `dialog_launcher_clicked` | `RibbonDialogLaunch` |
| `selected_tab_changed` | `RibbonTabChange` |
| `minimized_changed` | `bool` |
| `application_menu_item_invoked` | `MenuItemInvocation` |
| `recent_item_invoked` | `RibbonRecentItemInvocation` |
| `backstage_open_changed` | `bool` |

## `context_menu` (ContextMenu, component)

| Interaction | Payload |
|---|---|
| `item_invoked` | `MenuItemInvocation` |
| `open_changed` | `bool` |

## `dock_workspace` (DockWorkspace, component)

| Interaction | Payload |
|---|---|
| `document_closing` | `DockDocumentClosing`, by reference |
| `activation_changed` | `DockActivationChange` |
| `layout_changed` | `DockLayoutChange` |

## `command` (Command, component)

| Interaction | Payload |
|---|---|
| `state_changed` | `CommandState` |
| `invoked` | `CommandInvocation` |

## `font_command` (FontCommand, component)

| Interaction | Payload |
|---|---|
| `value_changed` | `FontChoice` |

## `color_command` (ColorCommand, component)

| Interaction | Payload |
|---|---|
| `value_changed` | `std::optional<Color>` |

## `gallery_command` (GalleryCommand, component)

| Interaction | Payload |
|---|---|
| `value_changed` | `std::optional<std::string>` |
| `preview_requested` | `GalleryPreview` |
| `preview_cancelled` | `GalleryPreview` |

## `anchored_overlay_layer` (AnchoredOverlayLayer, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `dismiss_requested` | `OverlayDismissReason` |

## `font_dialog` (FontDialog, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `title` | label |  |  |
| `preview_well` | well |  |  |
| `preview_text` | content |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `apply` | `FontSpec` |
| `closed` | `FontDialogResult` |

## `task_dialog` (TaskDialog, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |
| `caption_band` | face |  |  |
| `caption` | label |  |  |
| `icon_plate` | chip |  |  |
| `icon_image` | icon |  |  |
| `icon_glyph` | glyph |  |  |
| `icon_label` | label |  |  |
| `heading` | label | `label` |  |
| `text` | content | `label` |  |
| `progress` | track | `progress_bar` |  |
| `radio_group` | region | `radio_button` |  |
| `expander_glyph` | disclosure |  | open |
| `expander_button` | face | `button` |  |
| `expanded_text` | content | `label` |  |
| `verification` | indicator | `check_box` |  |
| `button_band` | face |  |  |
| `button_divider` | divider |  |  |
| `button_row` | region | `button` |  |
| `footnote_band` | face |  |  |
| `footnote_plate` | chip |  |  |
| `footnote_glyph` | glyph |  |  |
| `footnote_label` | label |  |  |
| `footnote` | content | `label` |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `button_clicked` | `TaskDialogButtonClick`, by reference |
| `closed` | `TaskDialogResult` |
| `navigated` | none |
| `expanded_changed` | `bool` |
| `verification_changed` | `bool` |
| `radio_button_changed` | `std::string` |

## `thread_exception_dialog` (ThreadExceptionDialog, component)

| Interaction | Payload |
|---|---|
| `closed` | `DialogResult` |

## `seven_segment_display` (SevenSegmentDisplay, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `plot` | plot |  |  |
| `trace` | trace |  |  |
| `graticule` | graticule |  |  |

States: `normal`, `selected`, `enabled`, `disabled`.

Properties beyond every control's: `digit_count` (unsigned_integer), `ghost_segments` (boolean), `text` (text).

| Interaction | Payload |
|---|---|
| `text_changed` | `std::string` |

## `gauge_base` (GaugeBase, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `decimal_places` (unsigned_integer), `maximum` (number), `minimum` (number), `show_value` (boolean), `tick_frequency` (number), `units` (text), `value` (number).

| Interaction | Payload |
|---|---|
| `range_changed` | `double, double` |
| `value_changed` | `double` |
| `scroll` | `RangeScrollEvent` |

## `linear_gauge` (LinearGauge, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `plot` | plot |  |  |
| `alarm_zone` | track |  |  |
| `trace` | trace |  |  |
| `graticule` | graticule |  |  |
| `caption` | caption |  |  |

States: `normal`, `selected`, `enabled`, `disabled`, `invalid`.

Properties beyond every control's: `decimal_places` (unsigned_integer), `maximum` (number), `minimum` (number), `show_value` (boolean), `tick_frequency` (number), `units` (text), `value` (number).

| Interaction | Payload |
|---|---|
| `range_changed` | `double, double` |
| `value_changed` | `double` |
| `scroll` | `RangeScrollEvent` |

## `radial_gauge` (RadialGauge, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `plot` | plot |  |  |
| `track` | arc |  |  |
| `alarm_arc` | arc |  |  |
| `value_arc` | arc |  |  |
| `graticule` | graticule |  |  |
| `needle` | needle |  |  |
| `caption` | caption |  |  |

States: `normal`, `selected`, `enabled`, `disabled`, `invalid`.

Properties beyond every control's: `decimal_places` (unsigned_integer), `maximum` (number), `minimum` (number), `show_value` (boolean), `tick_frequency` (number), `units` (text), `value` (number).

| Interaction | Payload |
|---|---|
| `range_changed` | `double, double` |
| `value_changed` | `double` |
| `scroll` | `RangeScrollEvent` |

## `dial` (Dial, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `track` | arc |  |  |
| `value_arc` | arc |  |  |
| `graticule` | graticule |  |  |
| `thumb` | thumb |  |  |
| `needle` | needle |  |  |
| `focus` | focus |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`, `selected`, `hot`.

Properties beyond every control's: `maximum` (number), `minimum` (number), `show_arc` (boolean), `tick_frequency` (number), `value` (number).

| Interaction | Payload |
|---|---|
| `range_changed` | `double, double` |
| `value_changed` | `double` |
| `scroll` | `RangeScrollEvent` |

## `step_indicator` (StepIndicator, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `chip` | chip |  |  |
| `glyph` | glyph |  |  |
| `divider` | divider |  |  |
| `caption` | caption |  |  |

States: `normal`, `selected`, `checked`, `enabled`, `disabled`.

Properties beyond every control's: `current_step` (unsigned_integer).

| Interaction | Payload |
|---|---|
| `step_clicked` | `std::size_t` |
| `current_step_changed` | `std::uint32_t` |

## `wizard` (Wizard, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `caption` | caption |  |  |
| `label` | label |  |  |
| `divider` | divider |  |  |

States: `normal`, `enabled`, `disabled`.

Properties beyond every control's: `back_text` (text), `cancel_text` (text), `current_page` (unsigned_integer), `finish_text` (text), `next_text` (text), `show_step_indicator` (boolean).

| Interaction | Payload |
|---|---|
| `page_changing` | `WizardPageChangingEvent`, by reference |
| `page_changed` | `std::uint32_t` |
| `finished` | none |
| `cancelled` | none |

## `wizard_page` (WizardPage, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `can_advance` (boolean), `description` (text), `title` (text), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |

## `callout` (Callout, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `beak` | beak |  |  |
| `close` | glyph |  | hot, pressed |
| `actuator` | actuator | `button` |  |

States: `normal`, `enabled`, `disabled`.

Properties beyond every control's: `action_text` (text), `light_dismiss` (boolean), `prefers_above` (boolean), `text` (text), `title` (text).

| Interaction | Payload |
|---|---|
| `closed` | `CalloutCloseReason` |
| `action_clicked` | none |

## `modal_layer` (ModalLayer, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `scrim` | scrim |  |  |
| `shadow` | shadow |  |  |

States: `normal`, `enabled`, `disabled`.

Properties beyond every control's: `close_on_escape` (boolean).

| Interaction | Payload |
|---|---|
| `closing` | `ModalLayerClosingEvent`, by reference |
| `closed` | `DialogResult` |

## `lookup_combo_box` (LookupComboBox, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `well` | well |  |  |
| `label` | content |  |  |
| `actuator` | actuator |  |  |
| `glyph` | disclosure |  |  |
| `focus` | focus |  |  |

States: `normal`, `hot`, `pressed`, `enabled`, `disabled`, `focused`, `focus_visible`, `active_tool`.

Properties beyond every control's: `display_column` (unsigned_integer), `max_drop_down_rows` (unsigned_integer), `selected_key` (text).

| Interaction | Payload |
|---|---|
| `selected_key_changed` | `std::string` |
| `drop_down_changed` | `bool` |

## `about_dialog` (AboutDialog, component)

| Interaction | Payload |
|---|---|
| `closed` | `DialogResult` |

## `folder_browser_dialog` (FolderBrowserDialog, component)

Interactions: none.

## `color_dialog` (ColorDialog, component)

Interactions: none.

## `tool_tip` (ToolTip, component)

| Interaction | Payload |
|---|---|
| `visibility_changed` | `ToolTipEvent` |

## `ribbon_tool_tip` (RibbonToolTip, component)

| Interaction | Payload |
|---|---|
| `visibility_changed` | `RibbonToolTipEvent` |
| `help_requested` | `RibbonToolTipHelpRequest` |

## `drawing_surface` (DrawingSurface, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: none.

Interactions beyond every control's: none.

## `raster_canvas` (RasterCanvas, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `image` | icon |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: none.

Interactions beyond every control's: none.

## `live_surface` (LiveSurface, service)

Interactions: none.

## `sub_surface_host` (SubSurfaceHost, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `content` | content |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `stopped` | `SubSurfaceStopped` |
| `pairing_missed` | `std::uint64_t` |

## `metrics_view` (MetricsView, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `face` | face |  |  |
| `accent_strip` | keyline |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: none.

Interactions beyond every control's: none.

## `timer` (Timer, component)

| Interaction | Payload |
|---|---|
| `tick` | none |

## `background_worker` (BackgroundWorker, component)

| Interaction | Payload |
|---|---|
| `progress_changed` | `BackgroundWorkerProgress` |
| `run_worker_completed` | `BackgroundWorkerCompleted` |

## `notify_icon` (NotifyIcon, component)

| Interaction | Payload |
|---|---|
| `click` | `NotifyIconClick` |
| `double_click` | none |
| `balloon_tip_clicked` | none |
| `balloon_tip_closed` | none |
| `menu_item_invoked` | `MenuItemInvocation` |

## `open_file_dialog` (OpenFileDialog, component)

| Interaction | Payload |
|---|---|
| `file_ok` | `FileDialogOk`, by reference |

## `save_file_dialog` (SaveFileDialog, component)

| Interaction | Payload |
|---|---|
| `file_ok` | `FileDialogOk`, by reference |

## `image_list` (ImageList, component)

| Interaction | Payload |
|---|---|
| `changed` | `ImageListChange` |

## `error_provider` (ErrorProvider, component)

| Interaction | Payload |
|---|---|
| `right_to_left_changed` | `bool` |
| `data_source_changed` | none |
| `data_member_changed` | `std::string` |
| `error_changed` | `ErrorProviderChange` |

## `help_provider` (HelpProvider, component)

| Interaction | Payload |
|---|---|
| `help_requested` | `HelpRequestEvent`, by reference |

## `help_book` (HelpBook, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `paper` | face |  |  |
| `title` | label |  |  |
| `rule` | divider |  |  |
| `close` | glyph |  |  |
| `body` | content |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Item system `topic`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `topic_bar` | face |  |  |
| `expander` | disclosure |  |  |

Item states: `hot`, `pressed`, `open`, `closed`.

Item system `link`:

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `link_text` | label |  |  |

Item states: `hot`, `pressed`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |
| `link_activated` | `HelpLinkActivation`, by reference |
| `close_requested` | none |
| `section_toggled` | `HelpSectionToggle` |

## `help_book_viewer` (HelpBookViewer, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `focus` | focus |  |  |

States: `normal`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: none.

| Interaction | Payload |
|---|---|
| `navigated` | `std::string` |
| `external_link` | `HelpLinkActivation`, by reference |
| `popup_requested` | `HelpLinkActivation`, by reference |
| `print_requested` | `HelpPrintRequest`, by reference |
| `bookmarks_changed` | `std::vector<std::string>` |
| `built_in_language_requested` | none |

## `help_book_system` (HelpBookSystem, component)

| Interaction | Payload |
|---|---|
| `external_link` | `HelpLinkActivation`, by reference |
| `print_requested` | `HelpPrintRequest`, by reference |
| `bookmarks_changed` | `std::vector<std::string>` |

## `semantic_feedback` (SemanticFeedback, component)

| Interaction | Payload |
|---|---|
| `emitted` | `SemanticFeedbackRecord` |

## `binding` (Binding, component)

| Interaction | Payload |
|---|---|
| `format` | `BindingConvertEvent`, by reference |
| `parse` | `BindingConvertEvent`, by reference |
| `binding_complete` | `BindingCompleteEvent`, by reference |

## `binding_source` (BindingSource, component)

| Interaction | Payload |
|---|---|
| `list_changed` | `BindingListChange` |
| `current_changed` | none |
| `current_item_changed` | none |
| `position_changed` | `std::ptrdiff_t` |
| `data_error` | `std::string` |
| `data_source_changed` | none |
| `data_member_changed` | `std::string` |
| `binding_complete` | `BindingCompleteEvent`, by reference |
| `disposed_event` | none |

## `binding_context` (BindingContext, component)

| Interaction | Payload |
|---|---|
| `collection_changed` | `BindingContextChange` |

## `text_box_base` (TextBoxBase, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |

## `list_control` (ListControl, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |

## `up_down_base` (UpDownBase, control)

| Part | Role | Reuses | Own states |
|---|---|---|---|
| `region` | region |  |  |
| `viewport` | region |  |  |
| `horizontal_decrement_arrow` | actuator | `button` | hot, pressed |
| `horizontal_track` | track |  | hot, pressed |
| `horizontal_thumb` | thumb |  | hot, pressed, captured |
| `horizontal_increment_arrow` | actuator | `button` | hot, pressed |
| `vertical_decrement_arrow` | actuator | `button` | hot, pressed |
| `vertical_track` | track |  | hot, pressed |
| `vertical_thumb` | thumb |  | hot, pressed, captured |
| `vertical_increment_arrow` | actuator | `button` | hot, pressed |
| `scroll_corner` | face |  |  |
| `focus` | focus |  |  |
| `face` | face |  |  |
| `edge` | edge |  |  |

States: `normal`, `pressed`, `captured`, `enabled`, `disabled`, `focused`, `focus_visible`.

Properties beyond every control's: `auto_scroll` (boolean), `auto_scroll_margin` (size), `auto_scroll_min_size` (size), `auto_scroll_position` (point), `border_style` (enumeration), `wheel_scrolls` (boolean).

| Interaction | Payload |
|---|---|
| `scroll` | `ScrollEvent`, by reference |

## `window` (Window, component)

| Interaction | Payload |
|---|---|
| `active_changed` | `bool` |
| `backdrop_changed` | `WindowBackdropGrant` |
| `dialog_result_changed` | `DialogResult` |
| `focused_control_changed` | `FocusedControlChange` |
| `help_requested` | `HelpRequestEvent`, by reference |
| `key_down` | `KeyEventArgs`, by reference |
| `key_press` | `KeyPressEventArgs`, by reference |
| `key_state_changed` | `KeyStateChange` |
| `key_up` | `KeyEventArgs`, by reference |
| `presentation_changed` | `PresentationSettings` |
| `theme_changed` | `Theme` |
