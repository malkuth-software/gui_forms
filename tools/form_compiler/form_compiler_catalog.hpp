// Generated from manifests/controls.json by `python3 tools/control_registry.py emit-forms`.
// Do not edit; `python3 tools/control_registry.py emit-forms --check` fails when stale.
// Included only by form_catalog.cpp: every registry type in registry order, with its
// C++ class, its public header and the recipe that adds it to the form schema.

#pragma once

#include "form_catalog.hpp"

#include "gui_forms/background_worker/background_worker.hpp"
#include "gui_forms/binding/binding/binding.hpp"
#include "gui_forms/binding/binding_context/binding_context.hpp"
#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/commands/color_command/color_command.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/commands/font_command/font_command.hpp"
#include "gui_forms/commands/gallery_command/gallery_command.hpp"
#include "gui_forms/components/color_dialog/color_dialog.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/components/error_provider/error_provider.hpp"
#include "gui_forms/components/folder_browser_dialog/folder_browser_dialog.hpp"
#include "gui_forms/components/help_provider/help_provider.hpp"
#include "gui_forms/components/notify_icon/notify_icon.hpp"
#include "gui_forms/components/open_file_dialog/open_file_dialog.hpp"
#include "gui_forms/components/ribbon_tool_tip/ribbon_tool_tip.hpp"
#include "gui_forms/components/save_file_dialog/save_file_dialog.hpp"
#include "gui_forms/components/thread_exception_dialog/thread_exception_dialog.hpp"
#include "gui_forms/components/tool_tip/tool_tip.hpp"
#include "gui_forms/controls/backstage_view/backstage_view.hpp"
#include "gui_forms/controls/binding_navigator/binding_navigator.hpp"
#include "gui_forms/controls/button_base/button/button.hpp"
#include "gui_forms/controls/button_base/button/drop_down_button/drop_down_button.hpp"
#include "gui_forms/controls/button_base/button_base.hpp"
#include "gui_forms/controls/button_base/check_box/check_box.hpp"
#include "gui_forms/controls/button_base/link_label/link_label.hpp"
#include "gui_forms/controls/button_base/radio_button/radio_button.hpp"
#include "gui_forms/controls/collections/choice_group.hpp"
#include "gui_forms/controls/collections/command_bar.hpp"
#include "gui_forms/controls/collections/expandable_sections.hpp"
#include "gui_forms/controls/container/master_detail_view/master_detail_view.hpp"
#include "gui_forms/controls/data_grid_view/data_grid_view.hpp"
#include "gui_forms/controls/docking/dock_navigator/dock_navigator.hpp"
#include "gui_forms/controls/docking/dock_site/dock_site.hpp"
#include "gui_forms/controls/docking/dock_workspace/dock_workspace.hpp"
#include "gui_forms/controls/drawing_surface/drawing_surface.hpp"
#include "gui_forms/controls/easing_preview/easing_preview.hpp"
#include "gui_forms/controls/instruments/sweep_scope/sweep_scope.hpp"
#include "gui_forms/controls/label/label.hpp"
#include "gui_forms/controls/menu_strip/menu_strip.hpp"
#include "gui_forms/controls/metrics_view/metrics_view.hpp"
#include "gui_forms/controls/pages/navigation_pane/navigation_pane.hpp"
#include "gui_forms/controls/pages/page_strip/page_strip.hpp"
#include "gui_forms/controls/pages/page_view/page_view.hpp"
#include "gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_layer.hpp"
#include "gui_forms/controls/panel/breadcrumb_trail/breadcrumb_trail.hpp"
#include "gui_forms/controls/panel/card/card.hpp"
#include "gui_forms/controls/panel/card/review_card/review_card.hpp"
#include "gui_forms/controls/panel/color_value_editor/color_value_editor.hpp"
#include "gui_forms/controls/panel/combo_box/combo_box.hpp"
#include "gui_forms/controls/panel/correspondence_view/correspondence_view.hpp"
#include "gui_forms/controls/panel/date_time_picker/date_time_picker.hpp"
#include "gui_forms/controls/panel/domain_up_down/domain_up_down.hpp"
#include "gui_forms/controls/panel/flags_value_editor/flags_value_editor.hpp"
#include "gui_forms/controls/panel/font_dialog/font_dialog.hpp"
#include "gui_forms/controls/panel/group_box/group_box.hpp"
#include "gui_forms/controls/panel/group_box/scaled_group_box/scaled_group_box.hpp"
#include "gui_forms/controls/panel/instrument_rack/instrument_rack.hpp"
#include "gui_forms/controls/panel/list_box/checked_list_box/checked_list_box.hpp"
#include "gui_forms/controls/panel/list_box/list_box.hpp"
#include "gui_forms/controls/panel/list_view/list_view.hpp"
#include "gui_forms/controls/panel/masked_text_box/masked_text_box.hpp"
#include "gui_forms/controls/panel/material_panel/material_panel.hpp"
#include "gui_forms/controls/panel/month_calendar/month_calendar.hpp"
#include "gui_forms/controls/panel/numeric_up_down/numeric_up_down.hpp"
#include "gui_forms/controls/panel/object_view/object_view.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/controls/panel/picture_box/picture_box.hpp"
#include "gui_forms/controls/panel/property_grid/property_grid.hpp"
#include "gui_forms/controls/panel/property_list/property_list.hpp"
#include "gui_forms/controls/panel/rich_text_box/rich_text_box.hpp"
#include "gui_forms/controls/panel/scaled_panel/scaled_panel.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/controls/panel/token_box/token_box.hpp"
#include "gui_forms/controls/panel/tree_list_view/tree_list_view.hpp"
#include "gui_forms/controls/panel/tree_view/tree_view.hpp"
#include "gui_forms/controls/quick_access_toolbar/quick_access_toolbar.hpp"
#include "gui_forms/controls/range_control/progress_bar/progress_bar.hpp"
#include "gui_forms/controls/range_control/range_control.hpp"
#include "gui_forms/controls/range_control/scroll_bar/h_scroll_bar/h_scroll_bar.hpp"
#include "gui_forms/controls/range_control/scroll_bar/scroll_bar.hpp"
#include "gui_forms/controls/range_control/scroll_bar/v_scroll_bar/v_scroll_bar.hpp"
#include "gui_forms/controls/range_control/track_bar/track_bar.hpp"
#include "gui_forms/controls/raster_canvas/raster_canvas.hpp"
#include "gui_forms/controls/ribbon/ribbon.hpp"
#include "gui_forms/controls/ribbon_color_picker/ribbon_color_picker.hpp"
#include "gui_forms/controls/ribbon_font_control/ribbon_font_control.hpp"
#include "gui_forms/controls/ribbon_gallery/ribbon_gallery.hpp"
#include "gui_forms/controls/scrollable_control/container_control/block_panel/block_panel.hpp"
#include "gui_forms/controls/scrollable_control/container_control/command_overflow_panel/command_overflow_panel.hpp"
#include "gui_forms/controls/scrollable_control/container_control/container_control.hpp"
#include "gui_forms/controls/scrollable_control/container_control/flex_panel/flex_panel.hpp"
#include "gui_forms/controls/scrollable_control/container_control/flow_layout_panel_compat/flow_layout_panel_compat.hpp"
#include "gui_forms/controls/scrollable_control/container_control/grid_panel/grid_panel.hpp"
#include "gui_forms/controls/scrollable_control/container_control/responsive_track_panel/responsive_track_panel.hpp"
#include "gui_forms/controls/scrollable_control/container_control/split_container/split_container.hpp"
#include "gui_forms/controls/scrollable_control/container_control/split_container/splitter_panel/splitter_panel.hpp"
#include "gui_forms/controls/scrollable_control/container_control/tab_control/tab_control.hpp"
#include "gui_forms/controls/scrollable_control/container_control/tab_control/tab_page/tab_page.hpp"
#include "gui_forms/controls/scrollable_control/container_control/table_layout_panel_compat/table_layout_panel_compat.hpp"
#include "gui_forms/controls/scrollable_control/container_control/user_control/user_control.hpp"
#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"
#include "gui_forms/controls/splitter/splitter.hpp"
#include "gui_forms/controls/status_strip/status_strip.hpp"
#include "gui_forms/controls/task_dialog/task_dialog.hpp"
#include "gui_forms/controls/text_block/text_block.hpp"
#include "gui_forms/controls/tool_strip/tool_strip.hpp"
#include "gui_forms/controls/tool_strip_container/tool_strip_container.hpp"
#include "gui_forms/controls/tool_strip_panel/tool_strip_panel.hpp"
#include "gui_forms/controls/typography_specimen/typography_specimen.hpp"
#include "gui_forms/controls/visual_inspector_view/visual_inspector_view.hpp"
#include "gui_forms/feedback/semantic_feedback/semantic_feedback.hpp"
#include "gui_forms/image_list/image_list/image_list.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"
#include "gui_forms/timer/timer/timer.hpp"

#include <array>

namespace gui_forms::form_compiler {

inline constexpr std::array<FormCatalogEntry, 117> form_catalog{{
    {"button_base", "ButtonBase", "gui_forms/controls/button_base/button_base.hpp", &add_catalog_type<ButtonBase>},
    {"button", "Button", "gui_forms/controls/button_base/button/button.hpp", &add_catalog_type<Button>},
    {"drop_down_button", "DropDownButton", "gui_forms/controls/button_base/button/drop_down_button/drop_down_button.hpp", &add_catalog_type<DropDownButton>},
    {"link_label", "LinkLabel", "gui_forms/controls/button_base/link_label/link_label.hpp", &add_catalog_type<LinkLabel>},
    {"command_bar", "CommandBar", "gui_forms/controls/collections/command_bar.hpp", &add_catalog_type<CommandBar>},
    {"text_box", "TextBox", "gui_forms/controls/panel/text_box/text_box.hpp", &add_catalog_type<TextBox>},
    {"token_box", "TokenBox", "gui_forms/controls/panel/token_box/token_box.hpp", &add_catalog_type<TokenBox>},
    {"masked_text_box", "MaskedTextBox", "gui_forms/controls/panel/masked_text_box/masked_text_box.hpp", &add_catalog_type<MaskedTextBox>},
    {"rich_text_box", "RichTextBox", "gui_forms/controls/panel/rich_text_box/rich_text_box.hpp", &add_catalog_type<RichTextBox>},
    {"label", "Label", "gui_forms/controls/label/label.hpp", &add_catalog_type<Label>},
    {"text_block", "TextBlock", "gui_forms/controls/text_block/text_block.hpp", &add_catalog_type<TextBlock>},
    {"picture_box", "PictureBox", "gui_forms/controls/panel/picture_box/picture_box.hpp", &add_catalog_type<PictureBox>},
    {"progress_bar", "ProgressBar", "gui_forms/controls/range_control/progress_bar/progress_bar.hpp", &add_catalog_type<ProgressBar>},
    {"sweep_scope", "SweepScope", "gui_forms/controls/instruments/sweep_scope/sweep_scope.hpp", &add_catalog_type<SweepScope>},
    {"review_card", "ReviewCard", "gui_forms/controls/panel/card/review_card/review_card.hpp", &add_catalog_type<ReviewCard>},
    {"list_box", "ListBox", "gui_forms/controls/panel/list_box/list_box.hpp", &add_catalog_type<ListBox>},
    {"checked_list_box", "CheckedListBox", "gui_forms/controls/panel/list_box/checked_list_box/checked_list_box.hpp", &add_catalog_type<CheckedListBox>},
    {"combo_box", "ComboBox", "gui_forms/controls/panel/combo_box/combo_box.hpp", &add_catalog_type<ComboBox>},
    {"tree_view", "TreeView", "gui_forms/controls/panel/tree_view/tree_view.hpp", &add_catalog_type<TreeView>},
    {"object_view", "ObjectView", "gui_forms/controls/panel/object_view/object_view.hpp", &add_catalog_type<ObjectView>},
    {"list_view", "ListView", "gui_forms/controls/panel/list_view/list_view.hpp", &add_catalog_type<ListView>},
    {"tree_list_view", "TreeListView", "gui_forms/controls/panel/tree_list_view/tree_list_view.hpp", &add_catalog_type<TreeListView>},
    {"correspondence_view", "CorrespondenceView", "gui_forms/controls/panel/correspondence_view/correspondence_view.hpp", &add_catalog_type<CorrespondenceView>},
    {"data_grid_view", "DataGridView", "gui_forms/controls/data_grid_view/data_grid_view.hpp", &add_catalog_type<DataGridView>},
    {"choice_group", "ChoiceGroup", "gui_forms/controls/collections/choice_group.hpp", &add_catalog_type<ChoiceGroup>},
    {"breadcrumb_trail", "BreadcrumbTrail", "gui_forms/controls/panel/breadcrumb_trail/breadcrumb_trail.hpp", &add_catalog_type<BreadcrumbTrail>},
    {"check_box", "CheckBox", "gui_forms/controls/button_base/check_box/check_box.hpp", &add_catalog_type<CheckBox>},
    {"radio_button", "RadioButton", "gui_forms/controls/button_base/radio_button/radio_button.hpp", &add_catalog_type<RadioButton>},
    {"range_control", "RangeControl", "gui_forms/controls/range_control/range_control.hpp", &add_catalog_type<RangeControl>},
    {"track_bar", "TrackBar", "gui_forms/controls/range_control/track_bar/track_bar.hpp", &add_catalog_type<TrackBar>},
    {"numeric_up_down", "NumericUpDown", "gui_forms/controls/panel/numeric_up_down/numeric_up_down.hpp", &add_catalog_type<NumericUpDown>},
    {"domain_up_down", "DomainUpDown", "gui_forms/controls/panel/domain_up_down/domain_up_down.hpp", &add_catalog_type<DomainUpDown>},
    {"color_value_editor", "ColorValueEditor", "gui_forms/controls/panel/color_value_editor/color_value_editor.hpp", &add_catalog_type<ColorValueEditor>},
    {"flags_value_editor", "FlagsValueEditor", "gui_forms/controls/panel/flags_value_editor/flags_value_editor.hpp", &add_catalog_type<FlagsValueEditor>},
    {"property_grid", "PropertyGrid", "gui_forms/controls/panel/property_grid/property_grid.hpp", &add_catalog_type<PropertyGrid>},
    {"property_list", "PropertyList", "gui_forms/controls/panel/property_list/property_list.hpp", &add_catalog_type<PropertyList>},
    {"instrument_rack", "InstrumentRack", "gui_forms/controls/panel/instrument_rack/instrument_rack.hpp", &add_catalog_type<InstrumentRack>},
    {"date_time_picker", "DateTimePicker", "gui_forms/controls/panel/date_time_picker/date_time_picker.hpp", &add_catalog_type<DateTimePicker>},
    {"month_calendar", "MonthCalendar", "gui_forms/controls/panel/month_calendar/month_calendar.hpp", &add_catalog_type<MonthCalendar>},
    {"panel", "Panel", "gui_forms/controls/panel/panel.hpp", &add_catalog_type<Panel>},
    {"group_box", "GroupBox", "gui_forms/controls/panel/group_box/group_box.hpp", &add_catalog_type<GroupBox>},
    {"scaled_group_box", "ScaledGroupBox", "gui_forms/controls/panel/group_box/scaled_group_box/scaled_group_box.hpp", &add_catalog_type<ScaledGroupBox>},
    {"material_panel", "MaterialPanel", "gui_forms/controls/panel/material_panel/material_panel.hpp", &add_catalog_type<MaterialPanel>},
    {"card", "Card", "gui_forms/controls/panel/card/card.hpp", &add_catalog_type<Card>},
    {"expandable_sections", "ExpandableSections", "gui_forms/controls/collections/expandable_sections.hpp", &add_catalog_type<ExpandableSections>},
    {"container_control", "ContainerControl", "gui_forms/controls/scrollable_control/container_control/container_control.hpp", &add_catalog_type<ContainerControl>},
    {"user_control", "UserControl", "gui_forms/controls/scrollable_control/container_control/user_control/user_control.hpp", &add_catalog_type<UserControl>},
    {"tab_control", "TabControl", "gui_forms/controls/scrollable_control/container_control/tab_control/tab_control.hpp", &add_catalog_type<TabControl>},
    {"page_strip", "PageStrip", "gui_forms/controls/pages/page_strip/page_strip.hpp", &add_catalog_type<PageStrip>},
    {"page_view", "PageView", "gui_forms/controls/pages/page_view/page_view.hpp", &add_catalog_type<PageView>},
    {"navigation_pane", "NavigationPane", "gui_forms/controls/pages/navigation_pane/navigation_pane.hpp", &add_catalog_type<NavigationPane>},
    {"dock_site", "DockSite", "gui_forms/controls/docking/dock_site/dock_site.hpp", &add_catalog_type<DockSite>},
    {"dock_navigator", "DockNavigator", "gui_forms/controls/docking/dock_navigator/dock_navigator.hpp", &add_catalog_type<DockNavigator>},
    {"tab_page", "TabPage", "gui_forms/controls/scrollable_control/container_control/tab_control/tab_page/tab_page.hpp", &add_catalog_type<TabPage>},
    {"flex_panel", "FlexPanel", "gui_forms/controls/scrollable_control/container_control/flex_panel/flex_panel.hpp", &add_catalog_type<FlexPanel>},
    {"grid_panel", "GridPanel", "gui_forms/controls/scrollable_control/container_control/grid_panel/grid_panel.hpp", &add_catalog_type<GridPanel>},
    {"block_panel", "BlockPanel", "gui_forms/controls/scrollable_control/container_control/block_panel/block_panel.hpp", &add_catalog_type<BlockPanel>},
    {"flow_layout_panel_compat", "FlowLayoutPanelCompat", "gui_forms/controls/scrollable_control/container_control/flow_layout_panel_compat/flow_layout_panel_compat.hpp", &add_catalog_type<FlowLayoutPanelCompat>},
    {"table_layout_panel_compat", "TableLayoutPanelCompat", "gui_forms/controls/scrollable_control/container_control/table_layout_panel_compat/table_layout_panel_compat.hpp", &add_catalog_type<TableLayoutPanelCompat>},
    {"responsive_track_panel", "ResponsiveTrackPanel", "gui_forms/controls/scrollable_control/container_control/responsive_track_panel/responsive_track_panel.hpp", &add_catalog_type<ResponsiveTrackPanel>},
    {"scaled_panel", "ScaledPanel", "gui_forms/controls/panel/scaled_panel/scaled_panel.hpp", &add_catalog_type<ScaledPanel>},
    {"split_container", "SplitContainer", "gui_forms/controls/scrollable_control/container_control/split_container/split_container.hpp", &add_catalog_type<SplitContainer>},
    {"splitter_panel", "SplitterPanel", "gui_forms/controls/scrollable_control/container_control/split_container/splitter_panel/splitter_panel.hpp", &add_catalog_type<SplitterPanel>},
    {"splitter", "Splitter", "gui_forms/controls/splitter/splitter.hpp", &add_catalog_type<Splitter>},
    {"command_overflow_panel", "CommandOverflowPanel", "gui_forms/controls/scrollable_control/container_control/command_overflow_panel/command_overflow_panel.hpp", &add_catalog_type<CommandOverflowPanel>},
    {"master_detail_view", "MasterDetailView", "gui_forms/controls/container/master_detail_view/master_detail_view.hpp", &add_catalog_type<MasterDetailView>},
    {"scrollable_control", "ScrollableControl", "gui_forms/controls/scrollable_control/scrollable_control.hpp", &add_catalog_type<ScrollableControl>},
    {"scroll_bar", "ScrollBar", "gui_forms/controls/range_control/scroll_bar/scroll_bar.hpp", &add_catalog_type<ScrollBar>},
    {"h_scroll_bar", "HScrollBar", "gui_forms/controls/range_control/scroll_bar/h_scroll_bar/h_scroll_bar.hpp", &add_catalog_type<HScrollBar>},
    {"v_scroll_bar", "VScrollBar", "gui_forms/controls/range_control/scroll_bar/v_scroll_bar/v_scroll_bar.hpp", &add_catalog_type<VScrollBar>},
    {"menu_strip", "MenuStrip", "gui_forms/controls/menu_strip/menu_strip.hpp", &add_catalog_type<MenuStrip>},
    {"tool_strip", "ToolStrip", "gui_forms/controls/tool_strip/tool_strip.hpp", &add_catalog_type<ToolStrip>},
    {"ribbon_font_control", "RibbonFontControl", "gui_forms/controls/ribbon_font_control/ribbon_font_control.hpp", &add_catalog_type<RibbonFontControl>},
    {"ribbon_color_picker", "RibbonColorPicker", "gui_forms/controls/ribbon_color_picker/ribbon_color_picker.hpp", &add_catalog_type<RibbonColorPicker>},
    {"backstage_view", "BackstageView", "gui_forms/controls/backstage_view/backstage_view.hpp", &add_catalog_type<BackstageView>},
    {"ribbon_gallery", "RibbonGallery", "gui_forms/controls/ribbon_gallery/ribbon_gallery.hpp", &add_catalog_type<RibbonGallery>},
    {"status_strip", "StatusStrip", "gui_forms/controls/status_strip/status_strip.hpp", &add_catalog_type<StatusStrip>},
    {"quick_access_toolbar", "QuickAccessToolbar", "gui_forms/controls/quick_access_toolbar/quick_access_toolbar.hpp", &add_catalog_type<QuickAccessToolbar>},
    {"tool_strip_panel", "ToolStripPanel", "gui_forms/controls/tool_strip_panel/tool_strip_panel.hpp", &add_catalog_type<ToolStripPanel>},
    {"tool_strip_container", "ToolStripContainer", "gui_forms/controls/tool_strip_container/tool_strip_container.hpp", &add_catalog_type<ToolStripContainer>},
    {"binding_navigator", "BindingNavigator", "gui_forms/controls/binding_navigator/binding_navigator.hpp", &add_catalog_type<BindingNavigator>},
    {"ribbon", "Ribbon", "gui_forms/controls/ribbon/ribbon.hpp", &add_catalog_type<Ribbon>},
    {"context_menu", "ContextMenu", "gui_forms/components/context_menu/context_menu.hpp", &add_catalog_type<ContextMenu>},
    {"dock_workspace", "DockWorkspace", "gui_forms/controls/docking/dock_workspace/dock_workspace.hpp", &add_catalog_type<DockWorkspace>},
    {"command", "Command", "gui_forms/commands/command/command.hpp", &add_catalog_type<Command>},
    {"font_command", "FontCommand", "gui_forms/commands/font_command/font_command.hpp", &add_catalog_type<FontCommand>},
    {"color_command", "ColorCommand", "gui_forms/commands/color_command/color_command.hpp", &add_catalog_type<ColorCommand>},
    {"gallery_command", "GalleryCommand", "gui_forms/commands/gallery_command/gallery_command.hpp", &add_catalog_type<GalleryCommand>},
    {"anchored_overlay_layer", "AnchoredOverlayLayer", "gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_layer.hpp", &add_catalog_type<AnchoredOverlayLayer>},
    {"font_dialog", "FontDialog", "gui_forms/controls/panel/font_dialog/font_dialog.hpp", &add_catalog_type<FontDialog>},
    {"task_dialog", "TaskDialog", "gui_forms/controls/task_dialog/task_dialog.hpp", &add_catalog_type<TaskDialog>},
    {"thread_exception_dialog", "ThreadExceptionDialog", "gui_forms/components/thread_exception_dialog/thread_exception_dialog.hpp", &add_catalog_type<ThreadExceptionDialog>},
    {"folder_browser_dialog", "FolderBrowserDialog", "gui_forms/components/folder_browser_dialog/folder_browser_dialog.hpp", &add_catalog_type<FolderBrowserDialog>},
    {"color_dialog", "ColorDialog", "gui_forms/components/color_dialog/color_dialog.hpp", &add_catalog_type<ColorDialog>},
    {"tool_tip", "ToolTip", "gui_forms/components/tool_tip/tool_tip.hpp", &add_catalog_type<ToolTip>},
    {"ribbon_tool_tip", "RibbonToolTip", "gui_forms/components/ribbon_tool_tip/ribbon_tool_tip.hpp", &add_catalog_type<RibbonToolTip>},
    {"drawing_surface", "DrawingSurface", "gui_forms/controls/drawing_surface/drawing_surface.hpp", &add_catalog_type<DrawingSurface>},
    {"raster_canvas", "RasterCanvas", "gui_forms/controls/raster_canvas/raster_canvas.hpp", &add_catalog_type<RasterCanvas>},
    {"live_surface", "LiveSurface", "gui_forms/live_surface/surface/live_surface.hpp", &add_catalog_type<LiveSurface>},
    {"sub_surface_host", "SubSurfaceHost", "gui_forms/surface_root/sub_surface/sub_surface_host.hpp", &add_catalog_type<SubSurfaceHost>},
    {"easing_preview", "EasingPreview", "gui_forms/controls/easing_preview/easing_preview.hpp", &add_catalog_type<EasingPreview>},
    {"metrics_view", "MetricsView", "gui_forms/controls/metrics_view/metrics_view.hpp", &add_catalog_type<MetricsView>},
    {"typography_specimen", "TypographySpecimen", "gui_forms/controls/typography_specimen/typography_specimen.hpp", &add_catalog_type<TypographySpecimen>},
    {"visual_inspector_view", "VisualInspectorView", "gui_forms/controls/visual_inspector_view/visual_inspector_view.hpp", &add_catalog_type<VisualInspectorView>},
    {"visual_inspector_overlay", "VisualInspectorOverlay", "gui_forms/controls/visual_inspector_view/visual_inspector_view.hpp", &add_catalog_type<VisualInspectorOverlay>},
    {"timer", "Timer", "gui_forms/timer/timer/timer.hpp", &add_catalog_type<Timer>},
    {"background_worker", "BackgroundWorker", "gui_forms/background_worker/background_worker.hpp", &add_catalog_type<BackgroundWorker>},
    {"notify_icon", "NotifyIcon", "gui_forms/components/notify_icon/notify_icon.hpp", &add_catalog_type<NotifyIcon>},
    {"open_file_dialog", "OpenFileDialog", "gui_forms/components/open_file_dialog/open_file_dialog.hpp", &add_catalog_type<OpenFileDialog>},
    {"save_file_dialog", "SaveFileDialog", "gui_forms/components/save_file_dialog/save_file_dialog.hpp", &add_catalog_type<SaveFileDialog>},
    {"image_list", "ImageList", "gui_forms/image_list/image_list/image_list.hpp", &add_catalog_type<ImageList>},
    {"error_provider", "ErrorProvider", "gui_forms/components/error_provider/error_provider.hpp", &add_catalog_type<ErrorProvider>},
    {"help_provider", "HelpProvider", "gui_forms/components/help_provider/help_provider.hpp", &add_catalog_type<HelpProvider>},
    {"semantic_feedback", "SemanticFeedback", "gui_forms/feedback/semantic_feedback/semantic_feedback.hpp", &add_catalog_type<SemanticFeedback>},
    {"binding", "Binding", "gui_forms/binding/binding/binding.hpp", &add_catalog_type<Binding>},
    {"binding_source", "BindingSource", "gui_forms/binding/binding_source/binding_source.hpp", &add_catalog_type<BindingSource>},
    {"binding_context", "BindingContext", "gui_forms/binding/binding_context/binding_context.hpp", &add_catalog_type<BindingContext>},
}};

} // namespace gui_forms::form_compiler
