/* SPDX-License-Identifier: Unlicense */

// The real window, under a display: undo and redo as recorded operations.
// Every edit undone gives back the very document (capture() equality, which
// sees text, character format, paragraph format, lists, styles and the
// empty last paragraph's format), and redo the edited one. Steps group as
// before: a burst of typing is one step, each command is one, a no-op is
// none; a run of Backspace or of Delete is one step too. The
// state id hook (UndoHistory::state_id()) says when the document is back at
// its saved state, and a merge into the top step is a new state. Screen-only
// list tags never make a step. Last, the bookkeeping one keystroke costs in
// a 1,000-paragraph document against a 10-paragraph one.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  // A document as Open leaves it: no undo, unmodified.
  static void load(MainWindow& w, const Document& doc)
  {
    w.undo_.clear();
    w.replace_buffer(doc, 0);
    w.saved_state_ = w.undo_.state_id();
    w.save_point_ = true;
    w.last_typed_us_ = 0;
    w.update_title();
    w.update_actions();
    w.sync_format_controls();
  }
  // What a save does to the window's state (write_rtf without the file).
  static void save(MainWindow& w)
  {
    w.saved_state_ = w.undo_.state_id();
    w.save_point_ = true;
    w.update_title();
  }
  static Document doc(MainWindow& w)
  {
    return w.capture();
  }
  static bool dirty(MainWindow& w)
  {
    return w.dirty();
  }
  static std::uint64_t id(MainWindow& w)
  {
    return w.undo_.state_id();
  }
  static sigc::signal<void>& changed(MainWindow& w)
  {
    return w.undo_.signal_state_changed();
  }
  static size_t steps(MainWindow& w)
  {
    return w.undo_.size();
  }
  static bool can_redo(MainWindow& w)
  {
    return w.undo_.can_redo();
  }
  static long stray(MainWindow& w)
  {
    return w.undo_.stray_edits();
  }
  static std::string describe(MainWindow& w)
  {
    return w.undo_.describe_top();
  }
  static size_t top_ops(MainWindow& w)
  {
    return w.undo_.top_ops();
  }
  static void caret(MainWindow& w, int at)
  {
    w.buffer_->place_cursor(w.buffer_->get_iter_at_offset(at));
  }
  static int caret(MainWindow& w)
  {
    return w.cursor_offset();
  }
  static void select(MainWindow& w, int from, int to)
  {
    w.buffer_->select_range(w.buffer_->get_iter_at_offset(from), w.buffer_->get_iter_at_offset(to));
  }
  // A key typed, as GTK types it: one interactive insertion at the caret.
  static void type(MainWindow& w, const char* text)
  {
    w.buffer_->insert_interactive_at_cursor(text, true);
  }
  // Backspace and Delete at the caret, as the text view does them.
  static void backspace(MainWindow& w)
  {
    auto iter = w.buffer_->get_insert()->get_iter();
    w.buffer_->begin_user_action();
    w.buffer_->backspace(iter, true, true);
    w.buffer_->end_user_action();
  }
  static void del(MainWindow& w)
  {
    auto start = w.buffer_->get_insert()->get_iter();
    auto end = start;
    end.forward_char();
    w.buffer_->begin_user_action();
    w.buffer_->erase_interactive(start, end, true);
    w.buffer_->end_user_action();
  }
  static void copy(MainWindow& w)
  {
    w.buffer_->copy_clipboard(Gtk::Clipboard::get());
  }
  static void paste(MainWindow& w)
  {
    w.buffer_->paste_clipboard(Gtk::Clipboard::get());
  }
  static int length(MainWindow& w)
  {
    return w.buffer_->get_char_count();
  }
  static void delete_selection(MainWindow& w)
  {
    w.buffer_->erase_selection(true, true);
  }
  // Enter or Backspace as the window takes it: its list keys first.
  static void key(MainWindow& w, guint keyval)
  {
    GdkEventKey event{};
    event.type = GDK_KEY_PRESS;
    event.keyval = keyval;
    if (w.on_text_key(&event))
      return;
    if (keyval == GDK_KEY_Return)
      type(w, "\n");
    else if (keyval == GDK_KEY_BackSpace)
      backspace(w);
  }
  // More than the coalescing window since the last edit.
  static void pause(MainWindow& w)
  {
    if (w.last_typed_us_ != 0)
      w.last_typed_us_ -= 2000000;
  }
  static void undo(MainWindow& w)
  {
    w.undo();
  }
  static void redo(MainWindow& w)
  {
    w.redo();
  }
  static void bold(MainWindow& w)
  {
    w.toggle_flag(MainWindow::TextFlag::Bold);
  }
  static void italic(MainWindow& w)
  {
    w.toggle_flag(MainWindow::TextFlag::Italic);
  }
  static void underline(MainWindow& w)
  {
    w.toggle_flag(MainWindow::TextFlag::Underline);
  }
  static void font(MainWindow& w, const char* name)
  {
    w.apply_run_edit([name](Run& run) {
      run.font = name;
      run.direct |= kDirectFont;
    });
  }
  static void align(MainWindow& w, Align align)
  {
    w.apply_align(align);
  }
  static void indents(MainWindow& w, Indents indents)
  {
    w.apply_para_edit([indents](ParaFormat& format) { format.indents = indents; });
  }
  static void style(MainWindow& w, const char* name)
  {
    w.apply_named_style(name);
  }
  // As Format > Style...'s OK: one undo step.
  static bool edit_style(MainWindow& w, const Style& style)
  {
    const Document before = w.capture();
    Document after = before;
    adopt_sheet(after, w.typing_.font, w.typing_.size);
    after.styles = complete_sheet(style_sheet(after));
    if (!update_style(after, style.name, style))
      return false;
    w.commit_document(before, after);
    return true;
  }
  static Style style_named(MainWindow& w, const char* name)
  {
    for (const Style& style : w.sheet())
      if (style.name == name)
        return style;
    return Style{};
  }
  static void list(MainWindow& w, ListKind kind)
  {
    w.toggle_list_kind(kind);
  }
  static bool level(MainWindow& w, int delta)
  {
    return w.shift_list_level(delta);
  }
  static bool restart(MainWindow& w)
  {
    return w.renumber_list(true);
  }
  static int line_start(MainWindow& w, int line)
  {
    return w.buffer_->get_iter_at_line(line).get_offset();
  }
  // Whether paragraph `line`'s first character has a screen-only list tag.
  static bool screen_tag(MainWindow& w, int line, const char* prefix)
  {
    for (const auto& tag : w.buffer_->get_iter_at_line(line).get_tags()) {
      const std::string name = tag->property_name().get_value();
      if (name.compare(0, std::string(prefix).size(), prefix) == 0)
        return true;
    }
    return false;
  }
  // The undo bookkeeping of one keystroke: the step opened, the key's text
  // recorded as the buffer inserts it, the step closed (and coalesced).
  // One keystroke's undo work, as on_user_begin() and on_user_end() do it
  // around the buffer's insertion: the step opened and closed (`book`), and
  // the insertion itself with the history recording it (`insert`).
  struct Split {
    double book;
    double insert;
  };
  static Split bookkeeping_us(MainWindow& w)
  {
    using us = std::chrono::duration<double, std::micro>;
    const auto t0 = std::chrono::steady_clock::now();
    w.open_step();
    const auto ta = std::chrono::steady_clock::now();
    w.buffer_->insert_at_cursor("x");
    const auto tb = std::chrono::steady_clock::now();
    w.close_step(true);
    const auto t1 = std::chrono::steady_clock::now();
    return Split{us(ta - t0).count() + us(t1 - tb).count(), us(tb - ta).count()};
  }
  // The buffer's insertion with no undo at all (the history emptied first,
  // so nothing is recorded), to tell the recording from the insertion.
  static double insert_only_us(MainWindow& w)
  {
    w.undo_.clear();
    const auto t0 = std::chrono::steady_clock::now();
    w.buffer_->insert_at_cursor("x");
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(t1 - t0).count();
  }
  // A whole keystroke, the window's other work included.
  static double keystroke_us(MainWindow& w)
  {
    const auto t0 = std::chrono::steady_clock::now();
    type(w, "x");
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(t1 - t0).count();
  }
  static double undo_us(MainWindow& w)
  {
    const auto t0 = std::chrono::steady_clock::now();
    w.undo();
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(t1 - t0).count();
  }
};

}  // namespace writeit

namespace {

using writeit::Align;
using writeit::Document;
using writeit::ListKind;
using writeit::MainWindowProbe;
using writeit::Paragraph;
using writeit::Run;

void settle()
{
  auto context = Glib::MainContext::get_default();
  for (int round = 0; round < 4; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(20000);
  }
  while (context->pending())
    context->iteration(false);
}

Run run(const std::string& text, bool bold = false, bool italic = false, double size = 11)
{
  Run r;
  r.text = text;
  r.font = "Sans";
  r.size = size;
  r.bold = bold;
  r.italic = italic;
  return r;
}

Paragraph para(const std::string& text)
{
  Paragraph p;
  p.runs.push_back(run(text));
  return p;
}

Document document(const std::vector<Paragraph>& paragraphs)
{
  Document doc = writeit::blank_document("Sans", 11);
  doc.paragraphs = paragraphs;
  return doc;
}

std::string text_of(const Document& doc)
{
  std::string text;
  for (size_t i = 0; i < doc.paragraphs.size(); ++i) {
    if (i > 0)
      text += '\n';
    for (const Run& r : doc.paragraphs[i].runs)
      text += r.text;
  }
  return text;
}

// Undo gives `before` back and redo `after`, twice over; nothing strays.
// Four checks.
void round_trip(writeit::MainWindow& w, const Document& before, const Document& after)
{
  bool undone = true;
  bool redone = true;
  for (int pass = 0; pass < 2; ++pass) {
    MainWindowProbe::undo(w);
    settle();
    undone = undone && MainWindowProbe::doc(w) == before;
    MainWindowProbe::redo(w);
    settle();
    redone = redone && MainWindowProbe::doc(w) == after;
  }
  CHECK(undone);
  CHECK(redone);
  MainWindowProbe::undo(w);
  settle();
  CHECK(MainWindowProbe::doc(w) == before);
  CHECK(MainWindowProbe::stray(w) == 0);
}

// The dirty rule (Greg's decision: the history rule, see DEVELOPMENT.md and
// MainWindow::dirty()). The mark clears only at the saved undo state, so "a"
// then Backspace (text as saved, history not) stays dirty; "a" then Undo is
// clean; any edit, even one space on an empty line, marks it.
constexpr bool kDirtyFollowsHistory = true;

void dirty_rule(writeit::MainWindow& w, const Document& start)
{
  MainWindowProbe::load(w, start);
  MainWindowProbe::caret(w, 5);
  MainWindowProbe::type(w, "a");
  MainWindowProbe::pause(w);
  MainWindowProbe::backspace(w);
  CHECK(MainWindowProbe::doc(w) == start);
  CHECK(MainWindowProbe::dirty(w) == kDirtyFollowsHistory);
  CHECK((w.get_title().find('*') != Glib::ustring::npos) == kDirtyFollowsHistory);
  MainWindowProbe::load(w, start);
  MainWindowProbe::caret(w, 5);
  MainWindowProbe::type(w, "a");
  MainWindowProbe::undo(w);
  CHECK(!MainWindowProbe::dirty(w) && w.get_title().find('*') == Glib::ustring::npos);
  // Any edit marks it, even a single space on an empty line.
  MainWindowProbe::load(w, writeit::blank_document("Sans", 11));
  MainWindowProbe::caret(w, 0);
  MainWindowProbe::type(w, " ");
  CHECK(MainWindowProbe::dirty(w) && w.get_title().find('*') != Glib::ustring::npos);
}

// Typing: one step per burst, the caret back where it began, the saved
// state found again.
void typing(writeit::MainWindow& w)
{
  const Document start = document({para("Hello"), para("world")});
  MainWindowProbe::load(w, start);
  settle();
  const std::uint64_t saved = MainWindowProbe::id(w);
  CHECK(!MainWindowProbe::dirty(w) && MainWindowProbe::steps(w) == 0);
  MainWindowProbe::caret(w, 5);
  for (const char* key : {",", " ", "t", "h", "e", "r", "e"})
    MainWindowProbe::type(w, key);
  settle();
  const Document typed = MainWindowProbe::doc(w);
  CHECK(text_of(typed) == "Hello, there\nworld");
  // A burst is one step, recorded as one insertion: the format passes
  // after each key (finish_pending(), tag_line_breaks()) put back the tags
  // they take off, which records nothing.
  CHECK(MainWindowProbe::steps(w) == 1);
  CHECK(MainWindowProbe::top_ops(w) == 1);
  if (MainWindowProbe::top_ops(w) != 1)
    std::cerr << MainWindowProbe::describe(w);
  CHECK(MainWindowProbe::dirty(w) && MainWindowProbe::id(w) != saved);
  // Undo back to the saved state: clean again, by the hook and by the title.
  MainWindowProbe::undo(w);
  settle();
  CHECK(MainWindowProbe::doc(w) == start && MainWindowProbe::caret(w) == 5);
  CHECK(MainWindowProbe::id(w) == saved && !MainWindowProbe::dirty(w));
  CHECK(w.get_title().find('*') == Glib::ustring::npos);
  MainWindowProbe::redo(w);
  settle();
  CHECK(MainWindowProbe::doc(w) == typed && MainWindowProbe::dirty(w));
  CHECK(MainWindowProbe::caret(w) == 12);
  // A pause starts another step.
  MainWindowProbe::pause(w);
  MainWindowProbe::type(w, "!");
  CHECK(MainWindowProbe::steps(w) == 2);
  // So does a moved caret.
  MainWindowProbe::caret(w, 0);
  MainWindowProbe::type(w, ">");
  CHECK(MainWindowProbe::steps(w) == 3);
  MainWindowProbe::undo(w);
  MainWindowProbe::undo(w);
  settle();
  CHECK(MainWindowProbe::doc(w) == typed);
  // An edit after undo forgets redo.
  MainWindowProbe::caret(w, 0);
  MainWindowProbe::type(w, "#");
  CHECK(!MainWindowProbe::can_redo(w));
  dirty_rule(w, start);
  // Enter is typing too, and comes back out as one with it.
  MainWindowProbe::load(w, start);
  MainWindowProbe::caret(w, 5);
  MainWindowProbe::type(w, "a");
  MainWindowProbe::type(w, "\n");
  MainWindowProbe::type(w, "b");
  settle();
  CHECK(text_of(MainWindowProbe::doc(w)) == "Helloa\nb\nworld" && MainWindowProbe::steps(w) == 1);
  round_trip(w, start, MainWindowProbe::doc(w));
}

// Saved in the middle of a burst: the edit merged into the top step after
// the save is a new state (a fresh id, and the signal), though one undo
// still takes the whole burst back.
void merged_after_save(writeit::MainWindow& w)
{
  int emitted = 0;
  sigc::connection counter = MainWindowProbe::changed(w).connect([&emitted] { ++emitted; });

  // Typing.
  const Document start = document({para("Hello")});
  MainWindowProbe::load(w, start);
  MainWindowProbe::caret(w, 5);
  MainWindowProbe::type(w, "a");
  MainWindowProbe::type(w, "b");
  MainWindowProbe::save(w);
  const std::uint64_t saved = MainWindowProbe::id(w);
  emitted = 0;
  MainWindowProbe::type(w, "c");
  CHECK(MainWindowProbe::steps(w) == 1 && text_of(MainWindowProbe::doc(w)) == "Helloabc");
  CHECK(MainWindowProbe::id(w) != saved && emitted == 1 && MainWindowProbe::dirty(w));
  MainWindowProbe::undo(w);
  CHECK(MainWindowProbe::doc(w) == start && MainWindowProbe::steps(w) == 0);

  // A Delete run.
  const Document letters = document({para("abcdef")});
  MainWindowProbe::load(w, letters);
  MainWindowProbe::caret(w, 1);
  MainWindowProbe::del(w);
  MainWindowProbe::del(w);
  CHECK(MainWindowProbe::steps(w) == 1 && text_of(MainWindowProbe::doc(w)) == "adef");
  MainWindowProbe::save(w);
  const std::uint64_t saved_delete = MainWindowProbe::id(w);
  emitted = 0;
  MainWindowProbe::del(w);
  CHECK(MainWindowProbe::steps(w) == 1 && text_of(MainWindowProbe::doc(w)) == "aef");
  CHECK(MainWindowProbe::id(w) != saved_delete && emitted == 1 && MainWindowProbe::dirty(w));
  MainWindowProbe::undo(w);
  CHECK(MainWindowProbe::doc(w) == letters && MainWindowProbe::caret(w) == 1);

  // A Backspace run.
  MainWindowProbe::load(w, letters);
  MainWindowProbe::caret(w, 6);
  MainWindowProbe::backspace(w);
  MainWindowProbe::backspace(w);
  CHECK(MainWindowProbe::steps(w) == 1 && text_of(MainWindowProbe::doc(w)) == "abcd");
  MainWindowProbe::save(w);
  const std::uint64_t saved_back = MainWindowProbe::id(w);
  emitted = 0;
  MainWindowProbe::backspace(w);
  CHECK(MainWindowProbe::steps(w) == 1 && text_of(MainWindowProbe::doc(w)) == "abc");
  CHECK(MainWindowProbe::id(w) != saved_back && emitted == 1 && MainWindowProbe::dirty(w));
  MainWindowProbe::undo(w);
  CHECK(MainWindowProbe::doc(w) == letters && MainWindowProbe::caret(w) == 6);
  // Redo is the merged state, not the saved one.
  MainWindowProbe::redo(w);
  CHECK(text_of(MainWindowProbe::doc(w)) == "abc" && MainWindowProbe::id(w) != saved_back);

  // Backspace then Delete are two steps; so is a run broken by a pause.
  MainWindowProbe::load(w, letters);
  MainWindowProbe::caret(w, 3);
  MainWindowProbe::backspace(w);
  MainWindowProbe::del(w);
  MainWindowProbe::pause(w);
  MainWindowProbe::del(w);
  CHECK(MainWindowProbe::steps(w) == 3 && text_of(MainWindowProbe::doc(w)) == "abf");
  counter.disconnect();
}

// Deleting formatted text across paragraphs, and pasting it back: the tags
// come back exactly.
void delete_and_paste(writeit::MainWindow& w)
{
  Paragraph mixed;
  mixed.runs = {run("plain "), run("bold", true), run(" and "), run("big", false, true, 18)};
  Paragraph centred = para("centred");
  centred.align = Align::Center;
  centred.direct = writeit::kDirectAlign;
  const Document start = document({mixed, centred, para("last")});
  MainWindowProbe::load(w, start);
  settle();
  // From inside "bold" to inside "centred", newline and all.
  MainWindowProbe::select(w, 8, 22);
  MainWindowProbe::delete_selection(w);
  settle();
  const Document deleted = MainWindowProbe::doc(w);
  CHECK(text_of(deleted) == "plain botred\nlast" && MainWindowProbe::steps(w) == 1);
  round_trip(w, start, deleted);

  // Copy "bold and" (no step), paste it at the end of "last".
  MainWindowProbe::select(w, 6, 14);
  const size_t steps = MainWindowProbe::steps(w);
  MainWindowProbe::copy(w);
  settle();
  CHECK(MainWindowProbe::steps(w) == steps);
  MainWindowProbe::caret(w, MainWindowProbe::length(w));
  MainWindowProbe::paste(w);
  settle();
  const Document pasted = MainWindowProbe::doc(w);
  CHECK(text_of(pasted) == "plain bold and big\ncentred\nlastbold and");
  CHECK(MainWindowProbe::steps(w) == steps + 1);
  round_trip(w, start, pasted);
}

// Bold, italic, underline and font over a selection: one step each, and
// back exactly. Over nothing selected, or as a no-op, no step.
void formats(writeit::MainWindow& w)
{
  Paragraph mixed;
  mixed.runs = {run("one "), run("two", true), run(" three")};
  const Document start = document({mixed, para("second"), Paragraph{}, para("end")});
  MainWindowProbe::load(w, start);
  settle();
  MainWindowProbe::select(w, 0, 14);
  MainWindowProbe::bold(w);
  CHECK(MainWindowProbe::steps(w) == 1);
  round_trip(w, start, [&] {
    MainWindowProbe::redo(w);
    const Document after = MainWindowProbe::doc(w);
    MainWindowProbe::undo(w);
    return after;
  }());
  MainWindowProbe::select(w, 4, 7);
  MainWindowProbe::italic(w);
  Document after = MainWindowProbe::doc(w);
  CHECK(!(after == start));
  round_trip(w, start, after);
  // Underline across the empty paragraph, whose newline holds its format.
  MainWindowProbe::select(w, 0, MainWindowProbe::length(w));
  MainWindowProbe::underline(w);
  after = MainWindowProbe::doc(w);
  CHECK(after.paragraphs[2].mark && after.paragraphs[2].mark->underline);
  round_trip(w, start, after);
  MainWindowProbe::select(w, 0, 3);
  MainWindowProbe::font(w, "Serif");
  after = MainWindowProbe::doc(w);
  round_trip(w, start, after);
  // The same font again over the same text: no change, no step.
  MainWindowProbe::redo(w);
  const size_t steps = MainWindowProbe::steps(w);
  MainWindowProbe::select(w, 0, 3);
  MainWindowProbe::font(w, "Serif");
  CHECK(MainWindowProbe::steps(w) == steps && MainWindowProbe::doc(w) == after);
  // Bold with nothing selected sets what is typed next only.
  MainWindowProbe::caret(w, 2);
  MainWindowProbe::bold(w);
  CHECK(MainWindowProbe::steps(w) == steps);
  CHECK(MainWindowProbe::stray(w) == 0);
}

// Alignment and indents, over one paragraph and over several.
void paragraphs(writeit::MainWindow& w)
{
  const Document start = document({para("alpha"), para("beta"), para("gamma")});
  MainWindowProbe::load(w, start);
  settle();
  MainWindowProbe::select(w, 2, 8);
  MainWindowProbe::align(w, Align::Center);
  Document after = MainWindowProbe::doc(w);
  CHECK(after.paragraphs[0].align == Align::Center && after.paragraphs[1].align == Align::Center &&
        after.paragraphs[2].align == Align::Left && MainWindowProbe::steps(w) == 1);
  round_trip(w, start, after);
  MainWindowProbe::caret(w, 7);
  MainWindowProbe::indents(w, writeit::Indents{720, 360, -360});
  after = MainWindowProbe::doc(w);
  CHECK(after.paragraphs[1].indents == (writeit::Indents{720, 360, -360}));
  round_trip(w, start, after);
  MainWindowProbe::select(w, 0, MainWindowProbe::length(w));
  MainWindowProbe::align(w, Align::Justify);
  after = MainWindowProbe::doc(w);
  round_trip(w, start, after);
  // Typing in a centred paragraph after undo and redo keeps it centred.
  MainWindowProbe::redo(w);
  MainWindowProbe::caret(w, 5);
  MainWindowProbe::type(w, "!");
  CHECK(MainWindowProbe::doc(w).paragraphs[0].align == Align::Justify);
  round_trip(w, after, MainWindowProbe::doc(w));
}

// Named styles: applied, edited in Format > Style..., and the following
// style Enter gives a new paragraph.
void styles(writeit::MainWindow& w)
{
  const Document start = document({para("Title"), para("Body")});
  MainWindowProbe::load(w, start);
  settle();
  MainWindowProbe::caret(w, 0);
  MainWindowProbe::style(w, "Heading 1");
  Document after = MainWindowProbe::doc(w);
  CHECK(after.paragraphs[0].style == "Heading 1" && MainWindowProbe::steps(w) == 1);
  round_trip(w, start, after);
  // Normal made 14 pt and centred: the sheet and every Normal paragraph.
  writeit::Style normal = MainWindowProbe::style_named(w, "Normal");
  normal.format.size = 14;
  normal.align = Align::Center;
  CHECK(MainWindowProbe::edit_style(w, normal));
  after = MainWindowProbe::doc(w);
  CHECK(after.paragraphs[1].align == Align::Center && !(after.styles == start.styles));
  round_trip(w, start, after);

  // Enter at the end of a heading: the new paragraph is Normal, in the
  // middle of the document and as the empty last paragraph.
  MainWindowProbe::caret(w, 0);
  MainWindowProbe::style(w, "Heading 1");
  const Document heading = MainWindowProbe::doc(w);
  MainWindowProbe::caret(w, 5);
  MainWindowProbe::pause(w);
  MainWindowProbe::key(w, GDK_KEY_Return);
  after = MainWindowProbe::doc(w);
  CHECK(after.paragraphs.size() == 3 && after.paragraphs[1].style == "Normal");
  round_trip(w, heading, after);
  MainWindowProbe::load(w, document({para("Title")}));
  MainWindowProbe::caret(w, 0);
  MainWindowProbe::style(w, "Heading 1");
  const Document alone = MainWindowProbe::doc(w);
  MainWindowProbe::caret(w, 5);
  MainWindowProbe::pause(w);
  MainWindowProbe::key(w, GDK_KEY_Return);
  after = MainWindowProbe::doc(w);
  CHECK(after.paragraphs.size() == 2 && after.paragraphs[1].style == "Normal");
  round_trip(w, alone, after);
  // Typed into after redo, the new paragraph is still Normal.
  MainWindowProbe::redo(w);
  MainWindowProbe::caret(w, MainWindowProbe::length(w));
  MainWindowProbe::type(w, "x");
  CHECK(MainWindowProbe::doc(w).paragraphs[1].style == "Normal");
}

// Lists: Bullets and Numbering, Enter and Backspace on items, Tab, Restart
// Numbering. Labels are drawn and wide ones move text with screen-only
// tags; neither makes a step, and undo keeps them right.
void lists(writeit::MainWindow& w)
{
  std::vector<Paragraph> items;
  for (int i = 0; i < 12; ++i)
    items.push_back(para("Item"));
  const Document start = document(items);
  MainWindowProbe::load(w, start);
  settle();
  MainWindowProbe::select(w, 0, MainWindowProbe::length(w));
  MainWindowProbe::list(w, ListKind::Number);
  settle();
  const Document numbered = MainWindowProbe::doc(w);
  CHECK(numbered.paragraphs[11].list.kind == ListKind::Number);
  // "10." moves its text to the next stop, on screen only: no step.
  CHECK(MainWindowProbe::screen_tag(w, 9, "list-tab"));
  CHECK(MainWindowProbe::steps(w) == 1 && MainWindowProbe::stray(w) == 0);
  round_trip(w, start, numbered);
  MainWindowProbe::redo(w);
  settle();
  CHECK(MainWindowProbe::doc(w) == numbered && MainWindowProbe::screen_tag(w, 9, "list-tab"));
  // A centred item, then typing in item 11: one step each, none from the
  // screen passes.
  MainWindowProbe::caret(w, 0);
  MainWindowProbe::align(w, Align::Center);
  settle();
  const Document centred = MainWindowProbe::doc(w);
  const size_t steps = MainWindowProbe::steps(w);
  MainWindowProbe::caret(w, MainWindowProbe::line_start(w, 10) + 4);
  MainWindowProbe::type(w, "x");
  settle();
  CHECK(MainWindowProbe::steps(w) == steps + 1);
  round_trip(w, centred, MainWindowProbe::doc(w));
  // Deleting from item 9 into item 11, through the moved items' text.
  MainWindowProbe::select(w, MainWindowProbe::line_start(w, 8) + 2,
                          MainWindowProbe::line_start(w, 10) + 2);
  MainWindowProbe::delete_selection(w);
  settle();
  const Document joined = MainWindowProbe::doc(w);
  CHECK(joined.paragraphs.size() == 10);
  round_trip(w, centred, joined);
  CHECK(MainWindowProbe::screen_tag(w, 9, "list-tab") &&
        MainWindowProbe::screen_tag(w, 11, "list-tab"));
  // Enter at the end of the last item makes another; Enter on it ends the
  // list there, a step of its own.
  MainWindowProbe::caret(w, MainWindowProbe::length(w));
  MainWindowProbe::pause(w);
  MainWindowProbe::key(w, GDK_KEY_Return);
  const Document thirteen = MainWindowProbe::doc(w);
  CHECK(thirteen.paragraphs.size() == 13 && thirteen.paragraphs[12].list.kind == ListKind::Number);
  MainWindowProbe::key(w, GDK_KEY_Return);
  const Document ended = MainWindowProbe::doc(w);
  CHECK(ended.paragraphs.size() == 13 && ended.paragraphs[12].list.kind == ListKind::None);
  round_trip(w, thirteen, ended);
  MainWindowProbe::undo(w);
  CHECK(MainWindowProbe::doc(w) == centred);
  // Backspace at the start of item 4 takes it out of the list.
  MainWindowProbe::caret(w, MainWindowProbe::line_start(w, 3));
  MainWindowProbe::key(w, GDK_KEY_BackSpace);
  Document after = MainWindowProbe::doc(w);
  CHECK(after.paragraphs[3].list.kind == ListKind::None);
  round_trip(w, centred, after);
  // Tab at the start of item 2, and Restart Numbering at item 6.
  MainWindowProbe::caret(w, MainWindowProbe::line_start(w, 1));
  CHECK(MainWindowProbe::level(w, 1));
  after = MainWindowProbe::doc(w);
  CHECK(after.paragraphs[1].list.level == 1);
  round_trip(w, centred, after);
  MainWindowProbe::caret(w, MainWindowProbe::line_start(w, 5));
  MainWindowProbe::select(w, MainWindowProbe::line_start(w, 5), MainWindowProbe::line_start(w, 5));
  CHECK(MainWindowProbe::restart(w));
  after = MainWindowProbe::doc(w);
  round_trip(w, centred, after);
  settle();
  CHECK(MainWindowProbe::screen_tag(w, 9, "list-tab"));
}

// The empty last paragraph's format, which no tag holds: kept by erasing
// everything, set by aligning it alone, given up by typing into it.
void last_paragraph(writeit::MainWindow& w)
{
  // Erased through to the end, the last paragraph's format stays.
  Paragraph right = para("two");
  right.align = Align::Right;
  right.direct = writeit::kDirectAlign;
  const Document start = document({para("one"), right});
  MainWindowProbe::load(w, start);
  settle();
  MainWindowProbe::select(w, 0, MainWindowProbe::length(w));
  MainWindowProbe::delete_selection(w);
  Document after = MainWindowProbe::doc(w);
  CHECK(after.paragraphs.size() == 1 && after.paragraphs[0].align == Align::Right);
  round_trip(w, start, after);

  const Document trailing = document({para("one"), Paragraph{}});
  MainWindowProbe::load(w, trailing);
  settle();
  MainWindowProbe::caret(w, MainWindowProbe::length(w));
  MainWindowProbe::align(w, Align::Center);
  after = MainWindowProbe::doc(w);
  CHECK(after.paragraphs[1].align == Align::Center && after.paragraphs[0].align == Align::Left);
  CHECK(MainWindowProbe::steps(w) == 1);
  round_trip(w, trailing, after);
  MainWindowProbe::redo(w);
  MainWindowProbe::caret(w, MainWindowProbe::length(w));
  MainWindowProbe::type(w, "x");
  const Document typed = MainWindowProbe::doc(w);
  CHECK(typed.paragraphs[1].align == Align::Center && text_of(typed) == "one\nx");
  round_trip(w, after, typed);
}

// 200 steps at most; undoing them all does not reach the state before the
// first, which is gone.
void cap(writeit::MainWindow& w)
{
  const Document start = document({para("a")});
  MainWindowProbe::load(w, start);
  const std::uint64_t first = MainWindowProbe::id(w);
  MainWindowProbe::caret(w, 1);
  for (int i = 0; i < 205; ++i) {
    MainWindowProbe::pause(w);
    MainWindowProbe::type(w, "x");
  }
  CHECK(MainWindowProbe::steps(w) == 200);
  for (int i = 0; i < 205; ++i)
    MainWindowProbe::undo(w);
  CHECK(MainWindowProbe::steps(w) == 0 && text_of(MainWindowProbe::doc(w)) == "axxxxx");
  CHECK(MainWindowProbe::id(w) != first && MainWindowProbe::dirty(w));
}

double median(std::vector<double> samples)
{
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

Document long_document(int count)
{
  std::vector<Paragraph> paragraphs;
  for (int i = 0; i < count; ++i) {
    Paragraph p;
    p.runs = {run("Paragraph " + std::to_string(i) + " has "), run("some bold", true),
              run(" and plain text after it.")};
    paragraphs.push_back(p);
  }
  return document(paragraphs);
}

// The undo bookkeeping of one keystroke does not grow with the document.
void timing(writeit::MainWindow& w)
{
  double book[2] = {0, 0};
  int index = 0;
  for (int count : {10, 1000}) {
    MainWindowProbe::load(w, long_document(count));
    settle();
    MainWindowProbe::caret(w, MainWindowProbe::line_start(w, count / 2) + 10);
    // Interleaved, so both see the same buffer and the same pending work.
    std::vector<double> plain;
    std::vector<double> recorded;
    std::vector<double> samples;
    for (int i = 0; i < 300; ++i) {
      plain.push_back(MainWindowProbe::insert_only_us(w));
      const MainWindowProbe::Split split = MainWindowProbe::bookkeeping_us(w);
      samples.push_back(split.book);
      recorded.push_back(split.insert);
    }
    book[index] = median(samples);
    settle();
    // A burst of 320 characters, one step, and its undo.
    MainWindowProbe::caret(w, MainWindowProbe::line_start(w, count / 2) + 10);
    for (int i = 0; i < 320; ++i)
      MainWindowProbe::bookkeeping_us(w);
    const double undo = MainWindowProbe::undo_us(w);
    settle();
    std::vector<double> keys;
    for (int i = 0; i < 30; ++i)
      keys.push_back(MainWindowProbe::keystroke_us(w));
    settle();
    std::cout << "  " << count << " paragraphs: undo bookkeeping " << book[index]
              << " us per keystroke (median of 300); buffer insert " << median(recorded)
              << " us recorded, " << median(plain) << " us not; whole keystroke " << median(keys)
              << " us (median of 30); undo of 320 typed characters " << undo << " us\n";
    ++index;
  }
  CHECK(book[1] < std::max(3.0 * book[0], book[0] + 100.0));
}

}  // namespace

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-undo-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "undo: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    typing(window);
    merged_after_save(window);
    delete_and_paste(window);
    formats(window);
    paragraphs(window);
    styles(window);
    lists(window);
    last_paragraph(window);
    cap(window);
    timing(window);
    window.hide();
    settle();
  }
  g_rmdir(home.c_str());
  return suite_test::done("undo", 175);
}
