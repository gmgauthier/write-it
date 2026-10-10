/* SPDX-License-Identifier: Unlicense */

// The real window, under a display. capture() copies the whole document, so
// a keystroke must not call it for the title, the list labels it draws, or
// the centred-item shifts: in a 1,000-paragraph document each copy costs a
// keystroke real time. Typing one character may capture only for undo's
// snapshot and its compare (on_user_begin and on_user_end, until undo
// records operations instead), so at most 2 in all, and a redraw with
// nothing changed captures nothing.
//
// Each document is opened, the caret placed in the middle, and a key pressed
// as the keyboard sends it; the count covers every idle and draw it causes.
//
// A document with no list does no list work on a draw at all.
//
// The labels and the centred-item shifts read each paragraph's format and
// number from list_lines() instead, built from the paragraph tags. So here
// too: after each edit of a document of awkward lists, list_lines() gives
// what capture() and list_numbers() give, paragraph for paragraph, with the
// same start and the same label font.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  static void open(MainWindow& w, const std::string& path)
  {
    w.open_path(path, MainWindow::OpenKind::Rtf);
  }
  static Gtk::TextView& text(MainWindow& w)
  {
    return w.text_;
  }
  static void place_at_end(MainWindow& w, int line)
  {
    auto iter = w.buffer_->get_iter_at_line(line);
    iter.forward_to_line_end();
    w.buffer_->place_cursor(iter);
  }
  // Whether list_lines() has been built since the document went in.
  static bool lines_built(MainWindow& w)
  {
    return w.list_lines_valid_;
  }
  static long captures(MainWindow& w)
  {
    return w.captures_;
  }
  // list_lines() against capture(): the first difference, or "".
  static std::string compare_lines(MainWindow& w)
  {
    const Document doc = w.capture();
    const std::vector<int> numbers = list_numbers(doc.paragraphs);
    const auto& lines = w.list_lines();
    if (lines.size() != doc.paragraphs.size())
      return "paragraphs " + std::to_string(lines.size()) + " against " +
             std::to_string(doc.paragraphs.size());
    int offset = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
      const Paragraph& p = doc.paragraphs[i];
      const std::string at = "paragraph " + std::to_string(i) + ": ";
      if (!(lines[i].format == para_format(p)))
        return at + "format";
      if (lines[i].number != numbers[i])
        return at + "number " + std::to_string(lines[i].number) + " against " +
               std::to_string(numbers[i]);
      const int start = w.buffer_->get_iter_at_line(lines[i].line).get_offset();
      if (start != offset)
        return at + "starts at " + std::to_string(start) + " against " + std::to_string(offset);
      const Paragraph label = w.label_paragraph(i, start);
      auto label_run = [&w, start](const Paragraph& q) {
        return !q.runs.empty() ? q.runs.front()
               : q.mark        ? *q.mark
                               : w.format_of(w.buffer_->get_iter_at_offset(start));
      };
      if (!same_format(label_run(label), label_run(p)) || !(para_format(label) == para_format(p)))
        return at + "label format";
      for (const Run& run : p.runs)
        offset += static_cast<int>(Glib::ustring(run.text).length());
      offset += 1;
    }
    return "";
  }
  static void type_at(MainWindow& w, int offset, const char* text)
  {
    w.buffer_->place_cursor(w.buffer_->get_iter_at_offset(offset));
    w.buffer_->begin_user_action();
    w.buffer_->insert_interactive_at_cursor(text, true);
    w.buffer_->end_user_action();
  }
  static void erase(MainWindow& w, int from, int to)
  {
    w.buffer_->begin_user_action();
    w.buffer_->erase_interactive(w.buffer_->get_iter_at_offset(from),
                                 w.buffer_->get_iter_at_offset(to), true);
    w.buffer_->end_user_action();
  }
  static int length(MainWindow& w)
  {
    return w.buffer_->get_char_count();
  }
  static void select(MainWindow& w, int from, int to)
  {
    w.buffer_->select_range(w.buffer_->get_iter_at_offset(from), w.buffer_->get_iter_at_offset(to));
  }
  static void numbering(MainWindow& w)
  {
    w.toggle_list_kind(ListKind::Number);
  }
  static void undo(MainWindow& w)
  {
    w.undo();
  }
  static Glib::ustring line_text(MainWindow& w, int line)
  {
    auto start = w.buffer_->get_iter_at_line(line);
    auto end = start;
    end.forward_to_line_end();
    return w.buffer_->get_text(start, end);
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

constexpr int kParagraphs = 200;

void settle()
{
  auto context = Glib::MainContext::get_default();
  for (int round = 0; round < 6; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(20000);
  }
  while (context->pending())
    context->iteration(false);
}

// A key press queued on the window, as the keyboard delivers it.
void press(writeit::MainWindow& window, guint keyval)
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
  gdk_event_put(event);
  gdk_event_free(event);
}

enum class Kind { Plain, Numbered, Centred };

writeit::Document document(Kind kind)
{
  writeit::Document doc;
  for (int i = 0; i < kParagraphs; ++i) {
    writeit::Paragraph p;
    writeit::Run run;
    run.text = "Paragraph " + std::to_string(i + 1) + " holds an ordinary sentence of text.";
    p.runs.push_back(run);
    if (kind != Kind::Plain) {
      p.list.kind = writeit::ListKind::Number;
      p.list.list = 1;
      p.indents = writeit::list_indents(0);
    }
    if (kind == Kind::Centred)
      p.align = writeit::Align::Center;
    doc.paragraphs.push_back(p);
  }
  return doc;
}

const char* name_of(Kind kind)
{
  return kind == Kind::Plain ? "plain" : kind == Kind::Numbered ? "numbered" : "centred list";
}

void measure(writeit::MainWindow& window, const std::string& home, Kind kind)
{
  const std::string path = Glib::build_filename(home, std::string(name_of(kind)) + ".rtf");
  Glib::file_set_contents(path, writeit::rtf_export(document(kind)));
  MainWindowProbe::open(window, path);
  settle();
  MainWindowProbe::text(window).grab_focus();
  MainWindowProbe::place_at_end(window, kParagraphs / 2);
  settle();

  int draws = 0;
  auto counting = MainWindowProbe::text(window).signal_draw().connect(
      [&draws](const Cairo::RefPtr<Cairo::Context>&) {
        ++draws;
        return false;
      },
      false);

  // One character typed.
  long before = MainWindowProbe::captures(window);
  press(window, GDK_KEY_x);
  settle();
  const long typed = MainWindowProbe::captures(window) - before;
  std::cout << "  " << name_of(kind) << ": one key, " << typed << " captures, " << draws
            << " draws\n";
  CHECK(MainWindowProbe::line_text(window, kParagraphs / 2).raw().back() == 'x');
  CHECK(typed <= 2);

  // A redraw with nothing changed.
  draws = 0;
  before = MainWindowProbe::captures(window);
  MainWindowProbe::text(window).queue_draw();
  settle();
  const long redrawn = MainWindowProbe::captures(window) - before;
  std::cout << "  " << name_of(kind) << ": redraw, " << redrawn << " captures, " << draws
            << " draws\n";
  CHECK(draws >= 1);
  CHECK(redrawn == 0);
  // With no list, neither typing nor drawing looked at the paragraphs.
  if (kind == Kind::Plain)
    CHECK(!MainWindowProbe::lines_built(window));
  counting.disconnect();
  g_remove(path.c_str());
}

writeit::Paragraph item(const char* text, writeit::ListKind kind, int list, int level = 0)
{
  writeit::Paragraph p;
  if (*text) {
    writeit::Run run;
    run.text = text;
    p.runs.push_back(run);
  }
  p.list.kind = kind;
  p.list.list = kind == writeit::ListKind::Number ? list : 0;
  p.list.level = level;
  p.indents = writeit::list_indents(level);
  return p;
}

writeit::Document awkward()
{
  using writeit::ListKind;
  writeit::Document doc;
  doc.paragraphs.push_back(item("Plain.", ListKind::None, 0));
  doc.paragraphs.push_back(item("One.", ListKind::Number, 1));
  doc.paragraphs.push_back(item("One, nested.", ListKind::Number, 1, 1));
  doc.paragraphs.push_back(item("Bullet.", ListKind::Bullet, 0));
  // An empty item with a format of its own on its newline.
  doc.paragraphs.push_back(item("", ListKind::Number, 1));
  writeit::Run mark;
  mark.size = 20;
  mark.bold = true;
  doc.paragraphs.back().mark = mark;
  doc.paragraphs.push_back(item("Another list, from 7.", ListKind::Number, 2));
  doc.paragraphs.back().list.start = 7;
  doc.paragraphs.push_back(item("Centred.", ListKind::Number, 2));
  doc.paragraphs.back().align = writeit::Align::Center;
  {
    writeit::Paragraph big = item("Big bold label.", ListKind::Number, 1);
    big.runs.front().size = 18;
    big.runs.front().bold = true;
    doc.paragraphs.push_back(big);
  }
  // An empty last item: its format is held aside, with no tag.
  doc.paragraphs.push_back(item("", ListKind::Number, 1));
  return doc;
}

bool lines_match(writeit::MainWindow& window, const char* step)
{
  settle();
  const std::string diff = MainWindowProbe::compare_lines(window);
  if (!diff.empty())
    std::cerr << "  list_lines() after " << step << ": " << diff << "\n";
  return diff.empty();
}

void compare(const std::string& home)
{
  const std::string path = Glib::build_filename(home, "awkward.rtf");
  Glib::file_set_contents(path, writeit::rtf_export(awkward()));
  writeit::MainWindow window;
  window.show();
  settle();
  MainWindowProbe::open(window, path);
  CHECK(lines_match(window, "open"));
  // Other line separators: GTK lines that are not paragraphs.
  MainWindowProbe::type_at(window, 3, "\r");
  CHECK(lines_match(window, "a carriage return"));
  MainWindowProbe::type_at(window, 12, "\xe2\x80\xa9");
  CHECK(lines_match(window, "a paragraph separator"));
  // Typing into the empty last item, and a new one after it.
  MainWindowProbe::type_at(window, MainWindowProbe::length(window), "Last.");
  CHECK(lines_match(window, "typing in the last item"));
  MainWindowProbe::type_at(window, MainWindowProbe::length(window), "\n");
  CHECK(lines_match(window, "Enter at the end"));
  // A paragraph joined to the next, and numbering turned off and on.
  MainWindowProbe::erase(window, 20, 21);
  CHECK(lines_match(window, "a join"));
  MainWindowProbe::select(window, 0, 2);
  MainWindowProbe::numbering(window);
  CHECK(lines_match(window, "numbering the first paragraph"));
  MainWindowProbe::undo(window);
  CHECK(lines_match(window, "undo"));
  MainWindowProbe::erase(window, 0, MainWindowProbe::length(window));
  CHECK(lines_match(window, "deleting everything"));
  window.hide();
  settle();
  g_remove(path.c_str());
}

}  // namespace

constexpr int kChecks = 22;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-capture-count-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "capture-count: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  // A window each: the one typed in has changes, and Open would ask.
  for (Kind kind : {Kind::Plain, Kind::Numbered, Kind::Centred}) {
    writeit::MainWindow window;
    window.set_default_size(900, 700);
    window.show();
    settle();
    measure(window, home, kind);
    window.hide();
    settle();
  }
  compare(home);
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("capture-count", kChecks);
}
