/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"

#include "about_dialog.hpp"
#include "config.hpp"

#include <glibmm/miscutils.h>

#include <algorithm>

namespace writeit {
namespace {

constexpr int kPageW = 540;
constexpr int kPageH = 470;
constexpr int kZooms[] = {50, 75, 100, 150, 200, 0};

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

}  // namespace

MainWindow::MainWindow()
{
  settings_.load();
  set_title("Write-It - Untitled");
  set_default_size(settings_.window_width, settings_.window_height);
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

  show_all_children();
  apply_chrome();
  apply_page_size();
}

bool MainWindow::on_delete_event(GdkEventAny* event)
{
  if (get_width() > 0 && get_height() > 0) {
    settings_.window_width = get_width();
    settings_.window_height = get_height();
    settings_.save();
  }
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
  add_item(*file, "_New", false, GDK_KEY_n, Gdk::CONTROL_MASK);
  add_item(*file, "New from _Template…", false);
  add_item(*file, "_Open…", false, GDK_KEY_o, Gdk::CONTROL_MASK);
  add_item(*file, "Open _Recent", false);
  add_item(*file, "_Save", true, GDK_KEY_s, Gdk::CONTROL_MASK);
  add_item(*file, "Save _As…", false);
  add_item(*file, "_Export…", false);
  add_item(*file, "_Print…", false, GDK_KEY_p, Gdk::CONTROL_MASK);
  add_item(*file, "Page Set_up…", false);
  file->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item(*file, "_Close", true, GDK_KEY_w, Gdk::CONTROL_MASK)->signal_activate().connect([this] {
    set_title("Write-It - Untitled");
  });
  add_item(*file, "E_xit", true, GDK_KEY_q, Gdk::CONTROL_MASK)->signal_activate().connect([this] {
    close();
  });

  auto* edit = add_menu("_Edit");
  add_item(*edit, "_Undo", false, GDK_KEY_z, Gdk::CONTROL_MASK);
  auto* redo = add_item(*edit, "_Redo", false, GDK_KEY_y, Gdk::CONTROL_MASK);
  redo->add_accelerator("activate", accel_, GDK_KEY_z, Gdk::CONTROL_MASK | Gdk::SHIFT_MASK,
                        Gtk::ACCEL_VISIBLE);
  edit->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item(*edit, "Cu_t", false, GDK_KEY_x, Gdk::CONTROL_MASK);
  add_item(*edit, "_Copy", false, GDK_KEY_c, Gdk::CONTROL_MASK);
  add_item(*edit, "_Paste", false, GDK_KEY_v, Gdk::CONTROL_MASK);
  add_item(*edit, "_Delete", false, GDK_KEY_Delete, static_cast<Gdk::ModifierType>(0));
  edit->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item(*edit, "Select _All", false, GDK_KEY_a, Gdk::CONTROL_MASK);
  edit->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item(*edit, "_Find…", false, GDK_KEY_f, Gdk::CONTROL_MASK);
  add_item(*edit, "R_eplace…", false, GDK_KEY_h, Gdk::CONTROL_MASK);

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
    settings_.save();
  });
  format->signal_toggled().connect([this, format] {
    settings_.show_format_toolbar = format->get_active();
    format_bar_.set_visible(settings_.show_format_toolbar);
    settings_.save();
  });
  side->signal_toggled().connect([this, side] {
    settings_.toolbars_side_by_side = side->get_active();
    apply_toolbar_row();
    settings_.save();
  });
  status->signal_toggled().connect([this, status] {
    settings_.show_statusbar = status->get_active();
    status_.set_visible(settings_.show_statusbar);
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
  auto* page = Gtk::manage(new Gtk::RadioMenuItem(page_group, "_Page", true));
  auto* draft = Gtk::manage(new Gtk::RadioMenuItem(page_group, "_Draft", true));
  page->set_active(true);
  draft->set_sensitive(false);
  view->append(*page);
  view->append(*draft);

  auto* insert = add_menu("_Insert");
  add_item(*insert, "_Picture…", false);
  add_item(*insert, "_Table…", false);
  add_item(*insert, "Page _Break", false);
  add_item(*insert, "_Footnote", false);

  auto* format_menu = add_menu("F_ormat");
  add_item(*format_menu, "_Font…", false);
  add_item(*format_menu, "_Bold", false, GDK_KEY_b, Gdk::CONTROL_MASK);
  add_item(*format_menu, "_Italic", false, GDK_KEY_i, Gdk::CONTROL_MASK);
  add_item(*format_menu, "_Underline", false, GDK_KEY_u, Gdk::CONTROL_MASK);
  format_menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item(*format_menu, "Align _Left", false);
  add_item(*format_menu, "_Center", false);
  add_item(*format_menu, "Align _Right", false);
  format_menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item(*format_menu, "_Style…", false);
  add_item(*format_menu, "B_ullets", false);
  add_item(*format_menu, "_Numbering", false);
  format_menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item(*format_menu, "_Paragraph…", false);
  add_item(*format_menu, "C_olumns…", false);

  auto* tools = add_menu("_Tools");
  add_item(*tools, "_Spelling…", false, GDK_KEY_F7, static_cast<Gdk::ModifierType>(0));
  add_item(*tools, "_Options…", false);

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

  auto* cut = Gtk::manage(new Gtk::MenuItem("Cu_t", true));
  auto* copy = Gtk::manage(new Gtk::MenuItem("_Copy", true));
  auto* paste = Gtk::manage(new Gtk::MenuItem("_Paste", true));
  cut->set_sensitive(false);
  copy->set_sensitive(false);
  paste->set_sensitive(false);
  context_.append(*cut);
  context_.append(*copy);
  context_.append(*paste);
  context_.show_all();
}

Gtk::ToolButton* MainWindow::add_tool(Gtk::Toolbar& bar, const char* icon, const char* tip,
                                      bool sensitive)
{
  auto* button = Gtk::manage(new Gtk::ToolButton());
  if (Gtk::IconTheme::get_default()->has_icon(icon))
    button->set_icon_name(icon);
  else
    button->set_label(tip);
  button->set_tooltip_text(tip);
  button->set_sensitive(sensitive);
  bar.append(*button);
  return button;
}

void MainWindow::build_toolbars()
{
  standard_bar_.set_toolbar_style(Gtk::TOOLBAR_ICONS);
  standard_bar_.set_icon_size(Gtk::ICON_SIZE_SMALL_TOOLBAR);
  standard_bar_.set_hexpand(false);
  standard_bar_.set_show_arrow(false);
  format_bar_.set_toolbar_style(Gtk::TOOLBAR_ICONS);
  format_bar_.set_icon_size(Gtk::ICON_SIZE_SMALL_TOOLBAR);
  format_bar_.set_hexpand(false);
  format_bar_.set_show_arrow(false);
  toolbars_.pack_start(standard_bar_, Gtk::PACK_SHRINK);
  toolbars_.pack_start(format_bar_, Gtk::PACK_SHRINK);

  add_tool(standard_bar_, "document-new", "New", false);
  add_tool(standard_bar_, "document-open", "Open", false);
  add_tool(standard_bar_, "document-save", "Save", true);
  standard_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  add_tool(standard_bar_, "document-print", "Print", false);
  standard_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  add_tool(standard_bar_, "edit-cut", "Cut", false);
  add_tool(standard_bar_, "edit-copy", "Copy", false);
  add_tool(standard_bar_, "edit-paste", "Paste", false);
  standard_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  add_tool(standard_bar_, "edit-undo", "Undo", false);
  add_tool(standard_bar_, "edit-redo", "Redo", false);

  font_combo_.append("Sans");
  font_combo_.set_active(0);
  font_combo_.set_sensitive(false);
  font_combo_.set_size_request(128, -1);
  font_combo_.set_tooltip_text("Font");
  size_combo_.set_size_request(52, -1);
  size_combo_.set_tooltip_text("Size");
  for (const char* size : {"8", "9", "10", "11", "12", "14", "16", "18", "24", "36"})
    size_combo_.append(size);
  size_combo_.set_active_text("11");
  size_combo_.set_sensitive(false);
  style_combo_.append("Body text");
  style_combo_.set_active(0);
  style_combo_.set_sensitive(false);
  style_combo_.set_size_request(110, -1);
  style_combo_.set_tooltip_text("Style");

  auto hold = [](Gtk::Widget& child, int width) {
    auto* item = Gtk::manage(new Gtk::ToolItem());
    child.set_size_request(width, -1);
    item->add(child);
    return item;
  };
  format_bar_.append(*hold(font_combo_, 128));
  format_bar_.append(*hold(size_combo_, 52));

  auto toggle = [this](const char* icon, const char* tip, bool active) {
    auto* button = Gtk::manage(new Gtk::ToggleToolButton());
    if (Gtk::IconTheme::get_default()->has_icon(icon))
      button->set_icon_name(icon);
    else
      button->set_label(tip);
    button->set_tooltip_text(tip);
    button->set_active(active);
    button->set_sensitive(false);
    format_bar_.append(*button);
  };
  toggle("format-text-bold", "Bold", false);
  toggle("format-text-italic", "Italic", false);
  toggle("format-text-underline", "Underline", false);
  format_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  toggle("format-justify-left", "Align Left", true);
  toggle("format-justify-center", "Center", false);
  toggle("format-justify-right", "Align Right", false);
  format_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  format_bar_.append(*hold(style_combo_, 110));
  toggle("format-list-unordered", "Bullets", false);
  toggle("format-list-ordered", "Numbering", false);
}

void MainWindow::build_page()
{
  ruler_.set_size_request(-1, 18);
  ruler_.get_style_context()->add_class("ruler");
  ruler_.signal_draw().connect(sigc::mem_fun(*this, &MainWindow::on_ruler_draw));

  page_.set_size_request(kPageW, kPageH);
  page_.get_style_context()->add_class("page");
  page_.signal_draw().connect(sigc::mem_fun(*this, &MainWindow::on_page_draw));
  page_.add_events(Gdk::BUTTON_PRESS_MASK);
  page_.signal_button_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_context));
  page_.signal_size_allocate().connect([this](Gtk::Allocation&) { ruler_.queue_draw(); });

  board_.get_style_context()->add_class("pasteboard");
  board_.pack_start(page_, Gtk::PACK_SHRINK);
  page_.set_halign(Gtk::ALIGN_CENTER);
  page_.set_valign(Gtk::ALIGN_START);
  page_.set_margin_top(18);
  page_.set_margin_bottom(18);
  board_.signal_button_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_context));
  board_.add_events(Gdk::BUTTON_PRESS_MASK);

  paste_.get_style_context()->add_class("pasteboard");
  paste_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  paste_.add(board_);
  paste_.add_events(Gdk::BUTTON_PRESS_MASK);
  paste_.signal_button_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_context));
  paste_.signal_size_allocate().connect([this](Gtk::Allocation&) {
    if (settings_.zoom == 0)
      apply_page_size();
    ruler_.queue_draw();
  });
}

void MainWindow::build_status()
{
  status_.get_style_context()->add_class("statusbar");
  message_.set_halign(Gtk::ALIGN_START);
  message_.set_hexpand(true);
  page_label_.set_text("Page 1 of 1");
  zoom_cell_.add(zoom_label_);
  zoom_cell_.add_events(Gdk::BUTTON_PRESS_MASK);
  zoom_cell_.signal_button_press_event().connect([this](GdkEventButton* event) {
    if (event->type != GDK_BUTTON_PRESS || event->button != 1)
      return false;
    zoom_popup_.popup_at_widget(&zoom_cell_, Gdk::GRAVITY_NORTH_EAST, Gdk::GRAVITY_SOUTH_EAST,
                                reinterpret_cast<GdkEvent*>(event));
    return true;
  });
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
    return;
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
  int width = kPageW;
  int height = kPageH;
  if (settings_.zoom == 0) {
    width = std::max(120, paste_.get_allocated_width() - 36);
    height = std::max(80, width * kPageH / kPageW);
  } else {
    width = std::max(1, kPageW * settings_.zoom / 100);
    height = std::max(1, kPageH * settings_.zoom / 100);
  }
  int current_w = 0;
  int current_h = 0;
  page_.get_size_request(current_w, current_h);
  if (current_w == width && current_h == height)
    return;
  page_.set_size_request(width, height);
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

bool MainWindow::on_page_draw(const Cairo::RefPtr<Cairo::Context>& cr)
{
  const Gtk::Allocation area = page_.get_allocation();
  cr->set_source_rgb(1, 1, 1);
  cr->rectangle(0, 0, area.get_width(), area.get_height());
  cr->fill();
  cr->set_source_rgb(0.36, 0.36, 0.36);
  cr->rectangle(0.5, 0.5, area.get_width() - 1, area.get_height() - 1);
  cr->set_line_width(1);
  cr->stroke();
  return true;
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
  return true;
}

bool MainWindow::on_context(GdkEventButton* event)
{
  if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
    context_.popup_at_pointer(reinterpret_cast<GdkEvent*>(event));
    return true;
  }
  return false;
}

}  // namespace writeit
