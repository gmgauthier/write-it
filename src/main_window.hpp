/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "document.hpp"
#include "narrow_combo.hpp"
#include "page_text.hpp"
#include "settings.hpp"
#include "view.hpp"

#include <gtkmm.h>

#include <array>
#include <functional>
#include <utility>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace writeit {

// What a paragraph tag carries: the paragraph's indents, alignment, list,
// and style.
struct ParaFormat {
  Indents indents;
  Align align = Align::Left;
  ListFormat list;
  std::string style = kNormalStyle;
  // What of it was set directly (Paragraph::direct).
  unsigned direct = 0;
};

inline bool operator==(const ParaFormat& a, const ParaFormat& b)
{
  return a.indents == b.indents && a.align == b.align && a.list == b.list && a.style == b.style &&
         a.direct == b.direct;
}

inline ParaFormat para_format(const Paragraph& paragraph)
{
  return ParaFormat{paragraph.indents, paragraph.align, paragraph.list, paragraph.style,
                    paragraph.direct};
}

class MainWindow : public Gtk::ApplicationWindow {
 public:
  MainWindow();
  ~MainWindow() override;

  // For files handed to the program (see open_plan.hpp). open_file is
  // File > Open's route: RTF, Markdown or plain text by extension, recent
  // files, and the same error sentences. True when the file was loaded.
  bool open_file(const std::string& path);
  // Untitled, never edited: safe to load a file into without asking.
  bool pristine() const;
  // The RTF file this window saves to, or "".
  const std::string& document_path() const
  {
    return save_path_;
  }
  // The .md or .txt this window's document was imported from, or "".
  const std::string& import_source() const
  {
    return source_path_;
  }
  // A file with no local path, such as an sftp:// URI that is not mounted.
  void refuse_not_local(const std::string& uri);
  // Asked by File > Open and Open Recent before loading: if a window
  // already holds that file (by same_file), bring it forward and return
  // true, and nothing is loaded here. Set by the application.
  void set_open_elsewhere(std::function<bool(const std::string&)> open_elsewhere)
  {
    open_elsewhere_ = std::move(open_elsewhere);
  }

 protected:
  bool on_delete_event(GdkEventAny* event) override;

 private:
  // The window tests in tests/ drive the real window and read back what GTK
  // made of it.
  friend struct MainWindowProbe;

  struct Snapshot {
    Document doc;
    int offset = 0;
  };

  enum class OpenKind { Rtf, Markdown, Plain };

  static constexpr int kPageW = kScreenPageWidth;
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
  // apply_page_size() once the current layout is done.
  void queue_page_size();
  void set_zoom(int zoom);
  // The text view sits on the page inside the pasteboard's scroller, so
  // GTK's own scroll-to-caret has nothing to scroll. These scroll the
  // pasteboard instead: follow_caret() after the caret moves or the text
  // changes, scroll_to_caret() again whenever the layout settles, until the
  // wheel or a scrollbar takes the view somewhere else.
  void follow_caret();
  void scroll_to_caret();
  // Page Down and Page Up, with or without Shift. For the same reason GTK
  // would take the whole buffer as one page; these move the caret, and the
  // pasteboard, by the pasteboard's visible height and keep the caret's x.
  static void on_move_cursor(GtkTextView* view, GtkMovementStep step, gint count, gboolean extend,
                             gpointer self);
  bool page_caret(int count, bool extend);
  void sync_zoom_checks();
  void queue_page_status();
  void update_page_status();
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
  bool open_path(const std::string& path, OpenKind fallback);
  bool save_document();
  bool save_document_as();
  void export_markdown();
  bool confirm_discard_or_save();
  void close_document();
  void remember_path(const std::string& path);
  // Takes Open Recent from the ini: every window writes the whole file, so
  // a window reads the list fresh before it writes it.
  void reload_recent();
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
  ViewGeometry geometry() const;
  void set_view(ViewMode mode);
  int margin_left() const;
  int margin_right() const;
  int indent_px(int twips) const;
  double zoom_factor() const;
  // What apply_page_size() sizes the page's height to now, and whether the
  // page's size request differs from it.
  int page_height() const;
  bool page_height_stale() const;
  Run format_of(const Gtk::TextIter& iter) const;
  int heading_of(const Gtk::TextIter& iter) const;
  int heading_near(int offset) const;
  bool has_fmt(const Gtk::TextIter& iter) const;
  void strip_fmt(const Gtk::TextIter& from, const Gtk::TextIter& to);
  Run line_break_mark(int newline) const;
  void tag_line_breaks(int start, int end);
  void apply_run_edit(const std::function<void(Run&)>& edit);
  enum class TextFlag { Bold, Italic, Underline };
  static bool text_flag(const Run& run, TextFlag flag);
  static void set_text_flag(Run& run, TextFlag flag, bool on);
  void toggle_flag(TextFlag flag);
  void show_format(const Run& run);
  void sync_format_controls();
  void on_font_changed();
  void on_size_changed();

  Glib::RefPtr<Gtk::TextTag> para_tag(const ParaFormat& format);
  void style_para_tag(const Glib::RefPtr<Gtk::TextTag>& tag, const ParaFormat& format) const;
  Glib::RefPtr<Gtk::TextTag> para_tag_at(Gtk::TextIter iter) const;
  ParaFormat para_at(int offset) const;
  Indents indents_at(int offset) const;
  ParaFormat destination_para(int start, int end) const;
  int paragraph_start(int offset) const;
  int paragraph_end(int offset) const;
  bool final_paragraph_empty() const;
  void normalise_paragraphs();
  void on_erase(const Gtk::TextBuffer::iterator& from, const Gtk::TextBuffer::iterator& to);
  // Edits the format of every paragraph the selection touches, each from
  // its own current format.
  void apply_para_edit(const std::function<void(ParaFormat&)>& edit);
  // The same over all of them at once, for edits such as Bullets that look
  // at the whole selection.
  void apply_paragraphs(const std::function<void(std::vector<Paragraph>&)>& edit);
  void apply_align(Align align);
  void on_align_toggled(Align align);
  void show_align();
  void on_paragraph();
  // Named styles (styles.cpp).
  std::vector<Style> sheet() const;
  void fill_style_combo();
  void show_style();
  void on_style_chosen();
  void apply_named_style(const std::string& name);
  void apply_next_style();
  void on_style_dialog();
  int paragraph_index(int offset) const;
  // One undo step from `before` to `after`, keeping the selection.
  void commit_document(const Document& before, const Document& after);
  void toggle_list_kind(ListKind kind);
  // Tags the paragraph that starts at `start` with `format`, or holds the
  // format aside for the empty last paragraph.
  void tag_paragraph(int start, const ParaFormat& format);
  // The caret's paragraph, counted in '\n' as capture() counts them.
  size_t caret_paragraph() const;
  // Restart Numbering (true) or Continue Previous List (false) at the
  // caret's item, as one undo step. False when it changes nothing.
  bool renumber_list(bool restart);
  // Shows the right-click menu's numbering items on a numbered item.
  void sync_context_numbering();
  bool shift_list_level(int delta);
  bool on_text_key(GdkEventKey* event);
  bool on_text_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  // A list item's label, for the paragraph that starts at buffer `offset`:
  // its layout, its buffer x, and the first character's location. False for
  // a plain paragraph.
  bool list_label_place(const Paragraph& paragraph, int offset, int number,
                        Glib::RefPtr<Pango::Layout>& layout, int& x, Gdk::Rectangle& where);
  // The label's layout and pixel width, and a space's width in its font.
  Glib::RefPtr<Pango::Layout> list_label_layout(const Paragraph& paragraph, int offset, int number,
                                                int& width, int& gap);
  // A centred list item whose text must be centred from somewhere other
  // than its left indent (list_centre_from()) gets a screen-only
  // "list-shift" tag carrying that left margin, one tag per margin in use,
  // so label and first line centre as one unit. Recomputed in an idle after
  // any text or format change, renumbering, zoom, or view switch; capture()
  // and so RTF, undo, the dirty check, and document comparison never see it.
  void queue_list_shifts();
  void update_list_shifts();
  Glib::RefPtr<Gtk::TextTag> list_shift_tag(int left_margin);
  // Word 97 moves a left-aligned or justified list item's text to the next
  // default tab stop when its label reaches where the text starts. On screen
  // only, through "list-tab" tags the document never sees; brought up to
  // date in an idle after edits, formatting, zoom, and the view.
  void queue_list_tabs();
  // Notes that [start, end) changed, for the next update_list_tabs().
  void note_list_tabs(const Gtk::TextIter& start, const Gtk::TextIter& end);
  Glib::RefPtr<Gtk::TextTag> list_tab_tag(int indent);
  void update_list_tabs();
  // Tags paragraph `index` of tab_lines_, from buffer offset `start` to
  // `end`, among the existing list-tab tags `old`.
  Glib::RefPtr<Gtk::TextTag> retab_paragraph(size_t index, int start, int end,
                                             const std::vector<Glib::RefPtr<Gtk::TextTag>>& old);
  void sync_list_controls();

  void build_find();
  void present_find(bool replace);
  void find_next();
  void replace_once();
  void on_options();
  void tell(const std::string& sentence);

  Settings settings_;
  // The size, while not maximised, and the maximised state, saved on close.
  WindowMemory window_memory_;
  Glib::RefPtr<Gtk::AccelGroup> accel_;
  bool suppress_zoom_ = false;

  Gtk::Box root_{Gtk::ORIENTATION_VERTICAL};
  Gtk::MenuBar menu_bar_;
  Gtk::Box toolbars_{Gtk::ORIENTATION_HORIZONTAL};
  Gtk::Toolbar standard_bar_;
  Gtk::Toolbar format_bar_;
  NarrowCombo font_combo_{128};
  NarrowCombo size_combo_{52};
  // What size_combo_ lists now (size_choices()).
  std::vector<int> size_choices_shown_;
  NarrowCombo style_combo_{110};
  Gtk::DrawingArea ruler_;
  Gtk::ScrolledWindow paste_;
  Gtk::Box board_{Gtk::ORIENTATION_VERTICAL};
  Gtk::EventBox page_;
  PageText text_;
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
  Gtk::MenuItem* bullets_item_ = nullptr;
  Gtk::MenuItem* numbering_item_ = nullptr;
  Gtk::MenuItem* paragraph_item_ = nullptr;
  Gtk::MenuItem* style_item_ = nullptr;
  Gtk::MenuItem* align_left_item_ = nullptr;
  Gtk::MenuItem* align_center_item_ = nullptr;
  Gtk::MenuItem* align_right_item_ = nullptr;
  Gtk::MenuItem* justify_item_ = nullptr;
  Gtk::MenuItem* options_item_ = nullptr;
  Gtk::MenuItem* context_cut_ = nullptr;
  Gtk::MenuItem* context_copy_ = nullptr;
  Gtk::MenuItem* context_paste_ = nullptr;
  Gtk::SeparatorMenuItem* context_numbering_rule_ = nullptr;
  Gtk::MenuItem* context_restart_ = nullptr;
  Gtk::MenuItem* context_continue_ = nullptr;
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
  Gtk::ToggleToolButton* align_left_toggle_ = nullptr;
  Gtk::ToggleToolButton* align_center_toggle_ = nullptr;
  Gtk::ToggleToolButton* align_right_toggle_ = nullptr;
  Gtk::ToggleToolButton* justify_toggle_ = nullptr;
  Gtk::ToggleToolButton* bullets_toggle_ = nullptr;
  Gtk::ToggleToolButton* numbering_toggle_ = nullptr;

  std::string save_path_;
  std::string title_name_ = "Untitled";
  Document saved_;
  bool save_point_ = true;
  // Where an imported document came from; cleared when it becomes anything
  // else (New, Close, Save As). Lets a second request for it find this
  // window, since an import keeps no save path.
  std::string source_path_;
  std::function<bool(const std::string&)> open_elsewhere_;
  bool loading_ = false;
  bool restoring_ = false;
  bool suppress_format_ = false;
  bool in_user_ = false;
  bool pending_insert_ = false;
  bool sizing_ = false;
  // The idle that sizes the page again after a layout: to a new pasteboard
  // size (Fit width, Draft) or to text that rewrapped to another height.
  // One at a time, and gone with the window.
  sigc::connection page_idle_;
  // The text wants more height than the page gave it; the idle resizes.
  bool grow_page_ = false;
  bool follow_caret_ = false;
  // The idle follow_caret() queues; one at a time, and gone with the window.
  sigc::connection caret_idle_;
  // The status bar's page count idle, likewise.
  sigc::connection page_status_idle_;
  bool list_shifts_queued_ = false;
  bool shifting_ = false;
  sigc::connection list_shifts_idle_;
  // The list-tab idle, likewise; tabbing_ while it retags.
  sigc::connection list_tabs_idle_;
  bool tabbing_ = false;
  // Paragraphs update_list_tabs() has looked at, for the tests.
  long list_tabs_evaluated_ = 0;
  // Calls of update_list_tabs() and update_list_shifts(), for the tests:
  // none while nothing changes.
  long list_updates_ = 0;
  // What update_list_tabs() last saw of each paragraph: its format and its
  // list number. Typing changes neither, so only the typed-in paragraphs
  // are looked at again; a new or removed paragraph, or a paragraph format
  // change, renumbers (a walk of the paragraph tags, not a capture), and
  // zoom, the view, the margins or a font change look at every paragraph.
  struct TabLine {
    ParaFormat format;
    int number = 0;
  };
  std::vector<TabLine> tab_lines_;
  bool tabs_full_ = true;
  bool tabs_renumber_ = false;
  bool tabs_noted_ = false;
  Glib::RefPtr<Gtk::TextMark> tabs_from_;
  Glib::RefPtr<Gtk::TextMark> tabs_to_;
  // Label widths by font, size and text, at the current zoom.
  std::map<std::string, int> tab_widths_;
  // Paste's sensitivity follows the clipboard, which outlives the window.
  sigc::connection clipboard_owner_;
  double styled_zoom_ = -1;
  // View > Page / Draft. Not saved: every launch opens in Page.
  ViewMode view_ = kDefaultView;
  ViewMode styled_view_ = kDefaultView;
  Gtk::RadioMenuItem* page_item_ = nullptr;
  Gtk::RadioMenuItem* draft_item_ = nullptr;
  Run typing_;
  // The format of a last paragraph with no characters, which no tag can hold.
  ParaFormat pending_para_;
  bool pending_para_set_ = false;
  std::string caret_key_;
  std::vector<Snapshot> undo_;
  std::vector<Snapshot> redo_;
  gint64 last_typed_us_ = 0;
  // The document's style sheet; empty for the default, as in Document.
  std::vector<Style> styles_;
  // Enter at the end of a paragraph: the new one takes the next style.
  bool next_style_pending_ = false;
  // apply_align() is under way: the alignment was chosen, so it is direct.
  bool chose_align_ = false;
  int next_style_from_ = -1;
  Glib::RefPtr<Gtk::TextMark> insert_start_;
  Glib::RefPtr<Gtk::TextMark> insert_end_;

  std::unique_ptr<Gtk::Dialog> find_dialog_;
  Gtk::Entry* find_entry_ = nullptr;
  Gtk::Entry* replace_entry_ = nullptr;
  Gtk::CheckButton* match_case_ = nullptr;
};

}  // namespace writeit
