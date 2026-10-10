/* SPDX-License-Identifier: Unlicense */

// Undo fuzz, after bench/undo_fuzz.md. Two halves:
//
// The window half (needs a display). Runs of random edits, each through the
// path a user takes: key events to the window (typing, Delete, Backspace,
// Enter, Tab, Ctrl+B/I/U, Ctrl+X/C/V, Ctrl+Z/Y, Ctrl+S), the toolbar boxes and
// buttons, the Format menu's alignment items (the window has no Ctrl+L/E/R/J)
// and the Paragraph dialog. After every key or action the main loop runs to
// idle and the document is recorded: capture(), its RTF, the caret, the undo
// steps, the dirty mark. A reference stack of edit states (pushed by an edit,
// popped by undo, its redo tail dropped by an edit) is the oracle: every
// undo and redo, in the middle of a run and in the undo-all and redo-all at
// its end, must give back that state, Document and RTF bytes, exactly. Each
// run starts from a file Write-It has saved once (the first save rewrites a
// list document) and opens it as Open does.
//
// The rules the window half holds the window to:
// - An action that changes nothing adds no undo step.
// - An action that changes the document adds one step, or joins the top step
//   only as a continued burst: typing (any insertion at the caret: keys,
//   Enter, paste with nothing selected) on from where the last insertion
//   ended, a Backspace on from where the last Backspace ended, a Delete at
//   the last Delete's place.
// - Undo puts the caret where the undone step began.
// - The dirty mark follows the history: clear exactly at the state the last
//   save or open left (MainWindow::dirty()); an edit merged into the step
//   that was on top at the save is a new state.
// - Save writes the RTF of the document; closing and reopening the saved
//   file gives back those bytes, clean, with no undo or redo.
//
// The model half (no display needed). The same seed drives random insert,
// erase and tag edits on a bare Gtk::TextBuffer with an UndoHistory, grouped
// in steps; undoing all and redoing all must give back every state, text and
// tags, exactly. The window's own operations sit on MainWindow, so the two
// halves share no operation and there is no cross-check between them.
//
// Inputs: UNDO_FUZZ_SEED (default 0x5752495445495431), UNDO_FUZZ_RUNS (200),
// UNDO_FUZZ_OPS (60). Random numbers are std::mt19937_64, choices r % n. On a
// failure the seed, run, step and op are printed and written, with the
// starting RTF and the op list, to $MESON_BUILD_ROOT (or the temp dir)
// /undo-fuzz-failure-<seed>-<run>.txt; UNDO_FUZZ_REPLAY=<that file> runs that
// one run again.
//
// The check count does not depend on the seed: each run ends in a fixed
// number of checks that sum up every step's. It is runs * 9 for the window
// half, runs * 2 for the model half, plus 6.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"
#include "undo.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  static bool open(MainWindow& w, const std::string& path)
  {
    return w.open_path(path, MainWindow::OpenKind::Rtf);
  }
  static Document doc(MainWindow& w)
  {
    return w.capture();
  }
  static bool dirty(MainWindow& w)
  {
    return w.dirty();
  }
  static size_t steps(MainWindow& w)
  {
    return w.undo_.size();
  }
  static bool can_undo(MainWindow& w)
  {
    return w.undo_item_->get_sensitive() && w.undo_.can_undo();
  }
  static bool can_redo(MainWindow& w)
  {
    return w.redo_item_->get_sensitive() && w.undo_.can_redo();
  }
  static long stray(MainWindow& w)
  {
    return w.undo_.stray_edits();
  }
  static std::string top(MainWindow& w)
  {
    return w.undo_.describe_top();
  }
  // The first character with two character formats or two paragraph
  // formats on it, or -1.
  static int doubled(MainWindow& w)
  {
    for (auto it = w.buffer_->begin(); !it.is_end(); ++it) {
      int fmt = 0;
      int para = 0;
      for (const auto& tag : it.get_tags()) {
        const std::string name = tag->property_name().get_value();
        fmt += name.rfind("fmt", 0) == 0;
        para += name.rfind("para", 0) == 0;
      }
      if (fmt > 1 || para > 1)
        return it.get_offset();
    }
    return -1;
  }
  static int caret(MainWindow& w)
  {
    return w.cursor_offset();
  }
  static Glib::RefPtr<Gtk::TextBuffer> buffer(MainWindow& w)
  {
    return w.buffer_;
  }
  static int length(MainWindow& w)
  {
    return w.buffer_->get_char_count();
  }
  static void place(MainWindow& w, int at)
  {
    w.text_.grab_focus();
    w.buffer_->place_cursor(w.buffer_->get_iter_at_offset(at));
  }
  static void select(MainWindow& w, int from, int to)
  {
    w.text_.grab_focus();
    w.buffer_->select_range(w.buffer_->get_iter_at_offset(to), w.buffer_->get_iter_at_offset(from));
  }
  static bool paste_ready(MainWindow& w)
  {
    return w.paste_item_->get_sensitive();
  }
  static void size_box(MainWindow& w, const char* size)
  {
    Gtk::Entry* entry = w.size_combo_.get_entry();
    entry->set_text(size);
    entry->activate();
    w.text_.grab_focus();
  }
  static void font_box(MainWindow& w, const char* font)
  {
    w.font_combo_.set_active_text(font);
    w.text_.grab_focus();
  }
  static void style_box(MainWindow& w, const char* style)
  {
    w.style_combo_.set_active_text(style);
    w.text_.grab_focus();
  }
  static void toggle(MainWindow& w, bool numbering)
  {
    Gtk::ToggleToolButton* button = numbering ? w.numbering_toggle_ : w.bullets_toggle_;
    button->set_active(!button->get_active());
  }
  static void align(MainWindow& w, int which)
  {
    Gtk::MenuItem* items[] = {w.align_left_item_, w.align_center_item_, w.align_right_item_,
                              w.justify_item_};
    items[which]->activate();
  }
  static void paragraph(MainWindow& w)
  {
    w.paragraph_item_->activate();
  }
  // The B, I and U buttons show what is typed next.
  static bool buttons_agree(MainWindow& w)
  {
    return w.bold_toggle_->get_active() == w.typing_.bold &&
           w.italic_toggle_->get_active() == w.typing_.italic &&
           w.underline_toggle_->get_active() == w.typing_.underline;
  }
};

}  // namespace writeit

namespace {

using writeit::Document;
using writeit::ListKind;
using writeit::MainWindowProbe;
using writeit::Paragraph;
using writeit::Run;

std::uint64_t env_number(const char* name, std::uint64_t fallback)
{
  const char* value = g_getenv(name);
  if (!value || !*value)
    return fallback;
  return std::strtoull(value, nullptr, 0);
}

int criticals = 0;
// Actions after which a character carries two character or paragraph
// formats. Paste on m2 does this already (with the snapshot undo too); it
// is counted and reported, not failed.
int doubled_actions = 0;

void count_log(const gchar* domain, GLogLevelFlags level, const gchar* message, gpointer)
{
  ++criticals;
  std::cerr << (domain ? domain : "") << ": " << message << "\n";
  (void)level;
}

void settle()
{
  auto context = Glib::MainContext::get_default();
  for (int round = 0; round < 3; ++round) {
    while (context->pending())
      context->iteration(false);
    if (round < 2)
      g_usleep(1000);
  }
}

// Runs the main loop until `done`, or two seconds.
template <class F>
bool wait_for(F done)
{
  auto context = Glib::MainContext::get_default();
  for (int i = 0; i < 400 && !done(); ++i) {
    while (context->pending())
      context->iteration(false);
    if (!done())
      g_usleep(5000);
  }
  return done();
}

void key(writeit::MainWindow& window, guint keyval, guint state = 0)
{
  GdkEvent* event = gdk_event_new(GDK_KEY_PRESS);
  event->key.window = GDK_WINDOW(g_object_ref(window.get_window()->gobj()));
  event->key.send_event = TRUE;
  event->key.time = GDK_CURRENT_TIME;
  event->key.state = state;
  event->key.keyval = keyval;
  GdkKeymapKey* keys = nullptr;
  gint n = 0;
  if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_for_display(gdk_display_get_default()),
                                        keyval, &keys, &n) &&
      n > 0) {
    event->key.hardware_keycode = static_cast<guint16>(keys[0].keycode);
    event->key.group = static_cast<guint8>(keys[0].group);
  }
  g_free(keys);
  GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default());
  gdk_event_set_device(event, gdk_seat_get_keyboard(seat));
  gtk_main_do_event(event);
  gdk_event_free(event);
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

// The three starting documents of the spec: empty; twelve Normal paragraphs
// with bold and italic and one empty paragraph; the same with a nested
// numbered list, a bullet, two headings, a 10.5 pt run and a direct indent.
Document starting(int kind)
{
  Document doc = writeit::blank_document("Sans", 11);
  if (kind == 0)
    return doc;
  doc.paragraphs.clear();
  for (int i = 0; i < 13; ++i) {
    Paragraph p;
    if (i == 7) {
      doc.paragraphs.push_back(p);
      continue;
    }
    p.runs = {run("Para " + std::to_string(i) + " "), run("bold", true), run(" and "),
              run("italic", false, true), run(" end.")};
    doc.paragraphs.push_back(p);
  }
  if (kind == 1)
    return doc;
  const int levels[] = {0, 1, 1, 2, 0, 1, 0};
  for (int i = 3; i <= 9; ++i) {
    Paragraph& p = doc.paragraphs[static_cast<size_t>(i)];
    if (p.runs.empty())
      p.runs.push_back(run("Item"));
    p.list = writeit::ListFormat(ListKind::Number, levels[i - 3]);
    p.list.list = 1;
    p.indents = writeit::list_indents(levels[i - 3]);
  }
  doc.paragraphs[10].list = writeit::ListFormat(ListKind::Bullet, 0);
  doc.paragraphs[10].indents = writeit::list_indents(0);
  doc.paragraphs[2].runs.push_back(run(" small", false, false, 10.5));
  doc.paragraphs[11].indents.left = 1440;
  doc.paragraphs[11].direct |= writeit::kDirectLeft;
  writeit::adopt_sheet(doc, "Sans", 11);
  writeit::apply_style(doc, 0, 0, "Heading 1");
  writeit::apply_style(doc, 6, 6, "Heading 2");
  return doc;
}

std::string read_file(const std::string& path)
{
  try {
    return Glib::file_get_contents(path);
  } catch (const Glib::Error&) {
    return "<unreadable>";
  }
}

std::string around(const std::string& a, const std::string& b)
{
  size_t i = 0;
  while (i < a.size() && i < b.size() && a[i] == b[i])
    ++i;
  const size_t from = i > 1024 ? i - 1024 : 0;
  return "first difference at byte " + std::to_string(i) + "\n--- expected\n" +
         a.substr(from, 2048) + "\n--- actual\n" + b.substr(from, 2048) + "\n";
}

struct State {
  Document doc;
  std::string rtf;
  std::uint64_t serial = 0;
  // Where the caret was before the edit that made this state: undo puts it
  // back there.
  int caret_before = -1;
};

enum class Kind {
  Type,
  DelBack,
  DeleteRange,
  Cut,
  CopyPaste,
  PasteOver,
  Flag,
  Size,
  Font,
  Style,
  ListToggle,
  Tab,
  ListEnter,
  ListBackspace,
  Enter,
  Align,
  Paragraph,
  UndoRedo,
  Save,
  Reopen
};

struct Weighted {
  Kind kind;
  int weight;
  const char* name;
};

const Weighted kOps[] = {
    {Kind::Type, 14, "type"},
    {Kind::DelBack, 8, "delete/backspace"},
    {Kind::DeleteRange, 6, "delete range"},
    {Kind::Cut, 3, "cut"},
    {Kind::CopyPaste, 6, "copy, paste elsewhere"},
    {Kind::PasteOver, 3, "paste over selection"},
    {Kind::Flag, 8, "bold/italic/underline"},
    {Kind::Size, 5, "size box"},
    {Kind::Font, 2, "font box"},
    {Kind::Style, 6, "style box"},
    {Kind::ListToggle, 6, "bullets/numbering"},
    {Kind::Tab, 6, "tab/shift+tab on item"},
    {Kind::ListEnter, 5, "enter at end of item"},
    {Kind::ListBackspace, 4, "backspace at start of item"},
    {Kind::Enter, 4, "enter in run / at paragraph start"},
    {Kind::Align, 2, "alignment"},
    {Kind::Paragraph, 2, "paragraph dialog"},
    {Kind::UndoRedo, 6, "undo/redo"},
    {Kind::Save, 2, "save"},
    {Kind::Reopen, 1, "save, close, reopen"},
};

// One run of the window half.
class Run_ {
 public:
  Run_(std::uint64_t seed, int run, int ops, const std::string& home, const std::string& base_path)
      : rng_(seed ^ (0x9E3779B97F4A7C15ull * static_cast<std::uint64_t>(run + 1))),
        seed_(seed),
        run_(run),
        ops_(ops),
        home_(home)
  {
    path_ = Glib::build_filename(home, "run-" + std::to_string(run) + ".rtf");
    base_bytes_ = read_file(base_path);
    Glib::file_set_contents(path_, base_bytes_);
  }

  // Nine checks.
  void go()
  {
    reopen_window();
    for (step_ = 0; step_ < ops_; ++step_)
      one_op();
    ending();
    if (failed_)
      write_failure();
  }

 private:
  std::uint64_t r()
  {
    return rng_();
  }

  void fail(const std::string& what)
  {
    if (!failed_) {
      std::cerr << "undo-fuzz FAILED: seed 0x" << std::hex << seed_ << std::dec << " run " << run_
                << " step " << step_ << " op " << op_ << " (" << detail_.str() << "): " << what
                << "\n  reference depth " << stack_.size() - 1 << ", redo " << redo_.size()
                << ", undo steps " << MainWindowProbe::steps(*w_) << ", dirty "
                << MainWindowProbe::dirty(*w_) << "\n  top step:\n"
                << MainWindowProbe::top(*w_) << "  ops so far:\n"
                << ops_log_ << "  rtf now:\n"
                << writeit::rtf_export(MainWindowProbe::doc(*w_)).substr(0, 3000) << "\n";
      failure_ = what;
    }
    failed_ = true;
  }

  void reopen_window()
  {
    w_.reset();
    settle();
    w_ = std::make_unique<writeit::MainWindow>();
    w_->show();
    settle();
    MainWindowProbe::open(*w_, path_);
    settle();
    stack_.clear();
    redo_.clear();
    stack_.push_back(record());
    saved_serial_ = stack_.back().serial;
    last_insert_end_ = last_back_at_ = last_delete_at_ = -1;
  }

  State record()
  {
    State state;
    state.doc = MainWindowProbe::doc(*w_);
    state.rtf = writeit::rtf_export(state.doc);
    state.serial = next_serial_++;
    return state;
  }

  bool same(const State& a, const State& b)
  {
    return a.doc == b.doc && a.rtf == b.rtf;
  }

  // The window's mark against the reference: clean exactly at the saved
  // state, and the title saying the same.
  void check_mark(const char* where)
  {
    const bool expect = stack_.back().serial != saved_serial_;
    const bool title = w_->get_title().find('*') != Glib::ustring::npos;
    if (MainWindowProbe::dirty(*w_) != expect || title != expect)
      fail(std::string("dirty mark wrong ") + where + ": expected " + (expect ? "dirty" : "clean"));
  }

  // After an edit action: no step if nothing changed, one step, or a merge
  // allowed by the burst rules.
  enum class Burst { None, Insert, Back, Delete };
  void after_edit(size_t steps_before, const State& before, Burst burst, int caret_before)
  {
    settle();
    State now = record();
    const size_t steps = MainWindowProbe::steps(*w_);
    if (MainWindowProbe::stray(*w_) != 0)
      fail("buffer changed outside an undo step");
    if (MainWindowProbe::doubled(*w_) >= 0)
      ++doubled_actions;
    if (!MainWindowProbe::buffer(*w_)->get_has_selection() && !MainWindowProbe::buttons_agree(*w_))
      fail("B/I/U buttons disagree with the format typed next");
    if (same(now, before)) {
      if (steps != steps_before)
        fail("an action that changed nothing added an undo step");
      // As the snapshot undo did, a user action forgets redo even when it
      // changes nothing.
      if (!MainWindowProbe::can_redo(*w_))
        redo_.clear();
      // Nor does it end a burst: typing on still joins the step below.
      check_mark("after a no-op");
      return;
    }
    if (MainWindowProbe::can_redo(*w_))
      fail("an edit kept redo");
    redo_.clear();
    if (steps == steps_before + 1) {
      now.caret_before = caret_before;
      stack_.push_back(now);
    } else if (steps == steps_before && steps > 0) {
      const bool allowed = (burst == Burst::Insert && caret_before == last_insert_end_) ||
                           (burst == Burst::Back && caret_before == last_back_at_) ||
                           (burst == Burst::Delete && caret_before == last_delete_at_);
      if (!allowed)
        fail("an edit joined the top undo step outside a burst");
      now.serial = next_serial_++;
      now.caret_before = stack_.back().caret_before;
      stack_.back() = now;
    } else {
      fail("undo steps went from " + std::to_string(steps_before) + " to " + std::to_string(steps));
      stack_.push_back(now);
    }
    const int caret = MainWindowProbe::caret(*w_);
    reset_bursts(burst);
    if (burst == Burst::Insert)
      last_insert_end_ = caret;
    else if (burst == Burst::Back)
      last_back_at_ = caret;
    else if (burst == Burst::Delete)
      last_delete_at_ = caret;
    check_mark("after an edit");
  }

  void reset_bursts(Burst keep)
  {
    if (keep != Burst::Insert)
      last_insert_end_ = -1;
    if (keep != Burst::Back)
      last_back_at_ = -1;
    if (keep != Burst::Delete)
      last_delete_at_ = -1;
  }

  // Undo or redo through the keys, against the reference.
  void undo_key(bool undo, const char* where)
  {
    const bool could = undo ? MainWindowProbe::can_undo(*w_) : MainWindowProbe::can_redo(*w_);
    const bool should = undo ? stack_.size() > 1 : !redo_.empty();
    if (could != should) {
      fail(std::string(undo ? "Undo" : "Redo") + " sensitivity disagrees with the reference " +
           where);
      return;
    }
    const State before = record();
    key(*w_, undo ? GDK_KEY_z : GDK_KEY_y, GDK_CONTROL_MASK);
    settle();
    const State now = record();
    if (!should) {
      if (!same(now, before))
        fail(std::string("a refused ") + (undo ? "undo" : "redo") + " changed the document");
      return;
    }
    if (undo) {
      if (stack_.back().caret_before >= 0 &&
          MainWindowProbe::caret(*w_) != stack_.back().caret_before)
        fail("undo put the caret at " + std::to_string(MainWindowProbe::caret(*w_)) + ", not " +
             std::to_string(stack_.back().caret_before) + " where the step began " + where);
      redo_.push_back(stack_.back());
      stack_.pop_back();
    } else {
      stack_.push_back(redo_.back());
      redo_.pop_back();
    }
    if (!same(now, stack_.back()))
      fail(std::string(undo ? "undo" : "redo") + " gave another document " + where + "\n" +
           around(stack_.back().rtf, now.rtf));
    if (MainWindowProbe::stray(*w_) != 0)
      fail("buffer changed outside an undo step");
    reset_bursts(Burst::None);
    // Typing on after undo may still join the step now on top, as before.
    check_mark(where);
  }

  // A caret position or range, chosen as the spec says.
  int target()
  {
    const Document doc = MainWindowProbe::doc(*w_);
    std::vector<int> starts;
    std::vector<int> ends;
    std::vector<int> bounds;
    std::vector<int> empties;
    std::vector<int> items;
    int at = 0;
    for (const Paragraph& p : doc.paragraphs) {
      starts.push_back(at);
      int len = 0;
      for (const Run& x : p.runs) {
        len += static_cast<int>(Glib::ustring(x.text).length());
        bounds.push_back(at + len);
      }
      if (len == 0)
        empties.push_back(at);
      if (p.list.kind != ListKind::None)
        items.push_back(at);
      ends.push_back(at + len);
      at += len + 1;
    }
    const int total = MainWindowProbe::length(*w_);
    auto pick = [&](const std::vector<int>& from) {
      return from.empty() ? static_cast<int>(r() % static_cast<std::uint64_t>(total + 1))
                          : from[r() % from.size()];
    };
    switch (r() % 7) {
      case 0:
        return static_cast<int>(r() % static_cast<std::uint64_t>(total + 1));
      case 1:
        return pick(bounds);
      case 2:
        return pick(starts);
      case 3:
        return pick(ends);
      case 4:
        return r() % 2 ? 0 : total;
      case 5:
        return pick(empties);
      default:
        return items.empty() ? pick(starts) : (r() % 2 ? items.front() : items.back());
    }
  }

  void range(int& a, int& b)
  {
    const int total = MainWindowProbe::length(*w_);
    if (r() % 7 == 4) {
      a = 0;
      b = total;
      return;
    }
    a = target();
    b = target();
    if (a > b)
      std::swap(a, b);
    if (a == b && b < total)
      ++b;
  }

  int item_start(bool& found)
  {
    const Document doc = MainWindowProbe::doc(*w_);
    std::vector<int> items;
    int at = 0;
    for (const Paragraph& p : doc.paragraphs) {
      if (p.list.kind != ListKind::None)
        items.push_back(at);
      int len = 0;
      for (const Run& x : p.runs)
        len += static_cast<int>(Glib::ustring(x.text).length());
      at += len + 1;
    }
    found = !items.empty();
    return found ? items[r() % items.size()] : target();
  }

  int paragraph_end_of(int start)
  {
    auto buffer = MainWindowProbe::doc(*w_);
    int at = 0;
    for (const Paragraph& p : buffer.paragraphs) {
      int len = 0;
      for (const Run& x : p.runs)
        len += static_cast<int>(Glib::ustring(x.text).length());
      if (at == start)
        return at + len;
      at += len + 1;
    }
    return start;
  }

  // One key or action of an edit op, recorded on its own.
  template <class F>
  void edit(Burst burst, F act)
  {
    const size_t steps = MainWindowProbe::steps(*w_);
    const State before = record();
    before_rtf_ = before.rtf;
    const int caret = MainWindowProbe::caret(*w_);
    act();
    after_edit(steps, before, burst, caret);
  }

  void one_op()
  {
    int total_weight = 0;
    for (const Weighted& op : kOps)
      total_weight += op.weight;
    int roll = static_cast<int>(r() % static_cast<std::uint64_t>(total_weight));
    Kind kind = Kind::Type;
    for (const Weighted& op : kOps) {
      if (roll < op.weight) {
        kind = op.kind;
        op_ = op.name;
        break;
      }
      roll -= op.weight;
    }
    detail_.str("");
    std::ostringstream& detail = detail_;
    switch (kind) {
      case Kind::Type: {
        const int at = target();
        const int count = 1 + static_cast<int>(r() % 8);
        detail << "at " << at << ":";
        MainWindowProbe::place(*w_, at);
        for (int i = 0; i < count; ++i) {
          const std::uint64_t c = r() % 30;
          guint keyval = c < 26 ? GDK_KEY_a + static_cast<guint>(c) : GDK_KEY_space;
          if (c == 26)
            keyval = gdk_unicode_to_keyval(0xE9);  // é
          else if (c == 27)
            keyval = gdk_unicode_to_keyval(0x20AC);  // €
          detail << ' ' << keyval;
          edit(Burst::Insert, [&] { key(*w_, keyval); });
        }
        break;
      }
      case Kind::DelBack: {
        const int at = target();
        const bool back = r() % 2;
        const int count = 1 + static_cast<int>(r() % 3);
        detail << (back ? "backspace" : "delete") << " x" << count << " at " << at;
        MainWindowProbe::place(*w_, at);
        for (int i = 0; i < count; ++i)
          edit(back ? Burst::Back : Burst::Delete,
               [&] { key(*w_, back ? GDK_KEY_BackSpace : GDK_KEY_Delete); });
        break;
      }
      case Kind::DeleteRange:
      case Kind::Cut: {
        int a = 0;
        int b = 0;
        range(a, b);
        detail << '[' << a << ", " << b << ')';
        MainWindowProbe::select(*w_, a, b);
        edit(Burst::None, [&] {
          if (kind == Kind::Cut)
            key(*w_, GDK_KEY_x, GDK_CONTROL_MASK);
          else
            key(*w_, GDK_KEY_Delete);
        });
        break;
      }
      case Kind::CopyPaste: {
        int a = 0;
        int b = 0;
        range(a, b);
        if (a == b)
          break;
        MainWindowProbe::select(*w_, a, b);
        key(*w_, GDK_KEY_c, GDK_CONTROL_MASK);
        auto clipboard = Gtk::Clipboard::get();
        wait_for([&] {
          return clipboard->wait_is_rich_text_available(MainWindowProbe::buffer(*w_)) ||
                 clipboard->wait_is_text_available();
        });
        wait_for([&] { return MainWindowProbe::paste_ready(*w_); });
        const int at = target();
        detail << "copy [" << a << ", " << b << ") paste at " << at;
        MainWindowProbe::place(*w_, at);
        paste(Burst::Insert);
        break;
      }
      case Kind::PasteOver: {
        int a = 0;
        int b = 0;
        range(a, b);
        detail << "over [" << a << ", " << b << ')';
        if (!MainWindowProbe::paste_ready(*w_) || a == b)
          break;
        MainWindowProbe::select(*w_, a, b);
        paste(Burst::None);
        break;
      }
      case Kind::Flag: {
        const guint keys[] = {GDK_KEY_b, GDK_KEY_i, GDK_KEY_u};
        const guint which = keys[r() % 3];
        if (r() % 2) {
          int a = 0;
          int b = 0;
          range(a, b);
          detail << "ctrl+" << which << " [" << a << ", " << b << ')';
          MainWindowProbe::select(*w_, a, b);
        } else {
          const int at = target();
          detail << "ctrl+" << which << " at " << at;
          MainWindowProbe::place(*w_, at);
        }
        edit(Burst::None, [&] { key(*w_, which, GDK_CONTROL_MASK); });
        break;
      }
      case Kind::Size: {
        const char* sizes[] = {"8", "10.5", "11", "12", "16", "72"};
        const char* size = sizes[r() % 6];
        int a = 0;
        int b = 0;
        range(a, b);
        detail << size << " [" << a << ", " << b << ')';
        MainWindowProbe::select(*w_, a, b);
        edit(Burst::None, [&] { MainWindowProbe::size_box(*w_, size); });
        break;
      }
      case Kind::Font: {
        const char* fonts[] = {"Sans", "Serif", "Monospace"};
        const char* font = fonts[r() % 3];
        int a = 0;
        int b = 0;
        range(a, b);
        detail << font << " [" << a << ", " << b << ')';
        MainWindowProbe::select(*w_, a, b);
        edit(Burst::None, [&] { MainWindowProbe::font_box(*w_, font); });
        break;
      }
      case Kind::Style: {
        const char* styles[] = {"Normal", "Heading 1", "Heading 2", "Heading 3", "Block Text"};
        const char* style = styles[r() % 5];
        int a = 0;
        int b = 0;
        range(a, b);
        detail << style << " [" << a << ", " << b << ')';
        MainWindowProbe::select(*w_, a, b);
        edit(Burst::None, [&] { MainWindowProbe::style_box(*w_, style); });
        break;
      }
      case Kind::ListToggle: {
        const bool numbering = r() % 2;
        bool found = false;
        const int start = r() % 2 ? item_start(found) : target();
        int end = start;
        const int extra = static_cast<int>(r() % 4);
        for (int i = 0; i < extra; ++i)
          end = std::min(MainWindowProbe::length(*w_), paragraph_end_of(end) + 1);
        detail << (numbering ? "numbering" : "bullets") << " [" << start << ", " << end << ')';
        MainWindowProbe::select(*w_, start, end);
        edit(Burst::None, [&] { MainWindowProbe::toggle(*w_, numbering); });
        break;
      }
      case Kind::Tab: {
        bool found = false;
        const int at = item_start(found);
        const bool up = r() % 2;
        detail << (up ? "shift+tab" : "tab") << " at " << at;
        MainWindowProbe::place(*w_, at);
        // Off a list item, Tab types a tab.
        edit(Burst::Insert, [&] {
          if (up)
            key(*w_, GDK_KEY_ISO_Left_Tab, GDK_SHIFT_MASK);
          else
            key(*w_, GDK_KEY_Tab);
        });
        break;
      }
      case Kind::ListEnter: {
        bool found = false;
        const int at = paragraph_end_of(item_start(found));
        detail << "at " << at;
        MainWindowProbe::place(*w_, at);
        edit(Burst::Insert, [&] { key(*w_, GDK_KEY_Return); });
        break;
      }
      case Kind::ListBackspace: {
        bool found = false;
        const int at = item_start(found);
        detail << "at " << at;
        MainWindowProbe::place(*w_, at);
        edit(Burst::Back, [&] { key(*w_, GDK_KEY_BackSpace); });
        break;
      }
      case Kind::Enter: {
        const int at = target();
        detail << "at " << at;
        MainWindowProbe::place(*w_, at);
        edit(Burst::Insert, [&] { key(*w_, GDK_KEY_Return); });
        break;
      }
      case Kind::Align: {
        const int which = static_cast<int>(r() % 4);
        int a = 0;
        int b = 0;
        range(a, b);
        detail << which << " [" << a << ", " << b << ')';
        MainWindowProbe::select(*w_, a, b);
        edit(Burst::None, [&] { MainWindowProbe::align(*w_, which); });
        break;
      }
      case Kind::Paragraph: {
        const int field = static_cast<int>(r() % 3);
        const char* values[] = {"0", "0.5", "1", "1.5"};
        const char* value = values[r() % 4];
        const int at = target();
        detail << "field " << field << " = " << value << " at " << at;
        MainWindowProbe::place(*w_, at);
        edit(Burst::None, [&] { paragraph_dialog(field, value); });
        break;
      }
      case Kind::UndoRedo: {
        const bool undo = r() % 2;
        const int count = 1 + static_cast<int>(r() % 3);
        detail << (undo ? "undo" : "redo") << " x" << count;
        for (int i = 0; i < count; ++i)
          undo_key(undo, "in the middle");
        break;
      }
      case Kind::Save:
        save("in the middle");
        break;
      case Kind::Reopen: {
        save("before closing");
        const State saved = stack_.back();
        reopen_window();
        if (!same(stack_.back(), saved))
          fail("the reopened file is not what was saved\n" + around(saved.rtf, stack_.back().rtf));
        if (MainWindowProbe::dirty(*w_) || MainWindowProbe::can_undo(*w_) ||
            MainWindowProbe::can_redo(*w_))
          fail("the reopened file is dirty or has undo or redo");
        break;
      }
    }
    ops_log_ += std::to_string(step_) + " " + op_ + " " + detail_.str() + "\n";
  }

  void paste(Burst burst)
  {
    edit(burst, [&] {
      const int before = MainWindowProbe::length(*w_);
      const std::string text = MainWindowProbe::buffer(*w_)->get_text();
      key(*w_, GDK_KEY_v, GDK_CONTROL_MASK);
      wait_for([&] {
        return MainWindowProbe::length(*w_) != before ||
               MainWindowProbe::buffer(*w_)->get_text() != text;
      });
    });
  }

  void paragraph_dialog(int field, const char* value)
  {
    const char* labels[] = {"_Left:", "_Right:", "B_y:"};
    writeit::MainWindow* window = w_.get();
    int rounds = 0;
    sigc::connection poll = Glib::signal_timeout().connect(
        [&, window] {
          ++rounds;
          for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
            auto* message = dynamic_cast<Gtk::MessageDialog*>(top);
            if (message && message->get_visible()) {
              // Refused: say OK, then leave the dialog.
              message->response(Gtk::RESPONSE_OK);
              field = -1;
              return true;
            }
          }
          for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
            auto* dialog = dynamic_cast<Gtk::Dialog*>(top);
            if (!dialog || dynamic_cast<Gtk::MessageDialog*>(dialog) || !dialog->get_visible() ||
                dialog->get_transient_for() != window || dialog->get_title() != "Paragraph")
              continue;
            if (field < 0 || rounds > 50) {
              dialog->response(Gtk::RESPONSE_CANCEL);
              return false;
            }
            Gtk::SpinButton* spin = nullptr;
            Gtk::ComboBoxText* special = nullptr;
            std::vector<Gtk::Widget*> todo = {dialog};
            while (!todo.empty()) {
              Gtk::Widget* widget = todo.back();
              todo.pop_back();
              if (auto* label = dynamic_cast<Gtk::Label*>(widget)) {
                if (label->get_label() == labels[field])
                  spin = dynamic_cast<Gtk::SpinButton*>(label->get_mnemonic_widget());
                if (label->get_label() == "_Special:")
                  special = dynamic_cast<Gtk::ComboBoxText*>(label->get_mnemonic_widget());
              }
              if (auto* container = dynamic_cast<Gtk::Container*>(widget))
                for (Gtk::Widget* child : container->get_children())
                  todo.push_back(child);
            }
            if (field == 2 && special && special->get_active_row_number() == 0)
              special->set_active(1);
            if (spin)
              spin->set_text(value);
            dialog->response(Gtk::RESPONSE_OK);
            return true;
          }
          return true;
        },
        20);
    MainWindowProbe::paragraph(*w_);
    poll.disconnect();
  }

  void save(const char* where)
  {
    key(*w_, GDK_KEY_s, GDK_CONTROL_MASK);
    settle();
    saved_serial_ = stack_.back().serial;
    const std::string bytes = read_file(path_);
    if (bytes != stack_.back().rtf)
      fail(std::string("the saved file is not the document's RTF ") + where);
    check_mark(where);
  }

  void ending()
  {
    CHECK(!failed_);
    // Undo all: every state of the reference in reverse.
    bool undo_ok = !failed_;
    while (MainWindowProbe::can_undo(*w_) && !failed_)
      undo_key(true, "in undo-all");
    undo_ok = undo_ok && !failed_ && stack_.size() == 1;
    CHECK(undo_ok);
    const State first = record();
    CHECK(same(first, stack_.front()));
    // Saved now, the file is the starting file (or the reopened one).
    key(*w_, GDK_KEY_s, GDK_CONTROL_MASK);
    settle();
    saved_serial_ = stack_.back().serial;
    CHECK(read_file(path_) == stack_.front().rtf);
    bool redo_ok = !failed_;
    while (MainWindowProbe::can_redo(*w_) && !failed_)
      undo_key(false, "in redo-all");
    redo_ok = redo_ok && !failed_ && redo_.empty();
    CHECK(redo_ok);
    const State last = record();
    CHECK(same(last, stack_.back()));
    key(*w_, GDK_KEY_s, GDK_CONTROL_MASK);
    settle();
    CHECK(read_file(path_) == last.rtf);
    CHECK(MainWindowProbe::stray(*w_) == 0);
    CHECK(!MainWindowProbe::dirty(*w_));
    w_.reset();
    settle();
  }

  void write_failure()
  {
    const char* root = g_getenv("MESON_BUILD_ROOT");
    std::ostringstream name;
    name << "undo-fuzz-failure-0x" << std::hex << seed_ << std::dec << "-" << run_ << ".txt";
    const std::string file =
        Glib::build_filename(root && *root ? root : Glib::get_tmp_dir(), name.str());
    std::ofstream out(file);
    out << "seed 0x" << std::hex << seed_ << std::dec << "\nrun " << run_ << "\nops " << ops_
        << "\nfailure " << failure_ << "\n--- ops\n"
        << ops_log_ << "--- rtf before the failing action\n"
        << before_rtf_ << "\n--- starting rtf\n"
        << base_bytes_ << "\n";
    std::cerr << "undo-fuzz: wrote " << file << "\n";
  }

  std::mt19937_64 rng_;
  std::uint64_t seed_;
  int run_;
  int ops_;
  std::string home_;
  std::string path_;
  std::string base_bytes_;
  std::unique_ptr<writeit::MainWindow> w_;
  std::vector<State> stack_;
  std::vector<State> redo_;
  std::uint64_t saved_serial_ = 0;
  std::uint64_t next_serial_ = 1;
  int last_insert_end_ = -1;
  int last_back_at_ = -1;
  int last_delete_at_ = -1;
  int step_ = 0;
  std::string op_ = "load";
  std::ostringstream detail_;
  std::string ops_log_;
  std::string failure_;
  std::string before_rtf_;
  bool failed_ = false;
};

// The cap: 220 separate edits leave 200 undos, and undoing them all ends at
// the state after the 20th edit. Two checks.
void cap(const std::string& home, const std::string& base_path)
{
  const std::string path = Glib::build_filename(home, "cap.rtf");
  Glib::file_set_contents(path, read_file(base_path));
  writeit::MainWindow w;
  w.show();
  settle();
  MainWindowProbe::open(w, path);
  settle();
  std::vector<std::string> states = {writeit::rtf_export(MainWindowProbe::doc(w))};
  for (int i = 0; i < static_cast<int>(writeit::UndoHistory::kCap) + 20; ++i) {
    // A moved caret each time: never a burst.
    MainWindowProbe::place(w, i % 2 ? 0 : MainWindowProbe::length(w));
    key(w, GDK_KEY_a + static_cast<guint>(i % 26));
    settle();
    states.push_back(writeit::rtf_export(MainWindowProbe::doc(w)));
  }
  int undos = 0;
  while (MainWindowProbe::can_undo(w) && undos < 1000) {
    key(w, GDK_KEY_z, GDK_CONTROL_MASK);
    settle();
    ++undos;
  }
  CHECK(undos == static_cast<int>(writeit::UndoHistory::kCap));
  CHECK(writeit::rtf_export(MainWindowProbe::doc(w)) == states[20]);
}

// --- The model half: a bare buffer and an UndoHistory. ---

std::string serialise(const Glib::RefPtr<Gtk::TextBuffer>& buffer)
{
  std::string out;
  for (auto it = buffer->begin(); !it.is_end(); ++it) {
    out += Glib::ustring(1, it.get_char()).raw();
    out += '[';
    for (const auto& tag : it.get_tags())
      out += tag->property_name().get_value() + ",";
    out += ']';
  }
  return out;
}

// Two checks per run.
void model_run(std::uint64_t seed, int run, int ops)
{
  std::mt19937_64 rng(seed ^ (0xD1B54A32D192ED03ull * static_cast<std::uint64_t>(run + 1)));
  auto table = Gtk::TextTagTable::create();
  std::vector<Glib::RefPtr<Gtk::TextTag>> tags;
  for (const char* name : {"bold", "italic", "para-a", "para-b", "screen"}) {
    auto tag = Gtk::TextTag::create(name);
    table->add(tag);
    tags.push_back(tag);
  }
  auto buffer = Gtk::TextBuffer::create(table);
  buffer->set_text("Model text\nwith two paragraphs\n");
  writeit::UndoHistory history;
  // "screen" plays the list tags: never recorded, and so not compared.
  history.attach(buffer, [](const Glib::RefPtr<Gtk::TextTag>& tag) {
    return tag->property_name().get_value() == "screen";
  });
  auto plain = [&] {
    std::string s = serialise(buffer);
    std::string out;
    // Drop the screen tag before comparing.
    const std::string mark = "screen,";
    for (size_t i = 0; i < s.size();) {
      if (s.compare(i, mark.size(), mark) == 0)
        i += mark.size();
      else
        out += s[i++];
    }
    return out;
  };
  std::vector<std::string> states = {plain()};
  for (int step = 0; step < ops; ++step) {
    history.open(0, false);
    const int edits = 1 + static_cast<int>(rng() % 4);
    for (int e = 0; e < edits; ++e) {
      const int total = buffer->get_char_count();
      const int a = static_cast<int>(rng() % static_cast<std::uint64_t>(total + 1));
      const int b = std::min(total, a + static_cast<int>(rng() % 6));
      switch (rng() % 5) {
        case 0: {
          const char* texts[] = {"x", "ab", "\n", "é€", "word "};
          buffer->insert(buffer->get_iter_at_offset(a), texts[rng() % 5]);
          break;
        }
        case 1:
          buffer->erase(buffer->get_iter_at_offset(a), buffer->get_iter_at_offset(b));
          break;
        case 2:
          buffer->apply_tag(tags[rng() % tags.size()], buffer->get_iter_at_offset(a),
                            buffer->get_iter_at_offset(b));
          break;
        case 3:
          buffer->remove_tag(tags[rng() % tags.size()], buffer->get_iter_at_offset(a),
                             buffer->get_iter_at_offset(b));
          break;
        default:
          buffer->remove_all_tags(buffer->get_iter_at_offset(a), buffer->get_iter_at_offset(b));
          break;
      }
    }
    if (history.close(0, false) != writeit::UndoHistory::Closed::Dropped)
      states.push_back(plain());
    else if (plain() != states.back())
      states.push_back("<changed with no step>");
  }
  bool undo_ok = true;
  for (size_t i = states.size() - 1; i > 0; --i) {
    history.undo(0);
    undo_ok = undo_ok && plain() == states[i - 1];
  }
  undo_ok = undo_ok && !history.can_undo();
  CHECK(undo_ok);
  bool redo_ok = true;
  for (size_t i = 1; i < states.size(); ++i) {
    history.redo(0);
    redo_ok = redo_ok && plain() == states[i];
  }
  CHECK(redo_ok);
  if (!undo_ok || !redo_ok)
    std::cerr << "undo-fuzz model FAILED: seed 0x" << std::hex << seed << std::dec << " run " << run
              << "\n";
}

}  // namespace

int main(int argc, char* argv[])
{
  std::uint64_t seed = env_number("UNDO_FUZZ_SEED", 0x5752495445495431ull);
  int runs = static_cast<int>(env_number("UNDO_FUZZ_RUNS", 200));
  int ops = static_cast<int>(env_number("UNDO_FUZZ_OPS", 60));
  int only_run = -1;
  if (const char* replay = g_getenv("UNDO_FUZZ_REPLAY")) {
    std::ifstream in(replay);
    std::string word;
    while (in >> word) {
      if (word == "seed")
        in >> std::hex >> seed >> std::dec;
      else if (word == "run")
        in >> only_run;
      else if (word == "ops")
        in >> ops;
      else if (word == "---")
        break;
    }
  }
  const int first = only_run >= 0 ? only_run : 0;
  const int last = only_run >= 0 ? only_run + 1 : runs;
  const int count = last - first;

  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-undo-fuzz-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  const bool display = gtk_init_check(&argc, &argv);
  std::unique_ptr<Gtk::Main> kit;
  if (display) {
    kit = std::make_unique<Gtk::Main>(argc, argv);
  } else {
    Glib::init();
    Gtk::Main::init_gtkmm_internals();
  }

  // The model half first: it needs no display.
  for (int run = first; run < last; ++run)
    model_run(seed, run, ops);
  if (!display) {
    std::cout << "undo-fuzz: no display, window half skipped\n";
    return suite_test::done("undo-fuzz", count * 2);
  }
  for (const char* domain : {"Gtk", "Gdk", "GLib-GObject", "GLib", "Pango", "glibmm", "gtkmm"})
    g_log_set_handler(domain,
                      static_cast<GLogLevelFlags>(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING),
                      count_log, nullptr);

  // The starting files, each saved once by Write-It; reopened, each saves
  // back byte for byte.
  std::vector<std::string> bases;
  for (int kind = 0; kind < 3; ++kind) {
    const std::string generated = Glib::build_filename(home, "gen" + std::to_string(kind) + ".rtf");
    const std::string base = Glib::build_filename(home, "base" + std::to_string(kind) + ".rtf");
    Glib::file_set_contents(generated, writeit::rtf_export(starting(kind)));
    {
      writeit::MainWindow w;
      w.show();
      settle();
      MainWindowProbe::open(w, generated);
      settle();
      Glib::file_set_contents(base, writeit::rtf_export(MainWindowProbe::doc(w)));
    }
    settle();
    {
      writeit::MainWindow w;
      w.show();
      settle();
      MainWindowProbe::open(w, base);
      settle();
      CHECK(writeit::rtf_export(MainWindowProbe::doc(w)) == read_file(base));
    }
    settle();
    bases.push_back(base);
  }

  const gint64 began = g_get_monotonic_time();
  for (int run = first; run < last; ++run) {
    Run_ one(seed, run, ops, home, bases[static_cast<size_t>(run % 3)]);
    one.go();
  }
  std::cout << "  " << count << " runs of " << ops << " ops in "
            << (g_get_monotonic_time() - began) / 1000000.0 << " s; " << doubled_actions
            << " actions left a character with two formats of a kind (paste, as on m2)\n";
  cap(home, bases[1]);
  CHECK(criticals == 0);
  return suite_test::done("undo-fuzz", count * 9 + count * 2 + 6);
}
