/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "document.hpp"
#include "settings.hpp"

#include <gtkmm.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace writeit {

class MainWindow : public Gtk::ApplicationWindow {
 public:
  MainWindow();

 protected:
  bool on_delete_event(GdkEventAny* event) override;

 private:
  struct Snapshot {
    Document doc;
    int offset = 0;
  };

  enum class OpenKind { Rtf, Markdown, Plain };

  static constexpr int kPageW = 540;
  static constexpr int kPageH = 470;
  static constexpr int kUndoCap = 200;

  void build_menus();
  void build_toolbars();
  void build_page();
  void build_status();
  void build_editor();
  void load_css();
  void apply_chrome();
  void apply_toolbar_row();
  void apply_page_size();
  void set_zoom(int zoom);
  void sync_zoom_checks();
  void on_about();
  bool on_ruler_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_context(GdkEventButton* event);

  Gtk::MenuItem* add_item(Gtk::Menu& menu, const char* label, bool sensitive, guint key = 0,
                          Gdk::ModifierType mods = static_cast<Gdk::ModifierType>(0));
  Gtk::ToolButton* add_tool(Gtk::Toolbar& bar, const char* icon, const char* tip, bool sensitive);

  void connect_format();
  void fill_font_combo(Gtk::ComboBoxText& combo, const std::string& active);
  bool new_document(bool prompt);
  void open_document();
  void open_path(const std::string& path, OpenKind fallback);
  bool save_document();
  bool save_document_as();
  void export_markdown();
  bool confirm_discard_or_save();
  void close_document();
  void remember_path(const std::string& path);
  void rebuild_recent();
  void install_loaded(const Document& doc, const std::string& path, bool keep_path);
  bool write_rtf(const std::string& path);

  Document capture() const;
  void replace_buffer(const Document& doc, int offset);
  bool dirty() const;
  void update_title();
  void update_actions();
  int cursor_offset() const;
  void update_caret_font();

  void undo();
  void redo();
  void on_user_begin();
  void on_user_end();
  void coalesce_typing(const Document& current);
  void on_inserted(const Gtk::TextBuffer::iterator& pos, const Glib::ustring& text, int bytes);
  void on_mark_set(const Gtk::TextBuffer::iterator& location,
                   const Glib::RefPtr<Gtk::TextBuffer::Mark>& mark);
  void finish_pending();
  void note_insert(int start, int end);

  Glib::RefPtr<Gtk::TextTag> format_tag(const Run& run);
  Glib::RefPtr<Gtk::TextTag> heading_tag(int level);
  void raise_headings();
  void restyle_tags();
  void apply_margins();
  double zoom_factor() const;
  Run format_of(const Gtk::TextIter& iter) const;
  int heading_of(const Gtk::TextIter& iter) const;
  int heading_near(int offset) const;
  bool has_fmt(const Gtk::TextIter& iter) const;
  void strip_fmt(const Gtk::TextIter& from, const Gtk::TextIter& to);
  Run line_break_mark(int newline) const;
  void tag_line_breaks(int start, int end);
  void apply_run_edit(const std::function<void(Run&)>& edit);
  void toggle_flag(bool Run::* flag);
  void show_format(const Run& run);
  void sync_format_controls();
  void on_font_changed();
  void on_size_changed();

  void build_find();
  void present_find(bool replace);
  void find_next();
  void replace_once();
  void on_options();
  void tell(const char* sentence);

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
  Gtk::EventBox page_;
  Gtk::TextView text_;
  Glib::RefPtr<Gtk::TextBuffer> buffer_;
  Gtk::Box status_{Gtk::ORIENTATION_HORIZONTAL};
  Gtk::Label message_;
  Gtk::Label page_label_;
  Gtk::EventBox zoom_cell_;
  Gtk::Label zoom_label_;
  Gtk::Menu zoom_menu_;
  Gtk::Menu zoom_popup_;
  Gtk::Menu context_;
  Gtk::Menu recent_menu_;
  std::array<Gtk::RadioMenuItem*, 6> zoom_view_{};
  std::array<Gtk::RadioMenuItem*, 6> zoom_status_{};

  Gtk::MenuItem* new_item_ = nullptr;
  Gtk::MenuItem* open_item_ = nullptr;
  Gtk::MenuItem* recent_item_ = nullptr;
  Gtk::MenuItem* save_item_ = nullptr;
  Gtk::MenuItem* save_as_item_ = nullptr;
  Gtk::MenuItem* export_item_ = nullptr;
  Gtk::MenuItem* close_item_ = nullptr;
  Gtk::MenuItem* undo_item_ = nullptr;
  Gtk::MenuItem* redo_item_ = nullptr;
  Gtk::MenuItem* cut_item_ = nullptr;
  Gtk::MenuItem* copy_item_ = nullptr;
  Gtk::MenuItem* paste_item_ = nullptr;
  Gtk::MenuItem* delete_item_ = nullptr;
  Gtk::MenuItem* select_all_item_ = nullptr;
  Gtk::MenuItem* find_item_ = nullptr;
  Gtk::MenuItem* replace_item_ = nullptr;
  Gtk::MenuItem* bold_item_ = nullptr;
  Gtk::MenuItem* italic_item_ = nullptr;
  Gtk::MenuItem* underline_item_ = nullptr;
  Gtk::MenuItem* options_item_ = nullptr;
  Gtk::MenuItem* context_cut_ = nullptr;
  Gtk::MenuItem* context_copy_ = nullptr;
  Gtk::MenuItem* context_paste_ = nullptr;
  std::vector<Gtk::MenuItem*> recent_items_;

  Gtk::ToolButton* new_tool_ = nullptr;
  Gtk::ToolButton* open_tool_ = nullptr;
  Gtk::ToolButton* save_tool_ = nullptr;
  Gtk::ToolButton* cut_tool_ = nullptr;
  Gtk::ToolButton* copy_tool_ = nullptr;
  Gtk::ToolButton* paste_tool_ = nullptr;
  Gtk::ToolButton* undo_tool_ = nullptr;
  Gtk::ToolButton* redo_tool_ = nullptr;
  Gtk::ToggleToolButton* bold_toggle_ = nullptr;
  Gtk::ToggleToolButton* italic_toggle_ = nullptr;
  Gtk::ToggleToolButton* underline_toggle_ = nullptr;

  std::string save_path_;
  std::string title_name_ = "Untitled";
  Document saved_;
  bool save_point_ = true;
  bool loading_ = false;
  bool restoring_ = false;
  bool suppress_format_ = false;
  bool in_user_ = false;
  bool pending_insert_ = false;
  bool sizing_ = false;
  double styled_zoom_ = -1;
  Run typing_;
  std::string caret_key_;
  std::vector<Snapshot> undo_;
  std::vector<Snapshot> redo_;
  gint64 last_typed_us_ = 0;
  Glib::RefPtr<Gtk::TextMark> insert_start_;
  Glib::RefPtr<Gtk::TextMark> insert_end_;

  std::unique_ptr<Gtk::Dialog> find_dialog_;
  Gtk::Entry* find_entry_ = nullptr;
  Gtk::Entry* replace_entry_ = nullptr;
  Gtk::CheckButton* match_case_ = nullptr;
};

}  // namespace writeit
