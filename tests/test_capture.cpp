/* SPDX-License-Identifier: Unlicense */

// The real window, under a display: capture() reads each character's format
// only where a tag starts or ends, or at a paragraph's first character, and
// otherwise carries the run on. The document it builds must be exactly the
// one the old per-character reading built, which this suite keeps as its
// reference, for a varied document, after edits, and at 1000 paragraphs.
// One capture() of 1000 paragraphs must also be quick: in a normal build
// under a fixed bound, and in any build (ASan too) several times quicker
// than the per-character reference, which costs what capture() used to.

#include "check.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  static void load(MainWindow& w, const Document& doc)
  {
    w.replace_buffer(doc, 0);
  }
  static Document capture(MainWindow& w)
  {
    return w.capture();
  }
  static void select(MainWindow& w, int from, int to)
  {
    w.buffer_->select_range(w.buffer_->get_iter_at_offset(from), w.buffer_->get_iter_at_offset(to));
  }
  static void bold(MainWindow& w)
  {
    w.toggle_flag(MainWindow::TextFlag::Bold);
  }
  static void italic(MainWindow& w)
  {
    w.toggle_flag(MainWindow::TextFlag::Italic);
  }
  static void style(MainWindow& w, const char* name)
  {
    w.apply_named_style(name);
  }
  static void type_at(MainWindow& w, int offset, const char* text)
  {
    w.buffer_->place_cursor(w.buffer_->get_iter_at_offset(offset));
    w.buffer_->begin_user_action();
    w.buffer_->insert_interactive_at_cursor(text, true);
    w.buffer_->end_user_action();
  }
  static void caret(MainWindow& w, int offset)
  {
    w.text_.grab_focus();
    w.buffer_->place_cursor(w.buffer_->get_iter_at_offset(offset));
  }
  static void copy(MainWindow& w)
  {
    w.buffer_->copy_clipboard(Gtk::Clipboard::get());
  }
  // Edit > Paste.
  static void paste(MainWindow& w)
  {
    w.paste_item_->activate();
  }
  static void list(MainWindow& w, ListKind kind)
  {
    w.toggle_list_kind(kind);
  }
  static bool renumber(MainWindow& w, bool restart)
  {
    return w.renumber_list(restart);
  }
  static int end(MainWindow& w)
  {
    return w.buffer_->get_char_count();
  }
  static void erase(MainWindow& w, int from, int to)
  {
    w.buffer_->begin_user_action();
    w.buffer_->erase_interactive(w.buffer_->get_iter_at_offset(from),
                                 w.buffer_->get_iter_at_offset(to), true);
    w.buffer_->end_user_action();
  }

  // The old capture(): every character's format and heading read from its
  // tags. Paragraph formats follow para_at(), which keeps capture()'s rule.
  static Document reference(MainWindow& w)
  {
    Document doc;
    Paragraph paragraph;
    int heading = 0;
    int start = 0;
    auto add = [&](const Run& run, gunichar ch) {
      const std::string text = Glib::ustring(1, ch).raw();
      if (!paragraph.runs.empty() && same_format(paragraph.runs.back(), run)) {
        paragraph.runs.back().text += text;
        return;
      }
      Run next = run;
      next.text = text;
      paragraph.runs.push_back(next);
    };
    auto flush = [&]() {
      const ParaFormat format = w.para_at(start);
      paragraph.indents = format.indents;
      paragraph.align = format.align;
      paragraph.list = format.list;
      paragraph.style = format.style;
      paragraph.direct = format.direct;
      paragraph.heading = heading;
      doc.paragraphs.push_back(paragraph);
      paragraph = Paragraph{};
      heading = 0;
    };
    for (auto iter = w.buffer_->begin(); !iter.is_end(); ++iter) {
      const gunichar ch = iter.get_char();
      if (ch == '\n') {
        if (paragraph.runs.empty() && w.has_fmt(iter))
          paragraph.mark = w.format_of(iter);
        flush();
        start = iter.get_offset() + 1;
        continue;
      }
      if (heading == 0)
        heading = w.heading_of(iter);
      add(w.format_of(iter), ch);
    }
    if (paragraph.runs.empty() && w.pending_mark_set_)
      paragraph.mark = w.pending_mark_;
    flush();
    doc.styles = w.styles_;
    return doc;
  }
};

}  // namespace writeit

namespace {

using writeit::Document;
using writeit::ListKind;
using writeit::MainWindow;
using writeit::MainWindowProbe;
using writeit::Paragraph;
using writeit::Run;

// One CHECK in a normal build, the other in a sanitiser build; the count of
// checks stays the same.
#if defined(__SANITIZE_ADDRESS__)
constexpr bool kSanitised = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool kSanitised = true;
#else
constexpr bool kSanitised = false;
#endif
#else
constexpr bool kSanitised = false;
#endif

// One capture() of 1000 paragraphs in a plain build (-O0 -g): about 500 ms
// (plain) and 600 ms (list) reading every character, about 25 ms reading
// only where tags toggle. The bound leaves room for a slower CI machine.
constexpr double kCaptureBoundMs = 100;
// The reference is the old per-character capture(); the new one must be at
// least this many times quicker in any build.
constexpr double kMinSpeedUp = 4;

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

// A real key press, through the window's own handlers, as test_font_size_ui.
void key(MainWindow& window, guint keyval)
{
  GdkEvent* event = gdk_event_new(GDK_KEY_PRESS);
  event->key.window = GDK_WINDOW(g_object_ref(window.get_window()->gobj()));
  event->key.send_event = TRUE;
  event->key.time = GDK_CURRENT_TIME;
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
  settle();
}

Run run(const std::string& text, unsigned what = 0)
{
  Run r;
  r.text = text;
  r.bold = what & 1;
  r.italic = what & 2;
  r.underline = what & 4;
  if (what & 8) {
    r.font = "Serif";
    r.direct |= writeit::kDirectFont;
  }
  if (what & 16) {
    r.size = 14.5;
    r.direct |= writeit::kDirectSize;
  }
  if (what & 7)
    r.direct |= writeit::kDirectBold;
  return r;
}

Paragraph para(std::vector<Run> runs, const char* style = writeit::kNormalStyle, int heading = 0)
{
  Paragraph p;
  p.runs = std::move(runs);
  p.style = style;
  p.heading = heading;
  return p;
}

Paragraph item(std::vector<Run> runs, ListKind kind, int level = 0)
{
  Paragraph p = para(std::move(runs));
  p.list.kind = kind;
  p.list.list = 1;
  p.list.level = level;
  p.indents = writeit::list_indents(level);
  return p;
}

// Bold, italic, underline, font and size changes in mid-paragraph,
// headings, a named style of the document's own, lists (one centred, so a
// screen-only list tag toggles), empty paragraphs with and without a format
// of their own, and a bold tail that ends at the buffer's end. With
// `empty_last`, the last paragraph is instead empty with a format of its own.
Document varied(bool empty_last)
{
  Document doc = writeit::blank_document("Sans", 11);
  writeit::Style quote;
  quote.name = "Quote";
  quote.based_on = writeit::kNormalStyle;
  quote.format.italic = true;
  doc.styles.push_back(quote);
  Paragraph empty_mark;
  Run mark = run("", 1 | 16);
  empty_mark.mark = mark;
  Paragraph centred = item({run("centred "), run("item", 4)}, ListKind::Number);
  centred.align = writeit::Align::Center;
  doc.paragraphs = {
      para({run("A "), run("heading", 2), run(" here")}, "Heading 1", 1),
      para({run("plain "), run("bold", 1), run(" mid "), run("italic", 2), run(" "),
            run("under", 4), run(" "), run("serif", 8), run(" "), run("big", 16), run(" "),
            run("all", 1 | 2 | 4 | 8 | 16), run(" end")}),
      para({}),
      empty_mark,
      item({run("one "), run("bold", 1), run(" item")}, ListKind::Number),
      item({run("two")}, ListKind::Number, 1),
      centred,
      item({run("dot", 2)}, ListKind::Bullet),
      para({run("Quoted "), run("words", 1)}, "Quote"),
      para({run("Second "), run("level", 4)}, "Heading 2", 2),
      para({run("x", 1), run("y", 2), run("z", 4)}),
  };
  if (empty_last)
    doc.paragraphs.push_back(empty_mark);
  else
    doc.paragraphs.push_back(para({run("the "), run("tail", 1)}));
  return doc;
}

Document long_document(bool list)
{
  Document doc = writeit::blank_document("Sans", 11);
  doc.paragraphs.clear();
  for (int i = 0; i < 1000; ++i) {
    Paragraph p = para({run("Paragraph " + std::to_string(i + 1) +
                            " holds an ordinary sentence of text, as a long document does.")});
    if (list) {
      p.list.kind = ListKind::Number;
      p.list.list = 1;
      p.indents = writeit::list_indents(0);
    }
    doc.paragraphs.push_back(p);
  }
  return doc;
}

std::string describe(const Run& r)
{
  return "'" + r.text + "' " + r.font + " " + std::to_string(r.size) + (r.bold ? " b" : "") +
         (r.italic ? " i" : "") + (r.underline ? " u" : "") + " d" + std::to_string(r.direct);
}

// Exactly the same runs (split where the reference splits, with the same
// direct bits), headings, paragraph formats and marks.
bool same_runs(const Document& got, const Document& want)
{
  for (size_t i = 0; i < want.paragraphs.size() && i < got.paragraphs.size(); ++i) {
    const auto& a = got.paragraphs[i].runs;
    const auto& b = want.paragraphs[i].runs;
    bool same = a.size() == b.size();
    for (size_t j = 0; same && j < a.size(); ++j)
      same = a[j].text == b[j].text && writeit::same_format(a[j], b[j]);
    if (!same) {
      std::cerr << "paragraph " << i << " runs differ:";
      for (const Run& r : a)
        std::cerr << " [" << describe(r) << "]";
      std::cerr << " vs";
      for (const Run& r : b)
        std::cerr << " [" << describe(r) << "]";
      std::cerr << "\n";
      return false;
    }
  }
  return true;
}

bool same_paragraphs(const Document& got, const Document& want)
{
  for (size_t i = 0; i < want.paragraphs.size() && i < got.paragraphs.size(); ++i) {
    const Paragraph& a = got.paragraphs[i];
    const Paragraph& b = want.paragraphs[i];
    const bool marks = a.mark.has_value() == b.mark.has_value() &&
                       (!a.mark || writeit::same_format(*a.mark, *b.mark));
    if (a.heading != b.heading || a.style != b.style || !(a.indents == b.indents) ||
        a.align != b.align || !(a.list == b.list) || a.direct != b.direct || !marks) {
      std::cerr << "paragraph " << i << " format differs (heading " << a.heading << " vs "
                << b.heading << ", style " << a.style << " vs " << b.style << ")\n";
      return false;
    }
  }
  return true;
}

// Four checks: the new capture() is the reference, exactly.
void equivalent(MainWindow& w, const char* what)
{
  const Document got = MainWindowProbe::capture(w);
  const Document want = MainWindowProbe::reference(w);
  const bool count = got.paragraphs.size() == want.paragraphs.size();
  if (!count)
    std::cerr << what << ": " << got.paragraphs.size() << " paragraphs vs "
              << want.paragraphs.size() << "\n";
  CHECK(count);
  const bool runs = same_runs(got, want);
  if (!runs)
    std::cerr << what << ": runs differ\n";
  CHECK(runs);
  const bool paragraphs = same_paragraphs(got, want);
  if (!paragraphs)
    std::cerr << what << ": paragraph formats differ\n";
  CHECK(paragraphs);
  CHECK(got == want && got.styles == want.styles);
}

// Where paragraph `index` starts in the buffer now.
int start_of(MainWindow& w, size_t index)
{
  const Document doc = MainWindowProbe::capture(w);
  int offset = 0;
  for (size_t i = 0; i < index && i < doc.paragraphs.size(); ++i) {
    for (const Run& r : doc.paragraphs[i].runs)
      offset += static_cast<int>(Glib::ustring(r.text).length());
    ++offset;
  }
  return offset;
}

// The paragraph index of the `nth` (from 0) numbered item now.
size_t numbered(MainWindow& w, int nth)
{
  const Document doc = MainWindowProbe::capture(w);
  for (size_t i = 0; i < doc.paragraphs.size(); ++i)
    if (doc.paragraphs[i].list.kind == ListKind::Number && nth-- == 0)
      return i;
  return 0;
}

// Five checks: the edit changed the document, and then capture() is still
// the reference. A failure names the edit.
void after_edit(MainWindow& w, const char* what, const std::function<void()>& edit)
{
  const Document before = MainWindowProbe::capture(w);
  edit();
  settle();
  const bool changed = !(MainWindowProbe::capture(w) == before);
  if (!changed)
    std::cerr << what << ": the edit changed nothing\n";
  CHECK(changed);
  equivalent(w, what);
}

double best_ms(const std::function<void()>& work)
{
  double best = 1e9;
  for (int i = 0; i < 5; ++i) {
    const gint64 start = g_get_monotonic_time();
    work();
    best = std::min(best, (g_get_monotonic_time() - start) / 1000.0);
  }
  return best;
}

// Two checks: the bound (normal builds) or nothing more (sanitiser builds,
// counted as passed), and the speed-up over the reference (every build).
void timing(MainWindow& w, const char* what)
{
  size_t sink = 0;
  const double now = best_ms([&]() { sink += MainWindowProbe::capture(w).paragraphs.size(); });
  const double old = best_ms([&]() { sink += MainWindowProbe::reference(w).paragraphs.size(); });
  std::cout << "capture: " << what << " 1000 paragraphs, capture() " << now
            << " ms, per-character reference " << old << " ms" << (kSanitised ? " (ASan)" : "")
            << "\n";
  CHECK(sink == 10000);
  CHECK(kSanitised || now < kCaptureBoundMs);
  CHECK(now * kMinSpeedUp < old);
}

// Eleven edits, five checks each; five loaded documents, four each; two
// round trips; renumbering; two timings, three each.
constexpr int kChecks = 11 * 5 + 5 * 4 + 2 + 1 + 3 * 2;

}  // namespace

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-capture-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "capture: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    MainWindow window;
    window.show();
    settle();

    // The varied document, as loaded, and as read back from a file.
    const Document doc = varied(false);
    MainWindowProbe::load(window, doc);
    settle();
    equivalent(window, "varied");
    CHECK(MainWindowProbe::capture(window) == doc);

    // After each kind of edit.
    after_edit(window, "typing", [&]() {
      MainWindowProbe::caret(window, start_of(window, 1) + 8);
      key(window, GDK_KEY_q);
      MainWindowProbe::type_at(window, start_of(window, 1) + 6, "QQ");
    });
    after_edit(window, "Delete", [&]() {
      MainWindowProbe::caret(window, start_of(window, 1) + 5);
      key(window, GDK_KEY_Delete);
      key(window, GDK_KEY_Delete);
    });
    after_edit(window, "Backspace joining paragraphs", [&]() {
      MainWindowProbe::caret(window, start_of(window, 1));
      key(window, GDK_KEY_BackSpace);
    });
    after_edit(window, "paste", [&]() {
      MainWindowProbe::select(window, start_of(window, 0) + 10, start_of(window, 3) + 4);
      MainWindowProbe::copy(window);
      settle();
      MainWindowProbe::caret(window, start_of(window, 6) + 2);
      MainWindowProbe::paste(window);
    });
    after_edit(window, "named style", [&]() {
      MainWindowProbe::caret(window, start_of(window, 1) + 2);
      MainWindowProbe::style(window, "Quote");
      MainWindowProbe::select(window, start_of(window, 8) + 1, start_of(window, 9) + 1);
      MainWindowProbe::style(window, "Heading 3");
    });
    after_edit(window, "bold over a range", [&]() {
      MainWindowProbe::select(window, start_of(window, 1) + 3, start_of(window, 4) + 3);
      MainWindowProbe::bold(window);
    });
    after_edit(window, "italic over a paragraph break", [&]() {
      MainWindowProbe::select(window, start_of(window, 1) + 2, start_of(window, 2) + 2);
      MainWindowProbe::italic(window);
    });
    after_edit(window, "list on", [&]() {
      MainWindowProbe::caret(window, start_of(window, 1) + 1);
      MainWindowProbe::list(window, ListKind::Number);
    });
    after_edit(window, "list off", [&]() {
      MainWindowProbe::caret(window, start_of(window, numbered(window, 2)) + 1);
      MainWindowProbe::list(window, ListKind::Number);
    });
    bool renumbered = false;
    after_edit(window, "renumbering", [&]() {
      MainWindowProbe::caret(window, start_of(window, numbered(window, 2)) + 1);
      renumbered = MainWindowProbe::renumber(window, true);
    });
    CHECK(renumbered);
    after_edit(window, "typing at the end, in a tag that ends the buffer",
               [&]() { MainWindowProbe::type_at(window, MainWindowProbe::end(window), "!"); });

    // An empty last paragraph with a format of its own.
    const Document last = varied(true);
    MainWindowProbe::load(window, last);
    settle();
    equivalent(window, "empty last");
    CHECK(MainWindowProbe::capture(window) == last);

    // An empty document.
    MainWindowProbe::load(window, writeit::blank_document("Sans", 11));
    settle();
    equivalent(window, "empty");

    // 1000 paragraphs, plain and as one numbered list.
    MainWindowProbe::load(window, long_document(false));
    settle();
    equivalent(window, "plain 1000");
    timing(window, "plain");
    MainWindowProbe::load(window, long_document(true));
    settle();
    equivalent(window, "list 1000");
    timing(window, "list");
    window.hide();
    settle();
  }
  g_rmdir(home.c_str());
  return suite_test::done("capture", kChecks);
}
