/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "settings.hpp"

#include <gtkmm.h>

#include <array>

namespace writeit {

class MainWindow : public Gtk::ApplicationWindow {
 public:
  MainWindow();

 protected:
  bool on_delete_event(GdkEventAny* event) override;

 private:
  void build_menus();
  void build_toolbars();
  void build_page();
  void build_status();
  void load_css();
  void apply_chrome();
  void apply_toolbar_row();
  void apply_page_size();
  void set_zoom(int zoom);
  void sync_zoom_checks();
  void on_about();
  bool on_ruler_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_page_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_context(GdkEventButton* event);

  Gtk::MenuItem* add_item(Gtk::Menu& menu, const char* label, bool sensitive, guint key = 0,
                          Gdk::ModifierType mods = static_cast<Gdk::ModifierType>(0));
  Gtk::ToolButton* add_tool(Gtk::Toolbar& bar, const char* icon, const char* tip, bool sensitive);

  Settings settings_;
  Glib::RefPtr<Gtk::AccelGroup> accel_;
  bool suppress_zoom_ = false;

  Gtk::Box root_{Gtk::ORIENTATION_VERTICAL};
  Gtk::MenuBar menu_bar_;
  Gtk::Box toolbars_{Gtk::ORIENTATION_HORIZONTAL};
  Gtk::Toolbar standard_bar_;
  Gtk::Toolbar format_bar_;
  Gtk::ComboBoxText font_combo_;
  Gtk::ComboBoxText size_combo_;
  Gtk::ComboBoxText style_combo_;
  Gtk::DrawingArea ruler_;
  Gtk::ScrolledWindow paste_;
  Gtk::Box board_{Gtk::ORIENTATION_VERTICAL};
  Gtk::DrawingArea page_;
  Gtk::Box status_{Gtk::ORIENTATION_HORIZONTAL};
  Gtk::Label message_;
  Gtk::Label page_label_;
  Gtk::EventBox zoom_cell_;
  Gtk::Label zoom_label_;
  Gtk::Menu zoom_menu_;
  Gtk::Menu zoom_popup_;
  Gtk::Menu context_;
  std::array<Gtk::RadioMenuItem*, 6> zoom_view_{};
  std::array<Gtk::RadioMenuItem*, 6> zoom_status_{};
};

}  // namespace writeit
