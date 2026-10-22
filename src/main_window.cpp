/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"

#include "about_dialog.hpp"
#include "config.hpp"
#include "font_sizes.hpp"

#include <gdk/gdk.h>
#include <glibmm/miscutils.h>

#include <algorithm>
#include <cmath>
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

// Font colour or highlight in the overflow menu: a submenu of the palette,
// the current one ticked. Choosing one applies it, as the button's arrow does.
void proxy_ink(Gtk::MenuToolButton& button, const char* label, bool highlight,
               const std::function<Ink()>& current, const std::function<void(Ink)>& apply)
{
  auto* item = Gtk::manage(new Gtk::MenuItem(label, true));
  auto* menu = Gtk::manage(new Gtk::Menu());
  item->set_submenu(*menu);
  auto fill = [&button, item, menu, highlight, current, apply] {
    item->set_sensitive(button.get_sensitive());
    for (Gtk::Widget* child : menu->get_children())
      delete child;
    const Ink active = current();
    for (int i = 0; i <= 16; ++i) {
      const Ink ink = static_cast<Ink>(i);
      auto* entry = Gtk::manage(new Gtk::CheckMenuItem(ink_name(ink, highlight)));
      entry->set_draw_as_radio(true);
      entry->set_active(ink == active);
      entry->signal_toggled().connect([entry, ink, current, apply] {
        if (entry->get_active() && current() != ink)
          apply(ink);
      });
      menu->append(*entry);
    }
    menu->show_all();
  };
  attach_proxy(button, *item, fill);
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

// A Word 97 ruler mark, drawn on an 18 px ruler. Left, center, right, and
// decimal sit on a short baseline. A bar in the well is the same short line;
// a bar stop on the ruler itself is drawn full height by the caller.
void stroke_tab_icon(const Cairo::RefPtr<Cairo::Context>& cr, double x, double mid, TabAlign align)
{
  const double cx = std::floor(x) + 0.5;
  const double top = mid - 4.0;
  const double base = mid + 3.0;
  cr->set_source_rgb(0.2, 0.2, 0.2);
  cr->set_line_width(1.0);
  if (align == TabAlign::Bar) {
    cr->move_to(cx, mid - 6.0);
    cr->line_to(cx, mid + 6.0);
    cr->stroke();
    return;
  }
  if (align == TabAlign::Right) {
    cr->move_to(cx + 3.0, top);
    cr->line_to(cx + 3.0, base);
    cr->line_to(cx - 4.0, base);
  } else if (align == TabAlign::Center || align == TabAlign::Decimal) {
    cr->move_to(cx, top);
    cr->line_to(cx, base);
    cr->move_to(cx - 4.0, base);
    cr->line_to(cx + 4.0, base);
  } else {
    cr->move_to(cx - 3.0, top);
    cr->line_to(cx - 3.0, base);
    cr->line_to(cx + 4.0, base);
  }
  cr->stroke();
  if (align == TabAlign::Decimal) {
    cr->arc(cx, top - 1.5, 1.1, 0, 6.28318530718);
    cr->fill();
  }
}

void draw_tab_well(const Cairo::RefPtr<Cairo::Context>& cr, TabAlign align)
{
  cr->rectangle(1.5, 1.5, 15, 15);
  cr->set_source_rgb(0.90, 0.89, 0.87);
  cr->fill_preserve();
  cr->set_source_rgb(0.35, 0.35, 0.35);
  cr->set_line_width(1.0);
  cr->stroke();
  stroke_tab_icon(cr, 9, 9, align);
}

TabAlign next_ruler_kind(TabAlign kind)
{
  switch (kind) {
    case TabAlign::Left:
      return TabAlign::Center;
    case TabAlign::Center:
      return TabAlign::Right;
    case TabAlign::Right:
      return TabAlign::Decimal;
    case TabAlign::Decimal:
      return TabAlign::Bar;
    case TabAlign::Bar:
      return TabAlign::Left;
  }
  return TabAlign::Left;
}

}  // namespace

MainWindow::~MainWindow()
{
  // Focus leaving a note during teardown used to place the band again, and
  // that read the document while the widgets were going away.
  closing_ = true;
  // A clipboard answer still on its way finds the window gone.
  alive_.reset();
  page_idle_.disconnect();
  caret_idle_.disconnect();
  note_numbers_idle_.disconnect();
  note_scroll_conn_.disconnect();
  note_scroll_idle_.disconnect();
  restore_idle_.disconnect();
  cell_typing_idle_.disconnect();
  page_status_idle_.disconnect();
  table_sync_.disconnect();
  frame_fit_idle_.disconnect();
  list_shifts_idle_.disconnect();
  list_tabs_idle_.disconnect();
  story_tabs_idle_.disconnect();
  seam_pending_idle_.disconnect();
  painter_idle_.disconnect();
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
  // The frame is outside that client size. Hear when the window manager
  // names it, and fit once it has. A missing frame is not guessed.
  add_events(Gdk::PROPERTY_CHANGE_MASK);
  signal_map().connect([this] { schedule_frame_fit(); });
  signal_property_notify_event().connect([this](GdkEventProperty* event) {
    if (event && event->atom == gdk_atom_intern_static_string("_NET_FRAME_EXTENTS"))
      schedule_frame_fit();
    return false;
  });
  signal_size_allocate().connect([this](Gtk::Allocation&) {
    int w = 0;
    int h = 0;
    get_size(w, h);
    window_memory_.update(w, h, is_maximized());
  });
  signal_window_state_event().connect(
      [this](GdkEventWindowState* event) {
        // Only the state: the size that comes with it may still be the old one.
        if (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED) {
          const bool maximized = (event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
          window_memory_.update(0, 0, maximized);
          if (!maximized)
            schedule_frame_fit();
        }
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

void MainWindow::schedule_frame_fit()
{
  if (frame_fit_idle_.connected())
    return;
  frame_fit_idle_ = Glib::signal_idle().connect([this] {
    fit_frame_to_work_area();
    return false;
  });
}

void MainWindow::fit_frame_to_work_area()
{
  if (is_maximized())
    return;
  auto gdk_window = get_window();
  if (!gdk_window)
    return;
  auto display = gdk_window->get_display();
  if (!display)
    return;
  auto monitor = display->get_monitor_at_window(gdk_window);
  if (!monitor)
    return;
  int width = 0;
  int height = 0;
  get_size(width, height);
  if (width <= 0 || height <= 0)
    return;
  Gdk::Rectangle extents;
  gdk_window->get_frame_extents(extents);
  const int frame_width = extents.get_width() - width;
  const int frame_height = extents.get_height() - height;
  Gdk::Rectangle area;
  monitor->get_workarea(area);
  int fitted_width = width;
  int fitted_height = height;
  fit_window_frame(fitted_width, fitted_height, area.get_width(), area.get_height(), frame_width,
                   frame_height);
  if (fitted_width != width || fitted_height != height)
    resize(fitted_width, fitted_height);
}

bool MainWindow::on_delete_event(GdkEventAny* event)
{
  if (!confirm_discard_or_save())
    return true;
  window_memory_.store(settings_);
  reload_recent();
  settings_.save_fields(kSetWindow);
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
  template_item_ = add_item(*file, "New from _Template…", true);
  open_item_ = add_item(*file, "_Open…", true, GDK_KEY_o, Gdk::CONTROL_MASK);
  recent_item_ = add_item(*file, "Open _Recent", false);
  recent_item_->set_submenu(recent_menu_);
  save_item_ = add_item(*file, "_Save", true, GDK_KEY_s, Gdk::CONTROL_MASK);
  save_as_item_ = add_item(*file, "Save _As…", true);
  export_item_ = add_item(*file, "_Export…", true);
  print_item_ = add_item(*file, "_Print…", true, GDK_KEY_p, Gdk::CONTROL_MASK);
  page_setup_item_ = add_item(*file, "Page Set_up…", true);
  page_setup_item_->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_page_setup));
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
    settings_.save_fields(kSetStandardBar);
  });
  format->signal_toggled().connect([this, format] {
    settings_.show_format_toolbar = format->get_active();
    format_bar_.set_visible(settings_.show_format_toolbar);
    reload_recent();
    settings_.save_fields(kSetFormatBar);
  });
  side->signal_toggled().connect([this, side] {
    settings_.toolbars_side_by_side = side->get_active();
    apply_toolbar_row();
    reload_recent();
    settings_.save_fields(kSetToolbarsSide);
  });
  status->signal_toggled().connect([this, status] {
    settings_.show_statusbar = status->get_active();
    status_.set_visible(settings_.show_statusbar);
    reload_recent();
    settings_.save_fields(kSetStatusBar);
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
  view->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  header_footer_item_ = Gtk::manage(new Gtk::CheckMenuItem("_Header and Footer", true));
  header_footer_item_->signal_toggled().connect(
      sigc::mem_fun(*this, &MainWindow::on_header_footer));
  view->append(*header_footer_item_);

  auto* insert = add_menu("_Insert");
  picture_item_ = add_item(*insert, "_Picture…", true);
  insert_table_item_ = add_item(*insert, "_Table…", true);
  page_break_item_ = add_item(*insert, "Page _Break", true);
  footnote_item_ = add_item(*insert, "_Footnote", true);
  picture_item_->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_picture));
  insert_table_item_->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_insert_table));
  page_break_item_->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_page_break));
  footnote_item_->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_footnote));

  auto* format_menu = add_menu("F_ormat");
  font_item_ = add_item(*format_menu, "_Font…", true);
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
  tabs_item_ = add_item(*format_menu, "_Tabs…", true);
  borders_item_ = add_item(*format_menu, "Borders and Shadin_g…", true);
  columns_item_ = add_item(*format_menu, "C_olumns…", true);
  columns_item_->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_columns));

  auto* tools = add_menu("_Tools");
  spelling_item_ =
      add_item(*tools, "_Spelling…", true, GDK_KEY_F7, static_cast<Gdk::ModifierType>(0));
  options_item_ = add_item(*tools, "_Options…", true);

  auto* table = add_menu("T_able");
  table_insert_item_ = add_item(*table, "_Insert Table…", true);
  table_row_item_ = add_item(*table, "Insert _Row", false);
  table_column_item_ = add_item(*table, "Insert _Column", false);
  table->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  table_delete_row_item_ = add_item(*table, "_Delete Row", false);
  table_delete_column_item_ = add_item(*table, "Delete C_olumn", false);
  table_insert_item_->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_insert_table));
  table_row_item_->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_insert_row));
  table_column_item_->signal_activate().connect(
      sigc::mem_fun(*this, &MainWindow::on_insert_column));
  table_delete_row_item_->signal_activate().connect(
      sigc::mem_fun(*this, &MainWindow::on_delete_row));
  table_delete_column_item_->signal_activate().connect(
      sigc::mem_fun(*this, &MainWindow::on_delete_column));
  table->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  table_width_item_ = add_item(*table, "Table _Width…", false);
  table_width_item_->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_table_width));
  table->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  table_background_item_ = Gtk::manage(new Gtk::MenuItem("_Background", true));
  table_background_item_->set_sensitive(false);
  auto* colours = Gtk::manage(new Gtk::Menu());
  table_background_item_->set_submenu(*colours);
  for (int ink = 1; ink <= 16; ++ink) {
    const Ink chosen = static_cast<Ink>(ink);
    auto* item = Gtk::manage(new Gtk::MenuItem(ink_name(chosen, false), true));
    item->signal_activate().connect([this, chosen] { on_table_background(chosen); });
    colours->append(*item);
  }
  colours->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  auto* none = Gtk::manage(new Gtk::MenuItem("_None", true));
  none->signal_activate().connect([this] { on_table_background(Ink::Auto); });
  colours->append(*none);
  table->append(*table_background_item_);

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
  print_tool_ = add_tool(standard_bar_, "document-print", "Print", true);
  standard_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  cut_tool_ = add_tool(standard_bar_, "edit-cut", "Cut", false);
  copy_tool_ = add_tool(standard_bar_, "edit-copy", "Copy", false);
  paste_tool_ = add_tool(standard_bar_, "edit-paste", "Paste", false);
  painter_tool_ = Gtk::manage(new Gtk::ToggleToolButton());
  if (Gtk::IconTheme::get_default()->has_icon("draw-brush"))
    painter_tool_->set_icon_name("draw-brush");
  else
    fallback_label(*painter_tool_, "Painter");
  painter_tool_->set_tooltip_text("Format Painter");
  standard_bar_.append(*painter_tool_);
  painter_tool_->signal_toggled().connect(sigc::mem_fun(*this, &MainWindow::on_painter_toggled));
  // A click hits the toggle inside the tool item. glibmm connects after the
  // class handler, and that handler returns true, so a later slot never runs.
  // The press owns the state. The release returns true so the button does not
  // turn itself off on the next click.
  if (Gtk::Widget* face = painter_tool_->get_child()) {
    face->signal_button_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_painter_press),
                                              false);
    face->signal_button_release_event().connect(
        [](GdkEventButton* event) { return event && event->button == 1; }, false);
  }
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
  auto ink_button = [this](const char* tip, bool highlight) {
    auto* button = Gtk::manage(new Gtk::MenuToolButton());
    button->set_tooltip_text(tip);
    auto* image = Gtk::manage(new Gtk::Image());
    image->show();
    button->set_icon_widget(*image);
    auto* menu = Gtk::manage(new Gtk::Menu());
    for (int i = 0; i <= 16; ++i) {
      const Ink ink = static_cast<Ink>(i);
      auto* item = Gtk::manage(new Gtk::MenuItem(ink_name(ink, highlight)));
      item->signal_activate().connect([this, ink, highlight] {
        if (highlight)
          apply_highlight(ink);
        else
          apply_font_color(ink);
      });
      menu->append(*item);
    }
    menu->show_all();
    button->set_menu(*menu);
    button->signal_clicked().connect([this, highlight] {
      if (highlight)
        apply_highlight(highlight_ink_);
      else
        apply_font_color(font_ink_);
    });
    format_bar_.append(*button);
    return button;
  };
  font_color_button_ = ink_button("Font Color", false);
  highlight_button_ = ink_button("Highlight", true);
  paint_ink(font_color_button_, font_ink_);
  paint_ink(highlight_button_, highlight_ink_);
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
  proxy_button(*print_tool_, "_Print…");
  proxy_button(*cut_tool_, "Cu_t");
  proxy_button(*copy_tool_, "_Copy");
  proxy_button(*paste_tool_, "_Paste");
  if (painter_tool_)
    proxy_toggle(*painter_tool_, "Format Painter");
  proxy_button(*undo_tool_, "_Undo");
  proxy_button(*redo_tool_, "_Redo");
  proxy_combo(*font_item, font_combo_, "_Font");
  proxy_combo(*size_item, size_combo_, "Font Si_ze");
  proxy_toggle(*bold_toggle_, "_Bold");
  proxy_toggle(*italic_toggle_, "_Italic");
  proxy_toggle(*underline_toggle_, "_Underline");
  if (font_color_button_)
    proxy_ink(
        *font_color_button_, "Font _Color", false, [this] { return font_ink_; },
        [this](Ink ink) { apply_font_color(ink); });
  if (highlight_button_)
    proxy_ink(
        *highlight_button_, "_Highlight", true, [this] { return highlight_ink_; },
        [this](Ink ink) { apply_highlight(ink); });
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
  ruler_.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK);
  ruler_.signal_draw().connect(sigc::mem_fun(*this, &MainWindow::on_ruler_draw));
  ruler_.signal_button_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_ruler_press));
  ruler_.signal_button_release_event().connect(sigc::mem_fun(*this, &MainWindow::on_ruler_release));
  ruler_.signal_motion_notify_event().connect(sigc::mem_fun(*this, &MainWindow::on_ruler_motion));

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
  auto prepare_story = [](Gtk::TextView& view) {
    view.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    view.set_hexpand(true);
    view.set_left_margin(8);
    view.set_right_margin(8);
    view.get_style_context()->add_class("page-text");
    view.set_no_show_all(true);
  };
  prepare_story(header_view_);
  prepare_story(notes_view_);
  notes_view_.set_no_show_all(false);
  prepare_story(footer_view_);
  auto connect_story = [this](Gtk::TextView& view) {
    view.signal_draw().connect(
        [this, &view](const Cairo::RefPtr<Cairo::Context>& cr) {
          on_story_draw(view, cr);
          return false;
        },
        true);
  };
  connect_story(header_view_);
  connect_story(notes_view_);
  connect_story(footer_view_);
  note_label_.set_halign(Gtk::ALIGN_START);
  note_label_.set_valign(Gtk::ALIGN_START);
  note_marks_.put(note_label_, 0, 0);
  note_marks_.set_margin_right(6);
  note_marks_.set_halign(Gtk::ALIGN_START);
  note_marks_.set_valign(Gtk::ALIGN_START);
  note_box_.pack_start(note_marks_, Gtk::PACK_SHRINK);
  note_box_.pack_start(notes_view_, Gtk::PACK_EXPAND_WIDGET);
  notes_view_.signal_size_allocate().connect([this](Gtk::Allocation&) { queue_note_numbers(); });
  notes_view_.signal_focus_in_event().connect([this](GdkEventFocus*) {
    story_target_ = 3;
    story_focus_view_ = &notes_view_;
    note_editing_ = true;
    clear_cell_edit_on_story(false, false, true);
    sync_format_controls();
    return false;
  });
  notes_view_.signal_focus_out_event().connect([this](GdkEventFocus*) {
    note_editing_ = false;
    release_note_scroll(false);
    // A rebuild clears the body while this view still holds the keyboard.
    // Placing the band then uses the empty page. The rebuild places it.
    if (!loading_ && notes_view_.get_parent() != nullptr)
      place_notes();
    return false;
  });
  note_box_.set_halign(Gtk::ALIGN_START);
  note_box_.set_valign(Gtk::ALIGN_START);
  note_box_.set_no_show_all(true);
  note_box_.hide();
  // The body is the only text view until a header, footer, or footnote is
  // shown. Tests that take the first text view find this one. The overlay's
  // main child is the page column, so that search still reaches the body
  // before a note.
  page_box_.pack_start(text_, Gtk::PACK_EXPAND_WIDGET);
  page_overlay_.add(page_box_);
  page_overlay_.add_overlay(note_box_);
  // gtkmm's signal proxy does not deliver this out-rectangle. The C signal
  // does: the note's place is that allocation, not a margin.
  g_signal_connect(page_overlay_.gobj(), "get-child-position",
                   G_CALLBACK(+[](GtkOverlay*, GtkWidget* widget, GdkRectangle* allocation,
                                  gpointer data) -> gboolean {
                     auto* window = static_cast<MainWindow*>(data);
                     if (allocation == nullptr)
                       return FALSE;
                     int x = 0;
                     int y = 0;
                     int w = 1;
                     int h = 1;
                     if (!window->note_overlay_box(widget, x, y, w, h))
                       return FALSE;
                     allocation->x = x;
                     allocation->y = y;
                     allocation->width = std::max(1, w);
                     allocation->height = std::max(1, h);
                     return TRUE;
                   }),
                   this);
  page_.add(page_overlay_);
  auto remember_story_begin = [this](Gtk::TextView& view) {
    view.get_buffer()->signal_begin_user_action().connect([this, &view] {
      if (loading_ || restoring_)
        return;
      story_begin_view_ = &view;
      story_begin_caret_ = view.get_buffer()->get_insert()->get_iter().get_offset();
    });
  };
  remember_story_begin(header_view_);
  remember_story_begin(footer_view_);
  remember_story_begin(notes_view_);
  header_view_.get_buffer()->signal_end_user_action().connect([this] { on_story_end(&header_); });
  footer_view_.get_buffer()->signal_end_user_action().connect([this] { on_story_end(&footer_); });
  notes_view_.get_buffer()->signal_end_user_action().connect([this] { on_notes_end(); });
  header_view_.signal_key_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_story_key));
  footer_view_.signal_key_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_story_key));
  notes_view_.signal_key_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_story_key));
  header_view_.signal_focus_in_event().connect([this](GdkEventFocus*) {
    story_target_ = 1;
    story_focus_view_ = &header_view_;
    clear_cell_edit_on_story(true, false, false);
    sync_format_controls();
    return false;
  });
  footer_view_.signal_focus_in_event().connect([this](GdkEventFocus*) {
    story_target_ = 2;
    story_focus_view_ = &footer_view_;
    clear_cell_edit_on_story(false, true, false);
    sync_format_controls();
    return false;
  });
  text_.signal_button_release_event().connect(
      [this](GdkEventButton* event) {
        if (event && event->button == 1)
          queue_painter(text_);
        return false;
      },
      true);
  text_.signal_motion_notify_event().connect(
      [this](GdkEventMotion* event) {
        if (!painter_on_ || !event || !event->window)
          return false;
        auto window = Glib::wrap(event->window, true);
        if (!window)
          return false;
        if (!painter_cursor_)
          painter_cursor_ = Gdk::Cursor::create(window->get_display(), Gdk::PENCIL);
        window->set_cursor(painter_cursor_);
        return false;
      },
      true);
  text_.signal_button_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_context), false);
  text_.signal_button_press_event().connect(
      [this](GdkEventButton*) {
        reveal_note_ = 0;
        story_target_ = 0;
        story_focus_view_ = nullptr;
        return false;
      },
      false);
  text_.signal_key_press_event().connect(
      [this](GdkEventKey*) {
        reveal_note_ = 0;
        story_target_ = 0;
        story_focus_view_ = nullptr;
        return false;
      },
      false);
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
  // The sheet is taller than the window. GTK scrolls a focused descendant
  // into view by the top of its allocation. The page starts at its top
  // margin, so focus returning from a toolbar list jumps to the first page.
  // scroll_to_caret() is what moves this scroller.
  if (auto* viewport = dynamic_cast<Gtk::Container*>(paste_.get_child())) {
    viewport->set_focus_vadjustment({});
    viewport->set_focus_hadjustment({});
  }
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
    // A letter whose line has not been shown to move skips both measures:
    // each one walks every paragraph, and the status idle paginates if the
    // line's box changed.
    if (!page_edit_keeps_height()) {
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
    }
    if (restore_scroll_)
      restore_held_view(false);
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
  // A style apply, or collapsing a selection the caret has left, moves marks
  // without the user going to the first line.
  // A zoom swaps picture pixbufs and moves marks. That is not the caret.
  if (hold_scroll_ || releasing_ || scaling_pictures_)
    return;
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

void MainWindow::hold_view()
{
  auto v = paste_.get_vadjustment();
  auto h = paste_.get_hadjustment();
  if (!v || !h)
    return;
  held_v_ = v->get_value();
  held_h_ = h->get_value();
  hold_scroll_ = true;
  restore_scroll_ = true;
  follow_caret_ = false;
  if (scroll_lock_)
    return;
  scroll_lock_ = true;
  auto pin = [this](const Glib::RefPtr<Gtk::Adjustment>& adj, double want) {
    if (!scroll_lock_ || scroll_snapping_)
      return;
    const double page = adj->get_page_size();
    const double room = std::max(adj->get_lower(), adj->get_upper() - std::max(0.0, page));
    const double keep = std::max(adj->get_lower(), std::min(want, room));
    if (adj->get_value() == keep)
      return;
    scroll_snapping_ = true;
    adj->set_value(keep);
    scroll_snapping_ = false;
  };
  scroll_lock_v_ = v->signal_value_changed().connect([this, v, pin] { pin(v, held_v_); });
  scroll_lock_h_ = h->signal_value_changed().connect([this, h, pin] { pin(h, held_h_); });
}

void MainWindow::restore_held_view(bool finish, bool reveal)
{
  if (!hold_scroll_ && !restore_scroll_)
    return;
  auto v = paste_.get_vadjustment();
  auto h = paste_.get_hadjustment();
  if (!v || !h)
    return;
  hold_scroll_ = true;
  follow_caret_ = false;
  auto clamp = [](const Glib::RefPtr<Gtk::Adjustment>& adj, double want) {
    const double page = adj->get_page_size();
    const double room = std::max(adj->get_lower(), adj->get_upper() - std::max(0.0, page));
    adj->set_value(std::max(adj->get_lower(), std::min(want, room)));
  };
  clamp(v, held_v_);
  clamp(h, held_h_);
  if (!finish)
    return;
  // The short measures during the rebuild were ignored. The ones that repeat
  // now are the sheet, and the caret's place still fits on it.
  restore_scroll_ = false;
  apply_page_size();
  clamp(v, held_v_);
  clamp(h, held_h_);
  hold_scroll_ = false;
  scroll_lock_ = false;
  scroll_lock_v_.disconnect();
  scroll_lock_h_.disconnect();
  // A heading grows the line. Revealing it would pin that line to the bottom
  // of the window. The pasteboard stays where the style found it.
  if (reveal)
    reveal_editing_caret();
}

void MainWindow::release_view(bool reveal)
{
  restore_held_view(false);
  if (restore_idle_.connected())
    return;
  // After the page-size idle and the resize it queues, so the caret's
  // coordinates are the laid-out ones.
  restore_idle_ = Glib::signal_idle().connect(
      [this, reveal] {
        restore_held_view(true, reveal);
        return false;
      },
      Glib::PRIORITY_LOW);
}

void MainWindow::scroll_to_caret()
{
  if (hold_scroll_)
    return;
  // Typing in a note lays the page out again on the key. The body caret is
  // the marker, often at the top of the sheet, and a note taller than the
  // window cannot show its first line and its last line together. Either
  // scroll leaves the character just typed.
  Gtk::Widget* const focused = get_focus();
  if (focused == &header_view_ || focused == &footer_view_ || is_note_view(focused)) {
    if (follow_caret_)
      reveal_story_caret(static_cast<Gtk::TextView&>(*focused));
    return;
  }
  if (reveal_note_ > 0) {
    reveal_note_view();
    return;
  }
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
  // On the first line, all the way to the top: the page's edge and the gray
  // above it show. On the last line, all the way to the bottom only when
  // that line is the foot of the sheet, so the edge and the gray under it
  // show, not a margin short of them. The text view is as tall as the sheet,
  // so a short page's last line is the end of the buffer and still near the
  // top: the empty page below it is not the foot. A window only a little
  // shorter than the sheet would otherwise count that line as "within one
  // screen of the foot" and jump there, hiding the line just typed.
  Gdk::Rectangle first;
  Gdk::Rectangle last;
  text_.get_iter_location(buffer_->begin(), first);
  text_.get_iter_location(buffer_->end(), last);
  auto v = paste_.get_vadjustment();
  const double page = v->get_page_size();
  // Buffer y starts under the top margin, so the gap from the line to the
  // bottom of the text view includes both margins when the line is the last
  // one of a page the text fills.
  const int gap = text_.get_allocated_height() - (rect.get_y() + rect.get_height());
  const int slack =
      text_.get_top_margin() + text_.get_bottom_margin() + std::max(rect.get_height(), 1);
  const bool line_at_foot = gap <= slack;
  // `y` is the caret line in the board's coordinates, which is what the
  // pasteboard's adjustment measures. Scrolling to the bottom has to leave
  // that line on screen (a footer below the body can be taller than the
  // window).
  const bool bottom_shows_line = page > 0 && v->get_upper() - static_cast<double>(y) <= page;
  if (rect.get_y() <= first.get_y())
    v->set_value(v->get_lower());
  else if (rect.get_y() >= last.get_y() && line_at_foot && bottom_shows_line)
    v->set_value(std::max(v->get_lower(), v->get_upper() - page));
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
  // Recomputed from the zoom, the view, and the page setup every time, never
  // kept from the last layout: the text view asks for exactly this width, so
  // a smaller zoom is a narrower allocation and a narrower wrap.
  const int width = geometry().page_width;
  text_.set_layout_width(width);
  apply_margins();
  fit_note_views();
  // Paragraph tags carry the text inset in their margins, so they are
  // restyled when the zoom or the view changes it.
  if (z != styled_zoom_ || view_ != styled_view_) {
    styled_zoom_ = z;
    styled_view_ = view_;
    restyle_tags();
  }
  // A letter inside a line whose box has not been shown to change keeps the
  // height already on the page. The status idle measures that line after
  // GTK has laid it out and paginates if the box moved.
  int cached_h = 0;
  int cached_w = 0;
  page_.get_size_request(cached_w, cached_h);
  (void)cached_w;
  const bool reuse = page_edit_keeps_height();
  int height = cached_h;
  if (!reuse)
    height = page_height();
  int current_w = 0;
  int current_h = 0;
  page_.get_size_request(current_w, current_h);
  // A rebuild while the hold is up measures short once: the tables and
  // pictures do not have their height yet. The pasteboard would clamp up to
  // keep that short sheet on screen, then the real height comes back. Keep
  // the height the hold started with until the rebuild lets go.
  if (restore_scroll_ && current_h > 0 && height < current_h)
    height = current_h;
  if (current_w != width || current_h != height)
    page_.set_size_request(width, height);
  sizing_ = false;
  queue_page_status();
}

// The page's height for the view, the zoom, the window and the text as they
// are now, and nothing else: the least height (A4 in Page, the pasteboard's
// in Draft), or the text's own when it is taller. In Page, every sheet the
// text needs is a whole page, with the gray gap between sheets. A short
// first page stays one sheet.
int MainWindow::page_height() const
{
  const ViewGeometry g = geometry();
  // Draft's white area is at least as tall as the visible pasteboard, so
  // the text starts at the top and there is no gray below it.
  const int base_h = g.chrome ? g.page_height : std::max(1, paste_.get_allocated_height() - 4);
  const int width = g.page_width;
  int min_h = 0;
  int nat_h = 0;
  text_.get_preferred_height_for_width(std::max(1, width - (g.chrome ? 2 : 0)), min_h, nat_h);
  int extra = 0;
  for (const Gtk::TextView* story : {&header_view_, &footer_view_}) {
    if (!story->get_visible() || story->get_parent() == nullptr)
      continue;
    int story_min = 0;
    int story_nat = 0;
    story->get_preferred_height_for_width(std::max(1, width), story_min, story_nat);
    extra += story_nat;
  }
  int height = std::max(base_h, nat_h + extra);
  if (g.chrome && buffer_ && text_.get_realized()) {
    const int sheets = std::max(1, print_page_count());
    if (sheets > 1) {
      const int stack =
          sheets * std::max(1, g.page_height) + (sheets - 1) * std::max(0, g.gap) + extra;
      height = std::max(height, stack);
    }
  }
  return height;
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

// "Page n of m". The count follows the page setup. A page break is kept, and
// the count includes the sheet it starts. A page break on a table cell does
// not. See page_count() in view.hpp. The
// gray between sheets is not part of the count: seam_extra_through() takes
// it back out.
void MainWindow::update_page_status()
{
  if (!buffer_)
    return;
  // The handler kept the page height for this letter. Judge the line the
  // caret is on now, even if a paragraph-tag signal cleared the other flag.
  if (page_letter_) {
    page_edit_maybe_ = true;
    page_edit_line_ = buffer_->get_iter_at_mark(buffer_->get_insert()).get_line();
  }
  // The caret is still on the line we paginated, and that line's pixels did
  // not move, so the seams and "Page n of m" are already right.
  if (page_box_unchanged()) {
    page_edit_maybe_ = false;
    page_letter_ = false;
    // The letter did not move this line. The stored sheets stay correct, and
    // a later status pass must not measure them again.
    if (seam_edit_ == SeamEdit::Line)
      seam_edit_ = SeamEdit::None;
    return;
  }
  page_letter_ = false;
  // The handler kept the old page height. The line's box moved, so measure
  // before rebuilding the seams.
  const bool remeasure = page_edit_maybe_;
  page_edit_maybe_ = false;
  if (remeasure)
    apply_page_size();
  update_page_seams();
  Glib::ustring label;
  if (view_ == ViewMode::Page && seam_sheets_valid_ && seam_geometry_matches()) {
    PageCount count;
    count.pages = std::max(1, seam_sheets_);
    const int at = buffer_->get_insert()->get_iter().get_offset();
    int caret_page = 1;
    for (const SeamLine& line : seam_lines_) {
      if (line.page > 0 && at >= line.start)
        caret_page = line.page;
    }
    seam_caret_page_ = caret_page;
    count.page = std::clamp(caret_page, 1, count.pages);
    label = page_label(count);
  } else {
    int end_y = 0;
    int end_h = 0;
    text_.get_line_yrange(buffer_->end(), end_y, end_h);
    const auto caret_iter = buffer_->get_iter_at_mark(buffer_->get_insert());
    Gdk::Rectangle caret;
    text_.get_iter_location(caret_iter, caret);
    const int per = page_text_height(page_setup_, zoom_factor());
    const int content_end = page_content_y(end_y + end_h, buffer_->get_char_count());
    const int content_caret = page_content_y(caret.get_y(), caret_iter.get_offset());
    label = page_label(page_count(content_end, content_caret, per));
  }
  if (page_label_.get_text() != label)
    page_label_.set_text(label);
  remember_page_cache();
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
  settings_.save_fields(kSetZoom);
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
  if (!buffer_) {
    draw_tab_well(cr, ruler_kind_);
    return true;
  }
  const Indents indents = focused_para().indents;
  const int left = origin_x + margin_left();
  const int right = origin_x + page_w - margin_right();
  const double h = self.get_height();
  const PageSetup page = clamp_page(page_setup_);
  if (page.columns > 1 && right > left) {
    const int gap_px = std::max(0, indent_px(page.column_gap));
    const int gaps = page.columns - 1;
    const int text_w = right - left;
    const int col_w = std::max(1, (text_w - gaps * gap_px) / page.columns);
    cr->set_source_rgb(0.45, 0.45, 0.45);
    for (int column = 1; column < page.columns; ++column) {
      const double x = left + column * col_w + (column - 1) * gap_px + gap_px / 2.0 + 0.5;
      cr->move_to(x, 2);
      cr->line_to(x, h - 2);
    }
    cr->stroke();
  }
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

  // Custom stops only. The half-inch grid stays off the ruler; Tab still
  // lands on it. A drag previews the unsorted list so the grabbed index holds.
  const std::vector<TabStop> live = focused_para().tabs;
  const std::vector<TabStop>& preview = ruler_drag_ ? ruler_tabs_ : live;
  for (const TabStop& stop : preview) {
    const double sx = left + indent_px(stop.position) + 0.5;
    if (stop.align == TabAlign::Bar) {
      cr->set_source_rgb(0.2, 0.2, 0.2);
      cr->set_line_width(1);
      cr->move_to(sx, 2);
      cr->line_to(sx, h - 2);
      cr->stroke();
    } else {
      stroke_tab_icon(cr, sx, h / 2.0, stop.align);
    }
  }
  draw_tab_well(cr, ruler_kind_);
  return true;
}

int MainWindow::ruler_twips(int x)
{
  int origin_x = 0;
  int origin_y = 0;
  page_.translate_coordinates(ruler_, 0, 0, origin_x, origin_y);
  const double z = zoom_factor() > 0 ? zoom_factor() : 1.0;
  const int dx = x - (origin_x + margin_left());
  int twips = static_cast<int>(std::lround(static_cast<double>(dx) * kScreenPageTwips /
                                           (static_cast<double>(kScreenPageWidth) * z)));
  if (twips < 0)
    twips = 0;
  // An eighth of an inch.
  twips = ((twips + 90) / 180) * 180;
  if (twips > kMaxIndent)
    twips = kMaxIndent;
  return twips;
}

bool MainWindow::on_ruler_press(GdkEventButton* event)
{
  if (!event || event->button != 1 || event->type != GDK_BUTTON_PRESS || !buffer_)
    return false;
  if (event->x < 18.0) {
    ruler_kind_ = next_ruler_kind(ruler_kind_);
    ruler_.queue_draw();
    return true;
  }
  int origin_x = 0;
  int origin_y = 0;
  page_.translate_coordinates(ruler_, 0, 0, origin_x, origin_y);
  const int left = origin_x + margin_left();
  const int page_w = page_.get_allocated_width();
  const int right = origin_x + page_w - margin_right();
  std::vector<TabStop> tabs = focused_para().tabs;
  int grab = -1;
  double best = 6.0;
  for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
    const double sx = left + indent_px(tabs[static_cast<size_t>(i)].position) + 0.5;
    const double dist = std::fabs(event->x - sx);
    if (dist <= best) {
      best = dist;
      grab = i;
    }
  }
  if (grab < 0) {
    const Indents indents = focused_para().indents;
    const double marks[3] = {left + indent_px(indents.left + indents.first) + 0.5,
                             left + indent_px(indents.left) + 0.5,
                             right - indent_px(indents.right) + 0.5};
    for (double mark : marks) {
      if (std::fabs(event->x - mark) <= 5.0)
        return true;
    }
    if (static_cast<int>(tabs.size()) >= kMaxTabStops)
      return true;
    const int twips = ruler_twips(static_cast<int>(std::lround(event->x)));
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
      if (tabs[static_cast<size_t>(i)].position == twips) {
        grab = i;
        break;
      }
    }
    if (grab < 0) {
      TabStop stop;
      stop.position = twips;
      stop.align = ruler_kind_;
      stop.leader = TabLeader::None;
      tabs.push_back(stop);
      grab = static_cast<int>(tabs.size()) - 1;
    }
  }
  ruler_tabs_ = std::move(tabs);
  ruler_grab_ = grab;
  ruler_press_x_ = static_cast<int>(std::lround(event->x));
  ruler_drag_ = true;
  ruler_moved_ = false;
  ruler_.queue_draw();
  return true;
}

bool MainWindow::on_ruler_motion(GdkEventMotion* event)
{
  if (!ruler_drag_ || !event || (event->state & GDK_BUTTON1_MASK) == 0)
    return false;
  if (ruler_grab_ < 0 || ruler_grab_ >= static_cast<int>(ruler_tabs_.size()))
    return false;
  if (std::fabs(event->x - ruler_press_x_) <= 6.0)
    return true;
  const int twips = ruler_twips(static_cast<int>(std::lround(event->x)));
  TabStop& stop = ruler_tabs_[static_cast<size_t>(ruler_grab_)];
  if (stop.position != twips) {
    stop.position = twips;
    ruler_moved_ = true;
    ruler_.queue_draw();
  }
  return true;
}

bool MainWindow::on_ruler_release(GdkEventButton* event)
{
  if (!event || event->button != 1 || !ruler_drag_)
    return false;
  std::vector<TabStop> tabs = std::move(ruler_tabs_);
  ruler_tabs_.clear();
  const int grab = ruler_grab_;
  const bool moved = ruler_moved_;
  ruler_drag_ = false;
  ruler_moved_ = false;
  ruler_grab_ = -1;
  const int height = std::max(1, ruler_.get_allocated_height());
  if (grab >= 0 && grab < static_cast<int>(tabs.size())) {
    if (event->y < -8.0 || event->y > height + 8.0) {
      tabs.erase(tabs.begin() + grab);
    } else if (moved) {
      // Last, so a stop dropped on another one wins when the list is clamped.
      TabStop kept = tabs[static_cast<size_t>(grab)];
      kept.position = ruler_twips(static_cast<int>(std::lround(event->x)));
      tabs.erase(tabs.begin() + grab);
      tabs.push_back(kept);
    }
  }
  apply_focused_tabs(std::move(tabs));
  ruler_.queue_draw();
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

int MainWindow::print_page_count() const
{
  // The same count as the status bar. The widget is taller than that by the
  // gray gaps between sheets, and those gaps are not pages. The stored count
  // is that result from the last pagination. Measuring the buffer's end lays
  // out every line below the caret.
  if (seam_sheets_valid_ && seam_geometry_matches())
    return std::max(1, seam_sheets_);
  ++page_walks_;
  if (!buffer_)
    return 1;
  int end_y = 0;
  int end_h = 0;
  text_.get_line_yrange(buffer_->end(), end_y, end_h);
  const int content = std::max(0, page_content_y(end_y + end_h, buffer_->get_char_count()));
  const int per = std::max(1, page_text_height(page_setup_, zoom_factor()));
  return std::max(1, (content + per - 1) / per);
}

void MainWindow::draw_print_page(const Glib::RefPtr<Gtk::PrintContext>& context, int page_nr)
{
  const ViewGeometry sheet = view_geometry(ViewMode::Page, zoom_factor(), page_setup_);
  // One sheet, then the gray gap the screen leaves between sheets. The clip
  // below is one sheet tall, so the gap is not drawn on the paper.
  const int band = std::max(1, sheet.page_height + sheet.gap);
  const int src_w =
      page_.get_allocated_width() > 1 ? page_.get_allocated_width() : std::max(1, sheet.page_width);
  const Cairo::RefPtr<Cairo::Context> cr = context->get_cairo_context();
  const double scale = context->get_width() / static_cast<double>(std::max(1, src_w));
  cr->save();
  cr->set_source_rgb(1, 1, 1);
  cr->paint();
  cr->rectangle(0, 0, context->get_width(), context->get_height());
  cr->clip();
  cr->scale(scale, scale);
  cr->translate(0, -static_cast<double>(page_nr) * band);
  gtk_widget_draw(static_cast<Gtk::Widget&>(page_).gobj(), cr->cobj());
  cr->restore();
}

bool MainWindow::export_page(const std::string& path)
{
  std::string error;
  return run_print(Gtk::PRINT_OPERATION_ACTION_EXPORT, path, error);
}

void MainWindow::on_print()
{
  std::string error;
  if (!run_print(Gtk::PRINT_OPERATION_ACTION_PRINT_DIALOG, "", error) && !error.empty())
    tell(error);
}

bool MainWindow::run_print(Gtk::PrintOperationAction action, const std::string& path,
                           std::string& error)
{
  error.clear();
  if (!buffer_) {
    error = "Could not print this document.";
    return false;
  }
  // Draft keeps the line breaks and moves the text. The sheet that prints is
  // the page view, put back when this returns.
  const ViewMode saved = view_;
  struct Restore {
    MainWindow& window;
    ViewMode mode;
    ~Restore()
    {
      if (window.view_ != mode)
        window.set_view(mode);
    }
  } restore{*this, saved};
  if (view_ != ViewMode::Page)
    set_view(ViewMode::Page);

  auto operation = Gtk::PrintOperation::create();
  // The file already stores the edges as they print. A landscape flag must
  // not rotate them a second time.
  const double width_pt = static_cast<double>(page_setup_.paper_width) / 20.0;
  const double height_pt = static_cast<double>(page_setup_.paper_height) / 20.0;
  Gtk::PaperSize paper("write-it", "Write-It", width_pt, height_pt, Gtk::UNIT_POINTS);
  auto setup = Gtk::PageSetup::create();
  setup->set_paper_size(paper);
  setup->set_orientation(Gtk::PAGE_ORIENTATION_PORTRAIT);
  operation->set_default_page_setup(setup);
  operation->set_use_full_page(true);
  operation->set_unit(Gtk::UNIT_POINTS);
  operation->set_job_name(save_path_.empty() ? "Write-It" : Glib::path_get_basename(save_path_));
  if (action == Gtk::PRINT_OPERATION_ACTION_EXPORT)
    operation->set_export_filename(path);

  Gtk::PrintOperation* op = operation.get();
  op->signal_begin_print().connect(
      [this, op](const Glib::RefPtr<Gtk::PrintContext>&) { op->set_n_pages(print_page_count()); });
  op->signal_draw_page().connect([this](const Glib::RefPtr<Gtk::PrintContext>& context,
                                        int page_nr) { draw_print_page(context, page_nr); });

  try {
    const Gtk::PrintOperationResult result = operation->run(action, *this);
    return result == Gtk::PRINT_OPERATION_RESULT_APPLY ||
           result == Gtk::PRINT_OPERATION_RESULT_CANCEL ||
           result == Gtk::PRINT_OPERATION_RESULT_IN_PROGRESS;
  } catch (const Glib::Error& failure) {
    error = failure.what();
    return false;
  }
}

}  // namespace writeit
