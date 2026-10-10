/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "document.hpp"
#include "narrow_combo.hpp"
#include "page_text.hpp"
#include "settings.hpp"
#include "undo.hpp"
#include "view.hpp"

#include <gtkmm.h>

#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace writeit {

class TableSheet;

// What a paragraph tag carries: the paragraph's indents, alignment, list,
// and style.
struct ParaFormat {
  Indents indents;
  Align align = Align::Left;
  ListFormat list;
  std::string style = kNormalStyle;
  // What of it was set directly (Paragraph::direct).
  unsigned direct = 0;
  // A page break before this paragraph, and its table cell. Both zero on a
  // paragraph tag written the old way, so a tag from before this slice still
  // parses.
  bool page_break = false;
  Cell cell;
};

inline bool operator==(const ParaFormat& a, const ParaFormat& b)
{
  return a.indents == b.indents && a.align == b.align && a.list == b.list && a.style == b.style &&
         a.direct == b.direct && a.page_break == b.page_break && a.cell == b.cell;
}

inline ParaFormat para_format(const Paragraph& paragraph)
{
  ParaFormat format;
  format.indents = paragraph.indents;
  format.align = paragraph.align;
  format.list = paragraph.list;
  format.style = paragraph.style;
  format.direct = paragraph.direct;
  format.page_break = paragraph.page_break;
  format.cell = paragraph.cell;
  return format;
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
  // A field with the keyboard (the size box) gets its editing keys before
  // the window's accelerators, so Delete, Ctrl+A or Ctrl+Z typed there edit
  // the field and never the document.
  bool on_key_press_event(GdkEventKey* event) override;

 private:
  // The window tests in tests/ drive the real window and read back what GTK
  // made of it.
  friend struct MainWindowProbe;
  friend class TableSheet;

  // What an undo step keeps of the state the buffer cannot hold: the empty
  // last paragraph's format and character format.
  struct SideState {
    ParaFormat pending_para;
    bool pending_para_set = false;
    Run pending_mark;
    bool pending_mark_set = false;
  };

  enum class OpenKind { Rtf, Markdown, Plain };

  static constexpr int kPageW = kScreenPageWidth;

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
  // The current state is the saved one now.
  void mark_saved();

  Document capture() const;
  // Calls of capture(), for the tests: a keystroke makes at most undo's two.
  mutable long captures_ = 0;
  // For the tests: label geometry read through get_iter_location() (which
  // lays its line out again), and label layouts built, in list drawing and
  // list measuring. A keystroke costs a few of each, however many items show.
  long label_locates_ = 0;
  long label_layouts_ = 0;
  void replace_buffer(const Document& doc, int offset);
  // The undo state differs from the one saved, opened or made new.
  bool dirty() const;
  void update_title();
  void update_actions();
  // Asks the clipboard, without waiting, whether it holds text; the answer
  // sets clipboard_text_ and Paste.
  void refresh_clipboard();
  void apply_paste();
  int cursor_offset() const;
  void update_caret_font();

  void undo();
  void redo();
  void on_user_begin();
  void on_user_end();
  // Opens and closes an undo step around a user action or a command; the
  // buffer's own signals record what it changes (undo.hpp).
  void open_step();
  UndoHistory::Closed close_step(bool may_merge);
  // After undo or redo: the caret, and everything that follows the text.
  void after_replay(int caret);
  SideState side_state() const;
  void set_side_state(const SideState& state);
  // Whether the document is the same with either side state (close_step()).
  bool pending_same(const SideState& before, const SideState& after) const;
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
  // The only way a character format goes on: every other fmt tag on the
  // range comes off first, so no character ever carries two
  // (first_doubled_format_tag() in format_tags.hpp checks it).
  void set_fmt(const Gtk::TextIter& from, const Gtk::TextIter& to,
               const Glib::RefPtr<Gtk::TextTag>& tag);
  void on_tag_applied(const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& from,
                      const Gtk::TextIter& to);
  void clear_applied_fmt();
  // The start-end choice for a paste (see finish_pending()): which format the
  // paragraph a paste's first end closes takes, the copied paragraph's
  // (Word's paragraph mark) or the landing paragraph's.
  Glib::RefPtr<Gtk::TextTag> first_end_para(const Glib::RefPtr<Gtk::TextTag>& copied,
                                            const Glib::RefPtr<Gtk::TextTag>& landing) const;
  // The only way finish_pending() sets a paragraph format: every other para
  // tag on [from, to) comes off first.
  void set_para(int from, int to, const Glib::RefPtr<Gtk::TextTag>& tag);
  // [from, to)'s text takes outline level `level` (0 for none) and no other.
  void set_heading(int from, int to, int level);
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
  // Enter in the size box: the typed size, or Word 97's message saying why
  // not, then back to the text's size with the box still to type in.
  void on_size_entered();
  // Shows `size` in the size box, and highlights it in its list.
  void show_size(double size);
  // The keyboard moved. While the size box has it, the document's
  // selection is kept (lend_primary()).
  void on_focus_moved();
  // GTK collapses a text buffer's selection when another widget takes the
  // X PRIMARY selection, as the size box's entry does when its text is
  // selected. While the box has the keyboard the buffer stops tracking
  // PRIMARY, so the document's selection stays for Enter to size, as in
  // Word; it takes PRIMARY back when the box lets go.
  void lend_primary(bool lend);

  Glib::RefPtr<Gtk::TextTag> para_tag(const ParaFormat& format);
  void style_para_tag(const Glib::RefPtr<Gtk::TextTag>& tag, const ParaFormat& format) const;
  Glib::RefPtr<Gtk::TextTag> para_tag_at(Gtk::TextIter iter) const;
  ParaFormat para_at(int offset) const;
  Indents indents_at(int offset) const;
  ParaFormat destination_para(int start, int end) const;
  int paragraph_start(int offset) const;
  int paragraph_end(int offset) const;
  bool final_paragraph_empty() const;
  // Gives each paragraph the user action touched one paragraph tag over all
  // of it; the rest are as the last action left them.
  void normalise_paragraphs();
  // Widens the range normalise_paragraphs() looks at to [from, to).
  void note_touched(int from, int to);
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
  // One undo step from `before` to `after`. The selection stays, unless
  // `caret` is given: then the caret is that offset (a page break or a
  // footnote lands in the new place).
  void commit_document(const Document& before, const Document& after, int caret = -1);
  // Paper, header, footer, and notes. The buffer holds none of them.
  void apply_document_page(PageSetup page);
  void install_stories(const Document& doc);
  void refresh_stories();
  void on_page_setup();
  void on_columns();
  void on_page_break();
  void on_insert_table();
  void on_insert_row();
  void on_insert_column();
  void on_delete_row();
  void on_delete_column();
  void on_footnote();
  void on_picture();
  // A table is one child widget in the body, not a paragraph per cell.
  // The newline after that widget, when another paragraph follows, is only
  // a separator: capture() does not make a paragraph out of it.
  void mount_table(const std::vector<Paragraph>& cells, bool separator);
  void append_table(const Glib::RefPtr<Gtk::TextChildAnchor>& anchor,
                    std::vector<Paragraph>& out) const;
  int table_paragraph_count(const Glib::RefPtr<Gtk::TextChildAnchor>& anchor) const;
  TableSheet* focused_sheet() const;
  TableSheet* sheet_at_cursor() const;
  void relayout_tables();
  void fill_cell(Gtk::TextView& view, const std::vector<Paragraph>& paragraphs);
  std::vector<Paragraph> read_cell(const Gtk::TextView& view,
                                   const std::vector<Paragraph>& forms) const;
  void restyle_buffer(const Glib::RefPtr<Gtk::TextBuffer>& buffer);
  void on_cell_begin();
  void on_cell_end();
  // Tab in the last cell adds a row, as in Word 97.
  void extend_table(TableSheet& sheet);
  Glib::RefPtr<Gtk::TextTag> format_tag_on(const Glib::RefPtr<Gtk::TextBuffer>& buffer,
                                           const Run& run);
  size_t document_index_at(int offset) const;
  // After a document command, the caret sits on the table's anchor. Move
  // the keyboard into its first cell.
  void focus_table_caret();
  void on_header_footer();
  void on_story_end(std::vector<Paragraph>* story);
  void on_notes_end();
  // Ctrl+Z in a header, footer, or note undoes the document. Other Ctrl keys
  // that would format the body are swallowed; the story is plain text.
  bool on_story_key(GdkEventKey* event);
  Glib::RefPtr<Gdk::Pixbuf> pixbuf_for(const Image& image);
  // commit_document()'s retagging when the text is the same; false if not.
  bool retag_paragraphs(const Document& before, const Document& after);
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
  // Each paragraph's format and list number, as capture() and list_numbers()
  // give them, and the GTK line it starts on, without copying the document:
  // for the labels on_text_draw() draws and update_list_shifts(). Rebuilt
  // from the paragraph tags when a paragraph format changes, a line comes or
  // goes, or the empty last paragraph's held format changes; typing inside a
  // paragraph leaves it as it is.
  struct ListLine {
    ParaFormat format;
    int line = 0;
    int number = 0;
  };
  const std::vector<ListLine>& list_lines();
  // Whether any paragraph is a list item, from the tag table without
  // walking the paragraphs: a paragraph tag with a list that is in the
  // buffer, or a held format with one. A document with no list does no list
  // work on a draw.
  bool any_list() const;
  // The paragraph list_label_layout() measures for paragraph `index` of
  // list_lines(), starting at buffer `offset`: its format, and its first
  // character's format (or its mark's) as the label's.
  Paragraph label_paragraph(size_t index, int offset) const;
  // A list item's label, for the paragraph that starts at buffer `offset`:
  // its layout, its buffer x, and the first character's location. False for
  // a plain paragraph.
  bool list_label_place(const Paragraph& paragraph, int offset, int number,
                        Glib::RefPtr<Pango::Layout>& layout, int& x, Gdk::Rectangle& where);
  // As list_label_place(), with the first character's location `where`
  // already known.
  bool list_label_at(const Paragraph& paragraph, int offset, int number,
                     Glib::RefPtr<Pango::Layout>& layout, int& x, const Gdk::Rectangle& where);
  // The label's layout and pixel width, and a space's width in its font.
  // Layouts are kept by label text and font (label_layout_cache_): drawing
  // one again does not shape it again.
  Glib::RefPtr<Pango::Layout> list_label_layout(const Paragraph& paragraph, int offset, int number,
                                                int& width, int& gap);
  struct LabelLayout {
    Glib::RefPtr<Pango::Layout> layout;
    int width = 0;
    int gap = 0;
  };
  std::map<std::string, LabelLayout> label_layout_cache_;
  // Where the first character of the GTK line at `start` sits, the line's
  // top being `line_y` and its height `line_height` (get_line_yrange(),
  // which lays nothing out). get_iter_location() lays its line out again,
  // since GTK keeps only one laid-out line outside its draw pass, so each
  // line's answer is kept (label_geometry_) relative to the line's top: a
  // line that moves up or down keeps it.
  void label_where(const Gtk::TextIter& start, int line_y, int line_height, Gdk::Rectangle& where);
  struct LabelGeometry {
    int x = 0;
    int dy = 0;
    int width = 0;
    int height = 0;
    int line_height = 0;
  };
  // By GTK line. An edit or a tag change forgets the lines it touches and
  // moves the entries below by the lines it adds or removes; a line whose
  // height changed is measured again; any other change to the layout (the
  // text width, zoom, the left margin, tag priorities, the style) forgets
  // them all.
  std::map<int, LabelGeometry> label_geometry_;
  int label_geometry_width_ = -1;
  int label_geometry_margin_ = -1;
  double label_geometry_zoom_ = 0;
  void forget_label_geometry(int first, int last, int shift);
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
  void on_spelling();
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
  // Editable, as Word 97's: 10.5 can be typed (parse_size()).
  NarrowCombo size_combo_{52, true};
  // What size_combo_ lists now (size_choices()).
  std::vector<double> size_choices_shown_;
  NarrowCombo style_combo_{110};
  Gtk::DrawingArea ruler_;
  Gtk::ScrolledWindow paste_;
  Gtk::Box board_{Gtk::ORIENTATION_VERTICAL};
  Gtk::EventBox page_;
  Gtk::Box page_box_{Gtk::ORIENTATION_VERTICAL};
  Gtk::TextView header_view_;
  PageText text_;
  Gtk::TextView notes_view_;
  Gtk::TextView footer_view_;
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
  Gtk::MenuItem* spelling_item_ = nullptr;
  Gtk::MenuItem* options_item_ = nullptr;
  Gtk::MenuItem* page_setup_item_ = nullptr;
  Gtk::MenuItem* picture_item_ = nullptr;
  Gtk::MenuItem* insert_table_item_ = nullptr;
  Gtk::MenuItem* page_break_item_ = nullptr;
  Gtk::MenuItem* footnote_item_ = nullptr;
  Gtk::MenuItem* columns_item_ = nullptr;
  Gtk::MenuItem* table_insert_item_ = nullptr;
  Gtk::MenuItem* table_row_item_ = nullptr;
  Gtk::MenuItem* table_column_item_ = nullptr;
  Gtk::MenuItem* table_delete_row_item_ = nullptr;
  Gtk::MenuItem* table_delete_column_item_ = nullptr;
  Gtk::CheckMenuItem* header_footer_item_ = nullptr;
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
  // undo_.state_id() at the last save or open (UndoHistory::state_id()).
  std::uint64_t saved_state_ = 0;
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
  // The range the current user action changed (note_touched()).
  Glib::RefPtr<Gtk::TextMark> touched_start_;
  Glib::RefPtr<Gtk::TextMark> touched_end_;
  bool touched_ = false;
  bool normalising_ = false;
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
  // list_lines()' cache: valid until a change it depends on, with the held
  // format of the empty last paragraph it was built with.
  std::vector<ListLine> list_lines_;
  bool list_lines_valid_ = false;
  bool list_lines_any_ = false;
  bool list_lines_centred_ = false;
  bool list_lines_pending_set_ = false;
  ParaFormat list_lines_pending_;
  bool tabs_full_ = true;
  bool tabs_renumber_ = false;
  bool tabs_noted_ = false;
  Glib::RefPtr<Gtk::TextMark> tabs_from_;
  Glib::RefPtr<Gtk::TextMark> tabs_to_;
  // Label widths by font, size and text, at the current zoom.
  std::map<std::string, int> tab_widths_;
  // Paste's sensitivity follows the clipboard, which outlives the window.
  sigc::connection clipboard_owner_;
  // Whether the clipboard held text when it last answered. Never waited
  // for: a wait runs a main loop of its own, which takes the next key press
  // and updates the actions inside themselves until the stack overflows.
  // refresh_clipboard() asks, and the answer sets this through the loop.
  bool clipboard_text_ = false;
  // Lives as long as the window. An answer from the clipboard can arrive
  // after the window has closed; it holds this weakly and finds it gone.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  // update_actions() is running: it never runs inside itself.
  bool updating_actions_ = false;
  // How deep update_actions() is right now, and the deepest it has been:
  // the window tests read these to see that it never runs inside itself.
  int actions_depth_ = 0;
  int actions_depth_peak_ = 0;
  // The buffer's mark-set. A window closed with text selected gives up the
  // selection as its text view unrealizes, which moves the marks after the
  // menus are gone: the destructor cuts it first.
  sigc::connection mark_set_;
  // The window's set-focus, cut by the destructor likewise.
  sigc::connection set_focus_;
  // The idle that unhighlights the size list's first item; gone with the
  // window.
  sigc::connection size_popup_idle_;
  // The buffer has stopped tracking PRIMARY for the size box.
  bool primary_lent_ = false;
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
  // Its character format (Paragraph::mark), when it has one of its own.
  Run pending_mark_;
  bool pending_mark_set_ = false;
  std::string caret_key_;
  // Undo and redo, recorded as operations (undo.hpp); undo_.state_id() is
  // the saved-state hook.
  UndoHistory undo_;
  SideState side_before_;
  gint64 last_typed_us_ = 0;
  // The document's style sheet; empty for the default, as in Document.
  std::vector<Style> styles_;
  // Paper, header, footer, and footnotes. The body buffer does not hold
  // them; capture() copies these, and a command records the change.
  PageSetup page_setup_{};
  std::vector<Paragraph> header_;
  std::vector<Paragraph> footer_;
  std::vector<std::vector<Paragraph>> notes_;
  // Pictures in the body, keyed by the pixbuf the buffer is showing. The
  // value is the file's bytes; the pixbuf may be a scaled copy.
  std::map<GdkPixbuf*, Image> image_pix_;
  // The document as a cell edit began, so that edit is one undo step.
  Document cell_before_;
  bool cell_step_ = false;
  // capture() ignores this buffer range for one call. A delete that covers a
  // table anchor is applied to the document, because the anchor character
  // alone cannot bring the grid back.
  int capture_skip_from_ = -1;
  int capture_skip_to_ = -1;
  // View > Header and Footer. Not saved. A file that has either opens with
  // it on, until the user has toggled the item (stories_chosen_).
  bool stories_on_ = false;
  bool stories_chosen_ = false;
  bool suppress_stories_ = false;
  // The text the story views are showing. An edit that still matches it
  // keeps the document's own paragraphs.
  std::string header_shown_;
  std::string footer_shown_;
  std::string notes_shown_;
  // Enter at the end of a paragraph: the new one takes the next style.
  bool next_style_pending_ = false;
  // apply_align() is under way: the alignment was chosen, so it is direct.
  bool chose_align_ = false;
  int next_style_from_ = -1;
  Glib::RefPtr<Gtk::TextMark> insert_start_;
  Glib::RefPtr<Gtk::TextMark> insert_end_;
  // Character formats applied over the text inserted in this user action:
  // a paste or drop of this document's text copying its source's tags, or
  // Replace giving the match's format. finish_pending() keeps these on the
  // new text and nothing else GTK gave it from around the insertion point.
  struct AppliedFmt {
    Glib::RefPtr<Gtk::TextTag> tag;
    Glib::RefPtr<Gtk::TextMark> from;
    Glib::RefPtr<Gtk::TextMark> to;
  };
  std::vector<AppliedFmt> applied_fmt_;
  // Paragraph formats applied likewise: a paste's or drop's source tags.
  std::vector<AppliedFmt> applied_para_;

  std::unique_ptr<Gtk::Dialog> find_dialog_;
  Gtk::Entry* find_entry_ = nullptr;
  Gtk::Entry* replace_entry_ = nullptr;
  Gtk::CheckButton* match_case_ = nullptr;
};

}  // namespace writeit
