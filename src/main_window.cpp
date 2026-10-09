/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"

#include "about_dialog.hpp"
#include "config.hpp"

#include <glibmm/miscutils.h>

#include <algorithm>

namespace writeit {
namespace {

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

// A missing icon falls back to the menu's word. Toolbar buttons share one
// width, the widest's, so a word must not widen every icon on the bar.
void fallback_label(Gtk::ToolButton& button, const char* word)
{
  button.set_label(word);
  button.set_homogeneous(false);
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
  format_menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item(*format_menu, "_Style…", false);
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
  context_.show_all();
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
  toolbars_.pack_start(standard_bar_, Gtk::PACK_SHRINK);
  toolbars_.pack_start(format_bar_, Gtk::PACK_SHRINK);

  new_tool_ = add_tool(standard_bar_, "document-new", "New", true);
  open_tool_ = add_tool(standard_bar_, "document-open", "Open", true);
  save_tool_ = add_tool(standard_bar_, "document-save", "Save", true);
  standard_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  add_tool(standard_bar_, "document-print", "Print", false);
  standard_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  cut_tool_ = add_tool(standard_bar_, "edit-cut", "Cut", false);
  copy_tool_ = add_tool(standard_bar_, "edit-copy", "Copy", false);
  paste_tool_ = add_tool(standard_bar_, "edit-paste", "Paste", false);
  standard_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  undo_tool_ = add_tool(standard_bar_, "edit-undo", "Undo", false);
  redo_tool_ = add_tool(standard_bar_, "edit-redo", "Redo", false);

  font_combo_.set_size_request(128, -1);
  font_combo_.set_tooltip_text("Font");
  size_combo_.set_size_request(52, -1);
  size_combo_.set_tooltip_text("Size");
  for (const char* size : {"8", "9", "10", "11", "12", "14", "16", "18", "24", "36"})
    size_combo_.append(size);
  size_combo_.set_active_text("11");
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
  format_bar_.append(*Gtk::manage(new Gtk::SeparatorToolItem()));
  format_bar_.append(*hold(style_combo_, 110));
  bullets_toggle_ = toggle("format-list-unordered", "Bullets", false, true);
  numbering_toggle_ = toggle("format-list-ordered", "Numbering", false, true);
}

void MainWindow::build_page()
{
  ruler_.set_size_request(-1, 18);
  ruler_.get_style_context()->add_class("ruler");
  ruler_.signal_draw().connect(sigc::mem_fun(*this, &MainWindow::on_ruler_draw));

  page_.set_size_request(kScreenPageWidth, kScreenPageHeight);
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
    if (settings_.zoom == 0 || view_ == ViewMode::Draft)
      apply_page_size();
    ruler_.queue_draw();
  });
}

void MainWindow::build_status()
{
  status_.get_style_context()->add_class("statusbar");
  message_.set_halign(Gtk::ALIGN_START);
  message_.set_hexpand(true);
  message_.set_ellipsize(Pango::ELLIPSIZE_END);
  page_label_.set_text("Page 1 of 1");
  zoom_cell_.add(zoom_label_);
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
  const ViewGeometry g = geometry();
  const int width = g.page_width;
  // Draft's white area is at least as tall as the visible pasteboard, so
  // the text starts at the top and there is no gray below it.
  const int base_h = g.chrome ? g.page_height : std::max(1, paste_.get_allocated_height() - 4);
  apply_margins();
  // Paragraph tags carry the text inset in their margins, so they are
  // restyled when the zoom or the view changes it.
  if (z != styled_zoom_ || view_ != styled_view_) {
    styled_zoom_ = z;
    styled_view_ = view_;
    restyle_tags();
  }
  int min_h = 0;
  int nat_h = 0;
  text_.get_preferred_height_for_width(std::max(1, width - (g.chrome ? 2 : 0)), min_h, nat_h);
  const int height = std::max(base_h, nat_h);
  int current_w = 0;
  int current_h = 0;
  page_.get_size_request(current_w, current_h);
  if (current_w != width || current_h != height)
    page_.set_size_request(width, height);
  sizing_ = false;
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
  context_.popup_at_pointer(reinterpret_cast<GdkEvent*>(event));
  return true;
}

}  // namespace writeit
