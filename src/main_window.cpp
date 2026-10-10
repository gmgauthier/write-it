/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"

#include "about_dialog.hpp"
#include "config.hpp"
#include "font_sizes.hpp"

#include <glibmm/miscutils.h>

#include <algorithm>
#include <functional>

namespace writeit {
namespace {

constexpr const int (&kZooms)[6] = kZoomChoices;

Glib::ustring zoom_label(int zoom)
{
  if (zoom == 0)
    return "Fit width";
  return std::to_string(zoom) + "%";
}

void framed(Gtk::Box& row, Gtk::Widget& child, bool expand)
{
  auto* frame = Gtk::manage(new Gtk::Frame());
  frame->set_shadow_type(Gtk::SHADOW_IN);
  child.set_margin_start(8);
  child.set_margin_end(8);
  child.set_margin_top(1);
  child.set_margin_bottom(1);
  frame->add(child);
  row.pack_start(*frame, expand, true, 0);
}

// A missing icon falls back to the menu's word. Toolbar buttons share one
// width, the widest's, so a word must not widen every icon on the bar.
void fallback_label(Gtk::ToolButton& button, const char* word)
{
  button.set_label(word);
  button.set_homogeneous(false);
}

// What a toolbar control is called in its toolbar's overflow menu.
constexpr const char* kOverflowId = "write-it-overflow";

// Gives a toolbar item its entry in the overflow menu, the arrow at the end
// of a toolbar too narrow to show it. GTK asks for the entry each time it
// builds that menu; `refresh` brings it up to date with the control first.
void attach_proxy(Gtk::ToolItem& tool, Gtk::MenuItem& item, const std::function<void()>& refresh)
{
  item.show();
  tool.set_proxy_menu_item(kOverflowId, item);
  refresh();
  // Before GTK's own handler, which would make an unlabelled one.
  tool.signal_create_menu_proxy().connect(
      [&tool, &item, refresh] {
        refresh();
        tool.set_proxy_menu_item(kOverflowId, item);
        return true;
      },
      false);
}

// A command button: an item with the menus' word that clicks the button.
void proxy_button(Gtk::ToolButton& button, const char* label)
{
  auto* item = Gtk::manage(new Gtk::MenuItem(label, true));
  item->signal_activate().connect([&button] { g_signal_emit_by_name(button.gobj(), "clicked"); });
  button.property_sensitive().signal_changed().connect(
      [&button, item] { item->set_sensitive(button.get_sensitive()); });
  attach_proxy(button, *item, [&button, item] { item->set_sensitive(button.get_sensitive()); });
}

// A toggle button (Bold, Italic, Underline, Bullets, Numbering): a check
// item that shows the button's state and sets it.
void proxy_toggle(Gtk::ToggleToolButton& button, const char* label)
{
  auto* item = Gtk::manage(new Gtk::CheckMenuItem(label, true));
  auto sync = [&button, item] {
    item->set_sensitive(button.get_sensitive());
    if (item->get_active() != button.get_active())
      item->set_active(button.get_active());
  };
  sync();
  item->signal_toggled().connect([&button, item] {
    if (button.get_active() != item->get_active())
      button.set_active(item->get_active());
  });
  button.signal_toggled().connect(sync);
  attach_proxy(button, *item, sync);
}

// One of the alignment buttons, which act as one group: a radio item. The
// button that goes down ticks its item, and the group drops the others.
void proxy_align(Gtk::ToggleToolButton& button, const char* label, Gtk::RadioMenuItem::Group& group)
{
  auto* item = Gtk::manage(new Gtk::RadioMenuItem(group, label, true));
  auto sync = [&button, item] {
    item->set_sensitive(button.get_sensitive());
    if (button.get_active() && !item->get_active())
      item->set_active(true);
  };
  item->set_active(button.get_active());
  item->signal_toggled().connect([&button, item] {
    if (item->get_active() && !button.get_active())
      button.set_active(true);
  });
  button.signal_toggled().connect(sync);
  attach_proxy(button, *item, sync);
}

// A box (font, size, style): its name, opening a submenu of its entries with
// the current one ticked, as Word 97's boxes drop down their lists.
void proxy_combo(Gtk::ToolItem& tool, Gtk::ComboBoxText& combo, const char* label)
{
  auto* item = Gtk::manage(new Gtk::MenuItem(label, true));
  auto* menu = Gtk::manage(new Gtk::Menu());
  item->set_submenu(*menu);
  // The font list grows when a document names a font it lacks, so the
  // submenu is made afresh from the box each time.
  auto fill = [&combo, item, menu] {
    item->set_sensitive(combo.get_sensitive());
    for (Gtk::Widget* child : menu->get_children())
      delete child;
    const Glib::ustring active = combo.get_active_text();
    for (const Gtk::TreeRow& row : combo.get_model()->children()) {
      Glib::ustring text;
      row.get_value(0, text);
      auto* entry = Gtk::manage(new Gtk::CheckMenuItem(text));
      entry->set_draw_as_radio(true);
      entry->set_active(text == active);
      entry->signal_toggled().connect([&combo, entry, text] {
        if (entry->get_active() && combo.get_active_text() != text)
          combo.set_active_text(text);
      });
      menu->append(*entry);
    }
    menu->show_all();
  };
  attach_proxy(tool, *item, fill);
}

// GTK shows a toolbar's overflow arrow partway through laying the toolbar
// out and lays the arrow itself out only on a later pass, which narrowing
// the window does not always bring: the arrow stayed 1 px at -1, with nothing
// to click. Ask for that pass whenever the arrow comes or goes.
void lay_out_arrow(Gtk::Toolbar& bar)
{
  GtkWidget* arrow = nullptr;
  gtk_container_forall(
      GTK_CONTAINER(bar.gobj()),
      [](GtkWidget* child, gpointer data) {
        if (GTK_IS_TOGGLE_BUTTON(child))
          *static_cast<GtkWidget**>(data) = child;
      },
      &arrow);
  if (!arrow)
    return;
  Glib::wrap(arrow)->property_visible().signal_changed().connect([&bar] {
    // Not from inside the layout; the toolbar's own lifetime bounds the idle.
    Glib::signal_idle().connect(
        sigc::bind_return(sigc::mem_fun(bar, &Gtk::Widget::queue_resize), false));
  });
}

}  // namespace

MainWindow::~MainWindow()
{
  page_idle_.disconnect();
  caret_idle_.disconnect();
  page_status_idle_.disconnect();
  list_shifts_idle_.disconnect();
  list_tabs_idle_.disconnect();
  clipboard_owner_.disconnect();
  mark_set_.disconnect();
  set_focus_.disconnect();
  size_popup_idle_.disconnect();
  // The text view gives PRIMARY back as it unrealizes, so it must hold it.
  if (primary_lent_ && buffer_) {
    buffer_->add_selection_clipboard(text_.get_clipboard("PRIMARY"));
    primary_lent_ = false;
  }
}

MainWindow::MainWindow()
{
  settings_.load();
  set_title("Write-It - Untitled");
  // The saved size, or 960 x 700 the first time, on the screen it opens on.
  int width = settings_.window_width;
  int height = settings_.window_height;
  if (auto display = Gdk::Display::get_default()) {
    auto monitor = display->get_primary_monitor();
    if (!monitor && display->get_n_monitors() > 0)
      monitor = display->get_monitor(0);
    if (monitor) {
      Gdk::Rectangle area;
      monitor->get_workarea(area);
      clamp_window(width, height, area.get_width(), area.get_height());
    }
  }
  set_default_size(width, height);
  window_memory_ = WindowMemory(settings_);
  window_memory_.update(width, height, settings_.window_maximized);
  if (settings_.window_maximized)
    maximize();
  signal_size_allocate().connect([this](Gtk::Allocation&) {
    int w = 0;
    int h = 0;
    get_size(w, h);
    window_memory_.update(w, h, is_maximized());
  });
  signal_window_state_event().connect(
      [this](GdkEventWindowState* event) {
        // Only the state: the size that comes with it may still be the old one.
        if (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED)
          window_memory_.update(0, 0, (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0);
        return false;
      },
      false);
  accel_ = Gtk::AccelGroup::create();
  add_accel_group(accel_);

  build_menus();
  build_toolbars();
  build_page();
  build_status();
  load_css();

  root_.pack_start(menu_bar_, Gtk::PACK_SHRINK);
  root_.pack_start(toolbars_, Gtk::PACK_SHRINK);
  root_.pack_start(ruler_, Gtk::PACK_SHRINK);
  root_.pack_start(paste_, Gtk::PACK_EXPAND_WIDGET);
  root_.pack_start(status_, Gtk::PACK_SHRINK);
  add(root_);

  build_editor();
  show_all_children();
  apply_chrome();
  apply_page_size();
  text_.grab_focus();
}

bool MainWindow::on_delete_event(GdkEventAny* event)
{
  if (!confirm_discard_or_save())
    return true;
  window_memory_.store(settings_);
  reload_recent();
  settings_.save();
  return Gtk::ApplicationWindow::on_delete_event(event);
}

Gtk::MenuItem* MainWindow::add_item(Gtk::Menu& menu, const char* label, bool sensitive, guint key,
                                    Gdk::ModifierType mods)
{
  auto* item = Gtk::manage(new Gtk::MenuItem(label, true));
  item->set_sensitive(sensitive);
  if (key != 0)
    item->add_accelerator("activate", accel_, key, mods, Gtk::ACCEL_VISIBLE);
  menu.append(*item);
  return item;
}

void MainWindow::build_menus()
{
  auto add_menu = [this](const char* label) {
    auto* item = Gtk::manage(new Gtk::MenuItem(label, true));
    auto* menu = Gtk::manage(new Gtk::Menu());
    item->set_submenu(*menu);
    menu_bar_.append(*item);
    return menu;
  };

  auto* file = add_menu("_File");
  new_item_ = add_item(*file, "_New", true, GDK_KEY_n, Gdk::CONTROL_MASK);
  add_item(*file, "New from _Template…", false);
  open_item_ = add_item(*file, "_Open…", true, GDK_KEY_o, Gdk::CONTROL_MASK);
  recent_item_ = add_item(*file, "Open _Recent", false);
  recent_item_->set_submenu(recent_menu_);
  save_item_ = add_item(*file, "_Save", true, GDK_KEY_s, Gdk::CONTROL_MASK);
  save_as_item_ = add_item(*file, "Save _As…", true);
  export_item_ = add_item(*file, "_Export…", true);
  add_item(*file, "_Print…", false, GDK_KEY_p, Gdk::CONTROL_MASK);
  add_item(*file, "Page Set_up…", false);
  file->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  close_item_ = add_item(*file, "_Close", true, GDK_KEY_w, Gdk::CONTROL_MASK);
  add_item(*file, "E_xit", true, GDK_KEY_q, Gdk::CONTROL_MASK)->signal_activate().connect([this] {
    close();
  });

  auto* edit = add_menu("_Edit");
  undo_item_ = add_item(*edit, "_Undo", false, GDK_KEY_z, Gdk::CONTROL_MASK);
  redo_item_ = add_item(*edit, "_Redo", false, GDK_KEY_y, Gdk::CONTROL_MASK);
  redo_item_->add_accelerator("activate", accel_, GDK_KEY_z, Gdk::CONTROL_MASK | Gdk::SHIFT_MASK,
                              Gtk::ACCEL_VISIBLE);
  edit->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  cut_item_ = add_item(*edit, "Cu_t", false, GDK_KEY_x, Gdk::CONTROL_MASK);
  copy_item_ = add_item(*edit, "_Copy", false, GDK_KEY_c, Gdk::CONTROL_MASK);
  paste_item_ = add_item(*edit, "_Paste", false, GDK_KEY_v, Gdk::CONTROL_MASK);
  delete_item_ =
      add_item(*edit, "_Delete", false, GDK_KEY_Delete, static_cast<Gdk::ModifierType>(0));
  edit->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  select_all_item_ = add_item(*edit, "Select _All", false, GDK_KEY_a, Gdk::CONTROL_MASK);
  edit->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  find_item_ = add_item(*edit, "_Find…", true, GDK_KEY_f, Gdk::CONTROL_MASK);
  replace_item_ = add_item(*edit, "R_eplace…", true, GDK_KEY_h, Gdk::CONTROL_MASK);

  auto* view = add_menu("_View");
  auto* standard = Gtk::manage(new Gtk::CheckMenuItem("Standard _Toolbar", true));
  auto* format = Gtk::manage(new Gtk::CheckMenuItem("_Format Toolbar", true));
  auto* side = Gtk::manage(new Gtk::CheckMenuItem("Side _by side", true));
  auto* status = Gtk::manage(new Gtk::CheckMenuItem("_Status Bar", true));
  standard->set_active(settings_.show_standard_toolbar);
  format->set_active(settings_.show_format_toolbar);
  side->set_active(settings_.toolbars_side_by_side);
  status->set_active(settings_.show_statusbar);
  standard->signal_toggled().connect([this, standard] {
    settings_.show_standard_toolbar = standard->get_active();
    standard_bar_.set_visible(settings_.show_standard_toolbar);
    reload_recent();
    settings_.save();
  });
  format->signal_toggled().connect([this, format] {
    settings_.show_format_toolbar = format->get_active();
    format_bar_.set_visible(settings_.show_format_toolbar);
    reload_recent();
    settings_.save();
  });
  side->signal_toggled().connect([this, side] {
    settings_.toolbars_side_by_side = side->get_active();
    apply_toolbar_row();
    reload_recent();
    settings_.save();
  });
  status->signal_toggled().connect([this, status] {
    settings_.show_statusbar = status->get_active();
    status_.set_visible(settings_.show_statusbar);
    reload_recent();
    settings_.save();
  });
  view->append(*standard);
  view->append(*format);
  view->append(*side);
  view->append(*status);
  view->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));

  auto* zoom_item = Gtk::manage(new Gtk::MenuItem("_Zoom", true));
  zoom_item->set_submenu(zoom_menu_);
  view->append(*zoom_item);
  Gtk::RadioMenuItem::Group view_group;
  Gtk::RadioMenuItem::Group status_group;
  for (int i = 0; i < 6; ++i) {
    const int zoom = kZooms[i];
    const Glib::ustring label = zoom_label(zoom);
    zoom_view_[i] = Gtk::manage(new Gtk::RadioMenuItem(view_group, label));
    zoom_status_[i] = Gtk::manage(new Gtk::RadioMenuItem(status_group, label));
    zoom_menu_.append(*zoom_view_[i]);
    zoom_popup_.append(*zoom_status_[i]);
    zoom_view_[i]->signal_activate().connect([this, zoom] { set_zoom(zoom); });
    zoom_status_[i]->signal_activate().connect([this, zoom] { set_zoom(zoom); });
  }
  view->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  Gtk::RadioMenuItem::Group page_group;
  page_item_ = Gtk::manage(new Gtk::RadioMenuItem(page_group, "_Page", true));
  draft_item_ = Gtk::manage(new Gtk::RadioMenuItem(page_group, "_Draft", true));
  page_item_->set_active(view_ == ViewMode::Page);
  draft_item_->set_active(view_ == ViewMode::Draft);
  page_item_->signal_toggled().connect([this] {
    if (page_item_->get_active())
      set_view(ViewMode::Page);
  });
  draft_item_->signal_toggled().connect([this] {
    if (draft_item_->get_active())
      set_view(ViewMode::Draft);
  });
  view->append(*page_item_);
  view->append(*draft_item_);

  auto* insert = add_menu("_Insert");
  add_item(*insert, "_Picture…", false);
  add_item(*insert, "_Table…", false);
  add_item(*insert, "Page _Break", false);
  add_item(*insert, "_Footnote", false);

  auto* format_menu = add_menu("F_ormat");
  add_item(*format_menu, "_Font…", false);
  bold_item_ = add_item(*format_menu, "_Bold", true, GDK_KEY_b, Gdk::CONTROL_MASK);
  italic_item_ = add_item(*format_menu, "_Italic", true, GDK_KEY_i, Gdk::CONTROL_MASK);
  underline_item_ = add_item(*format_menu, "_Underline", true, GDK_KEY_u, Gdk::CONTROL_MASK);
  format_menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  align_left_item_ = add_item(*format_menu, "Align _Left", true);
  align_center_item_ = add_item(*format_menu, "_Center", true);
  align_right_item_ = add_item(*format_menu, "Align _Right", true);
  justify_item_ = add_item(*format_menu, "_Justify", true);
  format_menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  style_item_ = add_item(*format_menu, "_Style…", true);
  bullets_item_ = add_item(*format_menu, "Bull_ets", true);
  numbering_item_ = add_item(*format_menu, "_Numbering", true);
  format_menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  paragraph_item_ = add_item(*format_menu, "_Paragraph…", true);
  add_item(*format_menu, "C_olumns…", false);

  auto* tools = add_menu("_Tools");
  add_item(*tools, "_Spelling…", false, GDK_KEY_F7, static_cast<Gdk::ModifierType>(0));
  options_item_ = add_item(*tools, "_Options…", true);

  auto* table = add_menu("T_able");
  add_item(*table, "_Insert Table…", false);
  add_item(*table, "Insert _Row", false);
  add_item(*table, "Insert _Column", false);
  table->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item(*table, "_Delete Row", false);
  add_item(*table, "Delete C_olumn", false);

  auto* help = add_menu("_Help");
  add_item(*help, "_About Write-It", true, GDK_KEY_F1, static_cast<Gdk::ModifierType>(0))
      ->signal_activate()
      .connect(sigc::mem_fun(*this, &MainWindow::on_about));

  context_cut_ = Gtk::manage(new Gtk::MenuItem("Cu_t", true));
  context_copy_ = Gtk::manage(new Gtk::MenuItem("_Copy", true));
  context_paste_ = Gtk::manage(new Gtk::MenuItem("_Paste", true));
  context_cut_->set_sensitive(false);
  context_copy_->set_sensitive(false);
  context_paste_->set_sensitive(false);
  context_.append(*context_cut_);
  context_.append(*context_copy_);
  context_.append(*context_paste_);
  // This app's own items, after a separator: Word's numbering commands,
  // shown on a numbered item.
  context_numbering_rule_ = Gtk::manage(new Gtk::SeparatorMenuItem());
  context_restart_ = Gtk::manage(new Gtk::MenuItem("_Restart Numbering", true));
  context_continue_ = Gtk::manage(new Gtk::MenuItem("C_ontinue Previous List", true));
  context_restart_->signal_activate().connect([this] { renumber_list(true); });
  context_continue_->signal_activate().connect([this] { renumber_list(false); });
  context_.append(*context_numbering_rule_);
  context_.append(*context_restart_);
  context_.append(*context_continue_);
  context_.show_all();
  context_numbering_rule_->hide();
  context_restart_->hide();
  context_continue_->hide();
}

Gtk::ToolButton* MainWindow::add_tool(Gtk::Toolbar& bar, const char* icon, const char* tip,
                                      bool sensitive)
{
  auto* button = Gtk::manage(new Gtk::ToolButton());
  if (Gtk::IconTheme::get_default()->has_icon(icon))
    button->set_icon_name(icon);
  else
    fallback_label(*button, tip);
  button->set_tooltip_text(tip);
  button->set_sensitive(sensitive);
  bar.append(*button);
  return button;
}

void MainWindow::build_toolbars()
{
  // Both toolbars keep an overflow arrow, so a narrow window moves the last
  // controls into the arrow's menu instead of growing past the screen.
  standard_bar_.set_toolbar_style(Gtk::TOOLBAR_ICONS);
  standard_bar_.set_icon_size(Gtk::ICON_SIZE_SMALL_TOOLBAR);
  standard_bar_.set_hexpand(false);
  standard_bar_.set_show_arrow(true);
  format_bar_.set_toolbar_style(Gtk::TOOLBAR_ICONS);
  format_bar_.set_icon_size(Gtk::ICON_SIZE_SMALL_TOOLBAR);
  format_bar_.set_hexpand(false);
  format_bar_.set_show_arrow(true);
  lay_out_arrow(standard_bar_);
  lay_out_arrow(format_bar_);
  toolbars_.pack_start(standard_bar_, Gtk::PACK_SHRINK);
  toolbars_.pack_start(format_bar_, Gtk::PACK_SHRINK);

  new_tool_ = add_tool(standard_bar_, "document-new", "New", true);
  open_tool_ = add_tool(standard_bar_, "document-open", "Open", true);
  save_tool_ = add_tool(standard_bar_, "document-save", "Save", true);
  standard_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  auto* print_tool = add_tool(standard_bar_, "document-print", "Print", false);
  standard_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  cut_tool_ = add_tool(standard_bar_, "edit-cut", "Cut", false);
  copy_tool_ = add_tool(standard_bar_, "edit-copy", "Copy", false);
  paste_tool_ = add_tool(standard_bar_, "edit-paste", "Paste", false);
  standard_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  undo_tool_ = add_tool(standard_bar_, "edit-undo", "Undo", false);
  redo_tool_ = add_tool(standard_bar_, "edit-redo", "Redo", false);

  font_combo_.set_size_request(128, -1);
  font_combo_.set_tooltip_text("Font");
  size_combo_.set_tooltip_text("Size");
  for (int size : preset_sizes())
    size_combo_.append(std::to_string(size));
  size_choices_shown_.assign(preset_sizes().begin(), preset_sizes().end());
  size_combo_.set_active_text("11");
  style_combo_.set_size_request(110, -1);
  style_combo_.set_tooltip_text("Style");

  auto hold = [](Gtk::Widget& child, int width) {
    auto* item = Gtk::manage(new Gtk::ToolItem());
    child.set_size_request(width, -1);
    item->add(child);
    return item;
  };
  auto* font_item = hold(font_combo_, 128);
  // As wide as "1638" needs (NarrowCombo), not a fixed 52 px.
  auto* size_item = hold(size_combo_, -1);
  format_bar_.append(*font_item);
  format_bar_.append(*size_item);

  auto toggle = [this](const char* icon, const char* tip, bool active, bool sensitive) {
    auto* button = Gtk::manage(new Gtk::ToggleToolButton());
    if (Gtk::IconTheme::get_default()->has_icon(icon))
      button->set_icon_name(icon);
    else
      fallback_label(*button, tip);
    button->set_tooltip_text(tip);
    button->set_active(active);
    button->set_sensitive(sensitive);
    format_bar_.append(*button);
    return button;
  };
  bold_toggle_ = toggle("format-text-bold", "Bold", false, true);
  italic_toggle_ = toggle("format-text-italic", "Italic", false, true);
  underline_toggle_ = toggle("format-text-underline", "Underline", false, true);
  format_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  align_left_toggle_ = toggle("format-justify-left", "Align Left", true, true);
  align_center_toggle_ = toggle("format-justify-center", "Center", false, true);
  align_right_toggle_ = toggle("format-justify-right", "Align Right", false, true);
  justify_toggle_ = toggle("format-justify-fill", "Justify", false, true);
  format_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  auto* style_item = hold(style_combo_, 110);
  format_bar_.append(*style_item);
  bullets_toggle_ = toggle("format-list-unordered", "Bullets", false, true);
  numbering_toggle_ = toggle("format-list-ordered", "Numbering", false, true);

  // A control that does not fit goes to its toolbar's overflow menu under
  // the word the menus use for it. Separators bring their own.
  proxy_button(*new_tool_, "_New");
  proxy_button(*open_tool_, "_Open…");
  proxy_button(*save_tool_, "_Save");
  proxy_button(*print_tool, "_Print…");
  proxy_button(*cut_tool_, "Cu_t");
  proxy_button(*copy_tool_, "_Copy");
  proxy_button(*paste_tool_, "_Paste");
  proxy_button(*undo_tool_, "_Undo");
  proxy_button(*redo_tool_, "_Redo");
  proxy_combo(*font_item, font_combo_, "_Font");
  proxy_combo(*size_item, size_combo_, "Font Si_ze");
  proxy_toggle(*bold_toggle_, "_Bold");
  proxy_toggle(*italic_toggle_, "_Italic");
  proxy_toggle(*underline_toggle_, "_Underline");
  Gtk::RadioMenuItem::Group align_group;
  proxy_align(*align_left_toggle_, "Align _Left", align_group);
  proxy_align(*align_center_toggle_, "_Center", align_group);
  proxy_align(*align_right_toggle_, "Align _Right", align_group);
  proxy_align(*justify_toggle_, "_Justify", align_group);
  proxy_combo(*style_item, style_combo_, "_Style");
  proxy_toggle(*bullets_toggle_, "Bull_ets");
  proxy_toggle(*numbering_toggle_, "_Numbering");
}

void MainWindow::build_page()
{
  ruler_.set_size_request(-1, 18);
  ruler_.get_style_context()->add_class("ruler");
  ruler_.signal_draw().connect(sigc::mem_fun(*this, &MainWindow::on_ruler_draw));

  page_.set_size_request(kScreenPageWidth, page_sheet_height(1.0));
  page_.get_style_context()->add_class("page");
  page_.add_events(Gdk::BUTTON_PRESS_MASK);
  page_.signal_button_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_context), false);

  buffer_ = Gtk::TextBuffer::create();
  text_.set_buffer(buffer_);
  text_.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
  text_.set_hexpand(true);
  text_.set_vexpand(true);
  text_.get_style_context()->add_class("page-text");
  page_.add(text_);
  text_.signal_button_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_context), false);
  page_.signal_size_allocate().connect([this](Gtk::Allocation&) { ruler_.queue_draw(); });

  board_.get_style_context()->add_class("pasteboard");
  board_.pack_start(page_, Gtk::PACK_SHRINK);
  page_.set_halign(Gtk::ALIGN_CENTER);
  page_.set_valign(Gtk::ALIGN_START);
  page_.set_margin_top(18);
  page_.set_margin_bottom(18);
  board_.signal_button_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_context), false);
  board_.add_events(Gdk::BUTTON_PRESS_MASK);

  paste_.get_style_context()->add_class("pasteboard");
  paste_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  paste_.add(board_);
  paste_.add_events(Gdk::BUTTON_PRESS_MASK);
  paste_.signal_button_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_context), false);
  paste_.signal_size_allocate().connect([this](Gtk::Allocation&) {
    // Fit width and Draft follow the pasteboard. A size request set while
    // GTK is allocating is lost, so a narrower window left the page at the
    // old width; the page is sized again just after this layout instead.
    if (settings_.zoom == 0 || view_ == ViewMode::Draft)
      queue_page_size();
    ruler_.queue_draw();
  });

  // Keep the caret in view. Any move of the insert mark, and any edit (the
  // caret rides along with typing without a mark-set), asks to follow it;
  // each later layout (the page growing, a zoom, a view switch, a resize)
  // scrolls to it again. The wheel and the scrollbars let it go.
  buffer_->signal_mark_set().connect(
      [this](const Gtk::TextBuffer::iterator&, const Glib::RefPtr<Gtk::TextBuffer::Mark>& mark) {
        if (mark == buffer_->get_insert())
          follow_caret();
      });
  buffer_->signal_changed().connect([this] { follow_caret(); });
  text_.signal_size_allocate().connect([this](Gtk::Allocation& allocation) {
    // The text view lays its lines out again while it is being allocated (a
    // zoom rewraps every line), and the resize it asks for then is lost:
    // from Fit width to 200% the page stayed shorter than its text and the
    // caret on the last lines was out of reach. Size the page again after
    // this layout whenever the text wants more height than it was given.
    // (Less is normal: an empty page, or Draft's white area, is taller.)
    int min_h = 0;
    int nat_h = 0;
    text_.get_preferred_height_for_width(allocation.get_width(), min_h, nat_h);
    if (nat_h > allocation.get_height()) {
      grow_page_ = true;
      queue_page_size();
    } else if (page_height_stale()) {
      // The text came out shorter than the height the page was last sized
      // to. apply_page_size() measures the text before a zoom has rewrapped
      // it, so the request it sets from Fit width to 50% is the old, taller
      // layout's; left there, the sheet only ever grew and its height
      // depended on the zooms before. Size it again to the text as it is.
      queue_page_size();
    }
    scroll_to_caret();
  });
  g_signal_connect(text_.gobj(), "move-cursor", G_CALLBACK(&MainWindow::on_move_cursor), this);
  paste_.get_vadjustment()->signal_changed().connect([this] { scroll_to_caret(); });
  paste_.get_hadjustment()->signal_changed().connect([this] { scroll_to_caret(); });
  paste_.signal_scroll_event().connect(
      [this](GdkEventScroll*) {
        follow_caret_ = false;
        return false;
      },
      false);
  for (Gtk::Scrollbar* bar : {paste_.get_vscrollbar(), paste_.get_hscrollbar()}) {
    if (!bar)
      continue;
    bar->add_events(Gdk::BUTTON_PRESS_MASK);
    bar->signal_button_press_event().connect(
        [this](GdkEventButton*) {
          follow_caret_ = false;
          return false;
        },
        false);
  }
}

void MainWindow::on_move_cursor(GtkTextView* view, GtkMovementStep step, gint count,
                                gboolean extend, gpointer self)
{
  if (step != GTK_MOVEMENT_PAGES)
    return;
  if (static_cast<MainWindow*>(self)->page_caret(count, extend != FALSE))
    g_signal_stop_emission_by_name(view, "move-cursor");
}

bool MainWindow::page_caret(int count, bool extend)
{
  auto adj = paste_.get_vadjustment();
  const double page = adj->get_page_size();
  if (!buffer_ || page <= 0 || count == 0)
    return false;
  Gdk::Rectangle caret;
  Gdk::Rectangle first;
  Gdk::Rectangle last;
  text_.get_iter_location(buffer_->get_insert()->get_iter(), caret);
  text_.get_iter_location(buffer_->begin(), first);
  text_.get_iter_location(buffer_->end(), last);
  // A screen on, aimed at the middle of the line there, and no further
  // than the first or the last line.
  const double step = page * count;
  const int y = static_cast<int>(
      std::max<double>(first.get_y(), std::min<double>(last.get_y(), caret.get_y() + step)));
  Gtk::TextIter target;
  int trailing = 0;
  text_.get_iter_at_position(target, trailing, caret.get_x(), y + caret.get_height() / 2);
  // The character boundary nearest the caret's x.
  if (trailing > 0 && !target.ends_line())
    target.forward_chars(trailing);
  // The pasteboard goes the same screen, so the caret keeps its place on it.
  adj->set_value(
      std::max(adj->get_lower(), std::min(adj->get_value() + step, adj->get_upper() - page)));
  if (extend)
    buffer_->move_mark(buffer_->get_insert(), target);
  else
    buffer_->place_cursor(target);
  return true;
}

void MainWindow::follow_caret()
{
  follow_caret_ = true;
  // Opening a file or an undo rebuilds the buffer in many edits; the idle
  // below scrolls once, after the last of them.
  if (!loading_ && !restoring_)
    scroll_to_caret();
  // The layout may not have caught up with the move yet; look again once
  // the main loop is idle.
  if (caret_idle_.connected())
    return;
  caret_idle_ = Glib::signal_idle().connect([this] {
    scroll_to_caret();
    return false;
  });
}

void MainWindow::scroll_to_caret()
{
  if (!follow_caret_ || !buffer_ || !text_.get_realized())
    return;
  Gdk::Rectangle rect;
  text_.get_iter_location(buffer_->get_insert()->get_iter(), rect);
  int wx = 0;
  int wy = 0;
  text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, rect.get_x(), rect.get_y(), wx, wy);
  int x = 0;
  int y = 0;
  if (!text_.translate_coordinates(board_, wx, wy, x, y))
    return;
  // Bring [from, from + size) inside the adjustment's page, with a little
  // room either side when the page has it.
  auto reveal = [](const Glib::RefPtr<Gtk::Adjustment>& adj, double from, double size) {
    const double page = adj->get_page_size();
    if (page <= 0)
      return;
    const double pad = page >= size + 2 * 18 ? 18 : 0;
    const double value = adj->get_value();
    double want = value;
    if (from - pad < value)
      want = from - pad;
    else if (from + size + pad > value + page)
      want = from + size + pad - page;
    want = std::max(adj->get_lower(), std::min(want, adj->get_upper() - page));
    if (want != value)
      adj->set_value(want);
  };
  // On the first line, or the last, all the way: the page's edge and the
  // gray beyond it show, not a margin short of them.
  Gdk::Rectangle first;
  Gdk::Rectangle last;
  text_.get_iter_location(buffer_->begin(), first);
  text_.get_iter_location(buffer_->end(), last);
  auto v = paste_.get_vadjustment();
  if (rect.get_y() <= first.get_y())
    v->set_value(v->get_lower());
  else if (rect.get_y() >= last.get_y())
    v->set_value(std::max(v->get_lower(), v->get_upper() - v->get_page_size()));
  else
    reveal(v, y, rect.get_height());
  // Sideways too, for a page wider than the window (200% on a 960 px window).
  reveal(paste_.get_hadjustment(), x, std::max(1, rect.get_width()));
}

void MainWindow::build_status()
{
  status_.get_style_context()->add_class("statusbar");
  message_.set_halign(Gtk::ALIGN_START);
  message_.set_hexpand(true);
  message_.set_ellipsize(Pango::ELLIPSIZE_END);
  page_label_.set_text(page_label(PageCount{}));
  zoom_cell_.add(zoom_label_);
  // No window of its own: popup_at_widget() places the menu from the
  // widget's allocation, which is in its parent's window, and an event box
  // with a visible window counted the offset twice, sending the menu to the
  // corner of the screen.
  zoom_cell_.set_visible_window(false);
  zoom_cell_.add_events(Gdk::BUTTON_PRESS_MASK);
  zoom_cell_.signal_button_press_event().connect(
      [this](GdkEventButton* event) {
        if (event->type != GDK_BUTTON_PRESS || event->button != 1)
          return false;
        zoom_popup_.popup_at_widget(&zoom_cell_, Gdk::GRAVITY_NORTH_EAST, Gdk::GRAVITY_SOUTH_EAST,
                                    reinterpret_cast<GdkEvent*>(event));
        return true;
      },
      false);
  // Popup menus are not the window's children, so show_all_children() never
  // reaches them; without this the zoom cell popped an empty menu.
  zoom_popup_.show_all();
  framed(status_, message_, true);
  framed(status_, page_label_, false);
  framed(status_, zoom_cell_, false);
}

void MainWindow::load_css()
{
  const std::string source = Glib::build_filename(SOURCE_ROOT, "data", "write-it.css");
  const std::string installed = Glib::build_filename(DATADIR, "write-it.css");
  const std::string path = Glib::file_test(source, Glib::FILE_TEST_IS_REGULAR) ? source : installed;
  if (!Glib::file_test(path, Glib::FILE_TEST_IS_REGULAR))
    return;
  auto css = Gtk::CssProvider::create();
  try {
    css->load_from_path(path);
  } catch (const Glib::Error&) {
  }
  auto screen = Gdk::Screen::get_default();
  if (!screen)
    return;
  Gtk::StyleContext::add_provider_for_screen(screen, css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void MainWindow::apply_toolbar_row()
{
  toolbars_.set_orientation(settings_.toolbars_side_by_side ? Gtk::ORIENTATION_HORIZONTAL
                                                            : Gtk::ORIENTATION_VERTICAL);
}

void MainWindow::apply_chrome()
{
  apply_toolbar_row();
  standard_bar_.set_visible(settings_.show_standard_toolbar);
  format_bar_.set_visible(settings_.show_format_toolbar);
  status_.set_visible(settings_.show_statusbar);
  suppress_zoom_ = true;
  sync_zoom_checks();
  suppress_zoom_ = false;
  zoom_label_.set_text(zoom_label(settings_.zoom));
}

void MainWindow::apply_page_size()
{
  if (sizing_)
    return;
  sizing_ = true;
  const double z = zoom_factor();
  // Recomputed from the zoom and the view every time, never kept from the
  // last layout: the text view asks for exactly this width, so a smaller
  // zoom is a narrower allocation and a narrower wrap.
  const int width = page_widths(view_, z).page;
  text_.set_layout_width(width);
  apply_margins();
  // Paragraph tags carry the text inset in their margins, so they are
  // restyled when the zoom or the view changes it.
  if (z != styled_zoom_ || view_ != styled_view_) {
    styled_zoom_ = z;
    styled_view_ = view_;
    restyle_tags();
  }
  const int height = page_height();
  int current_w = 0;
  int current_h = 0;
  page_.get_size_request(current_w, current_h);
  if (current_w != width || current_h != height)
    page_.set_size_request(width, height);
  sizing_ = false;
  queue_page_status();
}

// The page's height for the view, the zoom, the window and the text as they
// are now, and nothing else: the least height (A4 in Page, the pasteboard's
// in Draft), or the text's own when it is taller.
int MainWindow::page_height() const
{
  const ViewGeometry g = geometry();
  // Draft's white area is at least as tall as the visible pasteboard, so
  // the text starts at the top and there is no gray below it.
  const int base_h = g.chrome ? g.page_height : std::max(1, paste_.get_allocated_height() - 4);
  const int width = page_widths(view_, zoom_factor()).page;
  int min_h = 0;
  int nat_h = 0;
  text_.get_preferred_height_for_width(std::max(1, width - (g.chrome ? 2 : 0)), min_h, nat_h);
  return std::max(base_h, nat_h);
}

bool MainWindow::page_height_stale() const
{
  int current_w = 0;
  int current_h = 0;
  page_.get_size_request(current_w, current_h);
  return current_h != page_height();
}

void MainWindow::queue_page_size()
{
  if (page_idle_.connected())
    return;
  page_idle_ = Glib::signal_idle().connect(
      [this] {
        apply_page_size();
        // Only for text that outgrew the page: a resize on every pasteboard
        // allocation would allocate the pasteboard again, and again.
        if (grow_page_) {
          grow_page_ = false;
          page_.queue_resize();
        }
        return false;
      },
      Glib::PRIORITY_HIGH_IDLE);
}

void MainWindow::queue_page_status()
{
  if (page_status_idle_.connected())
    return;
  // After GtkTextView's own validation idle, so the line heights are real.
  page_status_idle_ = Glib::signal_idle().connect(
      [this] {
        update_page_status();
        return false;
      },
      Glib::PRIORITY_DEFAULT_IDLE);
}

// "Page n of m", approximate until M3: see page_count() in view.hpp.
void MainWindow::update_page_status()
{
  if (!buffer_)
    return;
  int end_y = 0;
  int end_h = 0;
  text_.get_line_yrange(buffer_->end(), end_y, end_h);
  Gdk::Rectangle caret;
  text_.get_iter_location(buffer_->get_iter_at_mark(buffer_->get_insert()), caret);
  const Glib::ustring label = page_label(page_count(end_y + end_h, caret.get_y(), zoom_factor()));
  if (page_label_.get_text() != label)
    page_label_.set_text(label);
}

void MainWindow::set_zoom(int zoom)
{
  if (suppress_zoom_ || zoom == settings_.zoom)
    return;
  settings_.zoom = zoom;
  suppress_zoom_ = true;
  sync_zoom_checks();
  suppress_zoom_ = false;
  zoom_label_.set_text(zoom_label(zoom));
  apply_page_size();
  reload_recent();
  settings_.save();
}

void MainWindow::sync_zoom_checks()
{
  for (int i = 0; i < 6; ++i) {
    const bool on = kZooms[i] == settings_.zoom;
    if (zoom_view_[i])
      zoom_view_[i]->set_active(on);
    if (zoom_status_[i])
      zoom_status_[i]->set_active(on);
  }
}

void MainWindow::on_about()
{
  AboutDialog dialog(*this);
  dialog.run();
}

bool MainWindow::on_ruler_draw(const Cairo::RefPtr<Cairo::Context>& cr)
{
  const Gtk::Allocation self = ruler_.get_allocation();
  cr->set_source_rgb(0.957, 0.953, 0.949);
  cr->rectangle(0, 0, self.get_width(), self.get_height());
  cr->fill();
  int origin_x = 0;
  int origin_y = 0;
  page_.translate_coordinates(ruler_, 0, 0, origin_x, origin_y);
  const int page_w = page_.get_allocated_width();
  cr->set_source_rgb(0.604, 0.596, 0.588);
  cr->set_line_width(1);
  for (int x = origin_x; x <= origin_x + page_w; x += 27) {
    cr->move_to(x + 0.5, 10);
    cr->line_to(x + 0.5, self.get_height());
  }
  cr->stroke();
  cr->set_source_rgb(0.784, 0.776, 0.769);
  cr->move_to(0, self.get_height() - 0.5);
  cr->line_to(self.get_width(), self.get_height() - 0.5);
  cr->stroke();

  // The current paragraph's indents, as Word 97 marks them: the first line
  // hangs from the top edge, the left and right indents stand on the bottom.
  if (!buffer_)
    return true;
  const Indents indents = indents_at(cursor_offset());
  const int left = origin_x + margin_left();
  const int right = origin_x + page_w - margin_right();
  const double h = self.get_height();
  auto down = [&](double x) {
    cr->move_to(x - 4, 1);
    cr->line_to(x + 4, 1);
    cr->line_to(x, 6);
    cr->close_path();
  };
  auto up = [&](double x) {
    cr->move_to(x - 4, h - 1);
    cr->line_to(x + 4, h - 1);
    cr->line_to(x, h - 6);
    cr->close_path();
  };
  down(left + indent_px(indents.left + indents.first) + 0.5);
  up(left + indent_px(indents.left) + 0.5);
  up(right - indent_px(indents.right) + 0.5);
  cr->set_source_rgb(0.95, 0.95, 0.95);
  cr->fill_preserve();
  cr->set_source_rgb(0.25, 0.25, 0.25);
  cr->stroke();
  return true;
}

bool MainWindow::on_context(GdkEventButton* event)
{
  if (!event || event->type != GDK_BUTTON_PRESS || event->button != 3)
    return false;
  auto text_win = text_.get_window(Gtk::TEXT_WINDOW_TEXT);
  if (buffer_ && text_win && event->window == text_win->gobj()) {
    int bx = 0;
    int by = 0;
    text_.window_to_buffer_coords(Gtk::TEXT_WINDOW_TEXT, static_cast<int>(event->x),
                                  static_cast<int>(event->y), bx, by);
    Gtk::TextBuffer::iterator where;
    int trailing = 0;
    text_.get_iter_at_position(where, trailing, bx, by);
    Gtk::TextBuffer::iterator start;
    Gtk::TextBuffer::iterator end;
    if (!buffer_->get_selection_bounds(start, end) || where.compare(start) < 0 ||
        where.compare(end) > 0)
      buffer_->place_cursor(where);
    update_actions();
  }
  sync_context_numbering();
  context_.popup_at_pointer(reinterpret_cast<GdkEvent*>(event));
  return true;
}

}  // namespace writeit
