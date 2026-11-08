// Generated from manifests/controls.json by `python3 tools/control_registry.py emit-abi`.
// Do not edit; `python3 tools/control_registry.py emit-abi --check` fails when stale.
// Included only by abi_schema.cpp: every registry type in registry order, with the
// recipes the C ABI describes and creates it by (abi_schema_recipes.hpp).

#pragma once

#include "abi_schema_recipes.hpp"

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
#include "gui_forms/controls/badge/badge.hpp"
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
#include "gui_forms/controls/empty_state/empty_state.hpp"
#include "gui_forms/controls/info_banner/info_banner.hpp"
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
#include "gui_forms/controls/toast/toast.hpp"
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

namespace gui_forms::abi1 {

inline constexpr std::array<SchemaCatalogEntry, 121> schema_catalog{{
    {"button_base", "ButtonBase", &describe_type<ButtonBase>, &create_type<ButtonBase>, &is_exact_type<ButtonBase>},
    {"button", "Button", &describe_type<Button>, &create_type<Button>, &is_exact_type<Button>},
    {"drop_down_button", "DropDownButton", &describe_type<DropDownButton>, &create_type<DropDownButton>, &is_exact_type<DropDownButton>},
    {"link_label", "LinkLabel", &describe_type<LinkLabel>, &create_type<LinkLabel>, &is_exact_type<LinkLabel>},
    {"command_bar", "CommandBar", &describe_type<CommandBar>, &create_type<CommandBar>, &is_exact_type<CommandBar>},
    {"text_box", "TextBox", &describe_type<TextBox>, &create_type<TextBox>, &is_exact_type<TextBox>},
    {"token_box", "TokenBox", &describe_type<TokenBox>, &create_type<TokenBox>, &is_exact_type<TokenBox>},
    {"masked_text_box", "MaskedTextBox", &describe_type<MaskedTextBox>, &create_type<MaskedTextBox>, &is_exact_type<MaskedTextBox>},
    {"rich_text_box", "RichTextBox", &describe_type<RichTextBox>, &create_type<RichTextBox>, &is_exact_type<RichTextBox>},
    {"label", "Label", &describe_type<Label>, &create_type<Label>, &is_exact_type<Label>},
    {"text_block", "TextBlock", &describe_type<TextBlock>, &create_type<TextBlock>, &is_exact_type<TextBlock>},
    {"badge", "Badge", &describe_type<Badge>, &create_type<Badge>, &is_exact_type<Badge>},
    {"info_banner", "InfoBanner", &describe_type<InfoBanner>, &create_type<InfoBanner>, &is_exact_type<InfoBanner>},
    {"empty_state", "EmptyState", &describe_type<EmptyState>, &create_type<EmptyState>, &is_exact_type<EmptyState>},
    {"toast", "Toast", &describe_type<Toast>, &create_type<Toast>, &is_exact_type<Toast>},
    {"picture_box", "PictureBox", &describe_type<PictureBox>, &create_type<PictureBox>, &is_exact_type<PictureBox>},
    {"progress_bar", "ProgressBar", &describe_type<ProgressBar>, &create_type<ProgressBar>, &is_exact_type<ProgressBar>},
    {"sweep_scope", "SweepScope", &describe_type<SweepScope>, &create_type<SweepScope>, &is_exact_type<SweepScope>},
    {"review_card", "ReviewCard", &describe_type<ReviewCard>, &create_type<ReviewCard>, &is_exact_type<ReviewCard>},
    {"list_box", "ListBox", &describe_type<ListBox>, &create_type<ListBox>, &is_exact_type<ListBox>},
    {"checked_list_box", "CheckedListBox", &describe_type<CheckedListBox>, &create_type<CheckedListBox>, &is_exact_type<CheckedListBox>},
    {"combo_box", "ComboBox", &describe_type<ComboBox>, &create_type<ComboBox>, &is_exact_type<ComboBox>},
    {"tree_view", "TreeView", &describe_type<TreeView>, &create_type<TreeView>, &is_exact_type<TreeView>},
    {"object_view", "ObjectView", &describe_type<ObjectView>, &create_type<ObjectView>, &is_exact_type<ObjectView>},
    {"list_view", "ListView", &describe_type<ListView>, &create_type<ListView>, &is_exact_type<ListView>},
    {"tree_list_view", "TreeListView", &describe_type<TreeListView>, &create_type<TreeListView>, &is_exact_type<TreeListView>},
    {"correspondence_view", "CorrespondenceView", &describe_type<CorrespondenceView>, &create_type<CorrespondenceView>, &is_exact_type<CorrespondenceView>},
    {"data_grid_view", "DataGridView", &describe_type<DataGridView>, &create_type<DataGridView>, &is_exact_type<DataGridView>},
    {"choice_group", "ChoiceGroup", &describe_type<ChoiceGroup>, &create_type<ChoiceGroup>, &is_exact_type<ChoiceGroup>},
    {"breadcrumb_trail", "BreadcrumbTrail", &describe_type<BreadcrumbTrail>, &create_type<BreadcrumbTrail>, &is_exact_type<BreadcrumbTrail>},
    {"check_box", "CheckBox", &describe_type<CheckBox>, &create_type<CheckBox>, &is_exact_type<CheckBox>},
    {"radio_button", "RadioButton", &describe_type<RadioButton>, &create_type<RadioButton>, &is_exact_type<RadioButton>},
    {"range_control", "RangeControl", &describe_type<RangeControl>, &create_type<RangeControl>, &is_exact_type<RangeControl>},
    {"track_bar", "TrackBar", &describe_type<TrackBar>, &create_type<TrackBar>, &is_exact_type<TrackBar>},
    {"numeric_up_down", "NumericUpDown", &describe_type<NumericUpDown>, &create_type<NumericUpDown>, &is_exact_type<NumericUpDown>},
    {"domain_up_down", "DomainUpDown", &describe_type<DomainUpDown>, &create_type<DomainUpDown>, &is_exact_type<DomainUpDown>},
    {"color_value_editor", "ColorValueEditor", &describe_type<ColorValueEditor>, &create_type<ColorValueEditor>, &is_exact_type<ColorValueEditor>},
    {"flags_value_editor", "FlagsValueEditor", &describe_type<FlagsValueEditor>, &create_type<FlagsValueEditor>, &is_exact_type<FlagsValueEditor>},
    {"property_grid", "PropertyGrid", &describe_type<PropertyGrid>, &create_type<PropertyGrid>, &is_exact_type<PropertyGrid>},
    {"property_list", "PropertyList", &describe_type<PropertyList>, &create_type<PropertyList>, &is_exact_type<PropertyList>},
    {"instrument_rack", "InstrumentRack", &describe_type<InstrumentRack>, &create_type<InstrumentRack>, &is_exact_type<InstrumentRack>},
    {"date_time_picker", "DateTimePicker", &describe_type<DateTimePicker>, &create_type<DateTimePicker>, &is_exact_type<DateTimePicker>},
    {"month_calendar", "MonthCalendar", &describe_type<MonthCalendar>, &create_type<MonthCalendar>, &is_exact_type<MonthCalendar>},
    {"panel", "Panel", &describe_type<Panel>, &create_type<Panel>, &is_exact_type<Panel>},
    {"group_box", "GroupBox", &describe_type<GroupBox>, &create_type<GroupBox>, &is_exact_type<GroupBox>},
    {"scaled_group_box", "ScaledGroupBox", &describe_type<ScaledGroupBox>, &create_type<ScaledGroupBox>, &is_exact_type<ScaledGroupBox>},
    {"material_panel", "MaterialPanel", &describe_type<MaterialPanel>, &create_type<MaterialPanel>, &is_exact_type<MaterialPanel>},
    {"card", "Card", &describe_type<Card>, &create_type<Card>, &is_exact_type<Card>},
    {"expandable_sections", "ExpandableSections", &describe_type<ExpandableSections>, &create_type<ExpandableSections>, &is_exact_type<ExpandableSections>},
    {"container_control", "ContainerControl", &describe_type<ContainerControl>, &create_type<ContainerControl>, &is_exact_type<ContainerControl>},
    {"user_control", "UserControl", &describe_type<UserControl>, &create_type<UserControl>, &is_exact_type<UserControl>},
    {"tab_control", "TabControl", &describe_type<TabControl>, &create_type<TabControl>, &is_exact_type<TabControl>},
    {"page_strip", "PageStrip", &describe_type<PageStrip>, &create_type<PageStrip>, &is_exact_type<PageStrip>},
    {"page_view", "PageView", &describe_type<PageView>, &create_type<PageView>, &is_exact_type<PageView>},
    {"navigation_pane", "NavigationPane", &describe_type<NavigationPane>, &create_type<NavigationPane>, &is_exact_type<NavigationPane>},
    {"dock_site", "DockSite", &describe_type<DockSite>, &create_type<DockSite>, &is_exact_type<DockSite>},
    {"dock_navigator", "DockNavigator", &describe_type<DockNavigator>, &create_type<DockNavigator>, &is_exact_type<DockNavigator>},
    {"tab_page", "TabPage", &describe_type<TabPage>, &create_type<TabPage>, &is_exact_type<TabPage>},
    {"flex_panel", "FlexPanel", &describe_type<FlexPanel>, &create_type<FlexPanel>, &is_exact_type<FlexPanel>},
    {"grid_panel", "GridPanel", &describe_type<GridPanel>, &create_type<GridPanel>, &is_exact_type<GridPanel>},
    {"block_panel", "BlockPanel", &describe_type<BlockPanel>, &create_type<BlockPanel>, &is_exact_type<BlockPanel>},
    {"flow_layout_panel_compat", "FlowLayoutPanelCompat", &describe_type<FlowLayoutPanelCompat>, &create_type<FlowLayoutPanelCompat>, &is_exact_type<FlowLayoutPanelCompat>},
    {"table_layout_panel_compat", "TableLayoutPanelCompat", &describe_type<TableLayoutPanelCompat>, &create_type<TableLayoutPanelCompat>, &is_exact_type<TableLayoutPanelCompat>},
    {"responsive_track_panel", "ResponsiveTrackPanel", &describe_type<ResponsiveTrackPanel>, &create_type<ResponsiveTrackPanel>, &is_exact_type<ResponsiveTrackPanel>},
    {"scaled_panel", "ScaledPanel", &describe_type<ScaledPanel>, &create_type<ScaledPanel>, &is_exact_type<ScaledPanel>},
    {"split_container", "SplitContainer", &describe_type<SplitContainer>, &create_type<SplitContainer>, &is_exact_type<SplitContainer>},
    {"splitter_panel", "SplitterPanel", &describe_type<SplitterPanel>, &create_type<SplitterPanel>, &is_exact_type<SplitterPanel>},
    {"splitter", "Splitter", &describe_type<Splitter>, &create_type<Splitter>, &is_exact_type<Splitter>},
    {"command_overflow_panel", "CommandOverflowPanel", &describe_type<CommandOverflowPanel>, &create_type<CommandOverflowPanel>, &is_exact_type<CommandOverflowPanel>},
    {"master_detail_view", "MasterDetailView", &describe_type<MasterDetailView>, &create_type<MasterDetailView>, &is_exact_type<MasterDetailView>},
    {"scrollable_control", "ScrollableControl", &describe_type<ScrollableControl>, &create_type<ScrollableControl>, &is_exact_type<ScrollableControl>},
    {"scroll_bar", "ScrollBar", &describe_type<ScrollBar>, &create_type<ScrollBar>, &is_exact_type<ScrollBar>},
    {"h_scroll_bar", "HScrollBar", &describe_type<HScrollBar>, &create_type<HScrollBar>, &is_exact_type<HScrollBar>},
    {"v_scroll_bar", "VScrollBar", &describe_type<VScrollBar>, &create_type<VScrollBar>, &is_exact_type<VScrollBar>},
    {"menu_strip", "MenuStrip", &describe_type<MenuStrip>, &create_type<MenuStrip>, &is_exact_type<MenuStrip>},
    {"tool_strip", "ToolStrip", &describe_type<ToolStrip>, &create_type<ToolStrip>, &is_exact_type<ToolStrip>},
    {"ribbon_font_control", "RibbonFontControl", &describe_type<RibbonFontControl>, &create_type<RibbonFontControl>, &is_exact_type<RibbonFontControl>},
    {"ribbon_color_picker", "RibbonColorPicker", &describe_type<RibbonColorPicker>, &create_type<RibbonColorPicker>, &is_exact_type<RibbonColorPicker>},
    {"backstage_view", "BackstageView", &describe_type<BackstageView>, &create_type<BackstageView>, &is_exact_type<BackstageView>},
    {"ribbon_gallery", "RibbonGallery", &describe_type<RibbonGallery>, &create_type<RibbonGallery>, &is_exact_type<RibbonGallery>},
    {"status_strip", "StatusStrip", &describe_type<StatusStrip>, &create_type<StatusStrip>, &is_exact_type<StatusStrip>},
    {"quick_access_toolbar", "QuickAccessToolbar", &describe_type<QuickAccessToolbar>, &create_type<QuickAccessToolbar>, &is_exact_type<QuickAccessToolbar>},
    {"tool_strip_panel", "ToolStripPanel", &describe_type<ToolStripPanel>, &create_type<ToolStripPanel>, &is_exact_type<ToolStripPanel>},
    {"tool_strip_container", "ToolStripContainer", &describe_type<ToolStripContainer>, &create_type<ToolStripContainer>, &is_exact_type<ToolStripContainer>},
    {"binding_navigator", "BindingNavigator", &describe_type<BindingNavigator>, &create_type<BindingNavigator>, &is_exact_type<BindingNavigator>},
    {"ribbon", "Ribbon", &describe_type<Ribbon>, &create_type<Ribbon>, &is_exact_type<Ribbon>},
    {"context_menu", "ContextMenu", &describe_type<ContextMenu>, &create_type<ContextMenu>, &is_exact_type<ContextMenu>},
    {"dock_workspace", "DockWorkspace", &describe_type<DockWorkspace>, &create_type<DockWorkspace>, &is_exact_type<DockWorkspace>},
    {"command", "Command", &describe_type<Command>, &create_type<Command>, &is_exact_type<Command>},
    {"font_command", "FontCommand", &describe_type<FontCommand>, &create_type<FontCommand>, &is_exact_type<FontCommand>},
    {"color_command", "ColorCommand", &describe_type<ColorCommand>, &create_type<ColorCommand>, &is_exact_type<ColorCommand>},
    {"gallery_command", "GalleryCommand", &describe_type<GalleryCommand>, &create_type<GalleryCommand>, &is_exact_type<GalleryCommand>},
    {"anchored_overlay_layer", "AnchoredOverlayLayer", &describe_type<AnchoredOverlayLayer>, &create_type<AnchoredOverlayLayer>, &is_exact_type<AnchoredOverlayLayer>},
    {"font_dialog", "FontDialog", &describe_type<FontDialog>, &create_type<FontDialog>, &is_exact_type<FontDialog>},
    {"task_dialog", "TaskDialog", &describe_type<TaskDialog>, &create_type<TaskDialog>, &is_exact_type<TaskDialog>},
    {"thread_exception_dialog", "ThreadExceptionDialog", &describe_type<ThreadExceptionDialog>, &create_type<ThreadExceptionDialog>, &is_exact_type<ThreadExceptionDialog>},
    {"folder_browser_dialog", "FolderBrowserDialog", &describe_type<FolderBrowserDialog>, &create_type<FolderBrowserDialog>, &is_exact_type<FolderBrowserDialog>},
    {"color_dialog", "ColorDialog", &describe_type<ColorDialog>, &create_type<ColorDialog>, &is_exact_type<ColorDialog>},
    {"tool_tip", "ToolTip", &describe_type<ToolTip>, &create_type<ToolTip>, &is_exact_type<ToolTip>},
    {"ribbon_tool_tip", "RibbonToolTip", &describe_type<RibbonToolTip>, &create_type<RibbonToolTip>, &is_exact_type<RibbonToolTip>},
    {"drawing_surface", "DrawingSurface", &describe_type<DrawingSurface>, &create_type<DrawingSurface>, &is_exact_type<DrawingSurface>},
    {"raster_canvas", "RasterCanvas", &describe_type<RasterCanvas>, &create_type<RasterCanvas>, &is_exact_type<RasterCanvas>},
    {"live_surface", "LiveSurface", &describe_type<LiveSurface>, &create_type<LiveSurface>, &is_exact_type<LiveSurface>},
    {"sub_surface_host", "SubSurfaceHost", &describe_type<SubSurfaceHost>, &create_type<SubSurfaceHost>, &is_exact_type<SubSurfaceHost>},
    {"easing_preview", "EasingPreview", &describe_type<EasingPreview>, &create_type<EasingPreview>, &is_exact_type<EasingPreview>},
    {"metrics_view", "MetricsView", &describe_type<MetricsView>, &create_type<MetricsView>, &is_exact_type<MetricsView>},
    {"typography_specimen", "TypographySpecimen", &describe_type<TypographySpecimen>, &create_type<TypographySpecimen>, &is_exact_type<TypographySpecimen>},
    {"visual_inspector_view", "VisualInspectorView", &describe_type<VisualInspectorView>, &create_type<VisualInspectorView>, &is_exact_type<VisualInspectorView>},
    {"visual_inspector_overlay", "VisualInspectorOverlay", &describe_type<VisualInspectorOverlay>, &create_type<VisualInspectorOverlay>, &is_exact_type<VisualInspectorOverlay>},
    {"timer", "Timer", &describe_type<Timer>, &create_type<Timer>, &is_exact_type<Timer>},
    {"background_worker", "BackgroundWorker", &describe_type<BackgroundWorker>, &create_type<BackgroundWorker>, &is_exact_type<BackgroundWorker>},
    {"notify_icon", "NotifyIcon", &describe_type<NotifyIcon>, &create_type<NotifyIcon>, &is_exact_type<NotifyIcon>},
    {"open_file_dialog", "OpenFileDialog", &describe_type<OpenFileDialog>, &create_type<OpenFileDialog>, &is_exact_type<OpenFileDialog>},
    {"save_file_dialog", "SaveFileDialog", &describe_type<SaveFileDialog>, &create_type<SaveFileDialog>, &is_exact_type<SaveFileDialog>},
    {"image_list", "ImageList", &describe_type<ImageList>, &create_type<ImageList>, &is_exact_type<ImageList>},
    {"error_provider", "ErrorProvider", &describe_type<ErrorProvider>, &create_type<ErrorProvider>, &is_exact_type<ErrorProvider>},
    {"help_provider", "HelpProvider", &describe_type<HelpProvider>, &create_type<HelpProvider>, &is_exact_type<HelpProvider>},
    {"semantic_feedback", "SemanticFeedback", &describe_type<SemanticFeedback>, &create_type<SemanticFeedback>, &is_exact_type<SemanticFeedback>},
    {"binding", "Binding", &describe_type<Binding>, &create_type<Binding>, &is_exact_type<Binding>},
    {"binding_source", "BindingSource", &describe_type<BindingSource>, &create_type<BindingSource>, &is_exact_type<BindingSource>},
    {"binding_context", "BindingContext", &describe_type<BindingContext>, &create_type<BindingContext>, &is_exact_type<BindingContext>},
}};

} // namespace gui_forms::abi1
