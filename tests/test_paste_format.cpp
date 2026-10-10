/* SPDX-License-Identifier: Unlicense */

// The real window, under a display: pasted text keeps the character format
// it was copied with, whatever the format at the caret, and no character
// ever carries two format tags. Every edit is a real key event (typing,
// Ctrl+B, Ctrl+C, Ctrl+V, Ctrl+Z, Ctrl+Y) through the window's own paths,
// except drag and drop, which replays what GtkTextView does on a drop
// within one buffer (a GTK drag cannot be driven under Xvfb).
//
// Each case reads the result three ways, which must agree: the text, what
// GTK draws (CAPS = drawn bold) and what capture() records (CAPS = bold),
// and then assert_single_format_tag() on the buffer.

#include "check.hpp"
#include "format_tags_check.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  static Glib::RefPtr<Gtk::TextBuffer> buffer(MainWindow& w)
  {
    return w.buffer_;
  }
  static void focus(MainWindow& w)
  {
    w.text_.grab_focus();
  }
  static Document capture(MainWindow& w)
  {
    return w.capture();
  }
  static bool paste_ready(MainWindow& w)
  {
    return w.paste_item_->get_sensitive();
  }
  static void size_box(MainWindow& w, const char* size)
  {
    auto* entry = w.size_combo_.get_entry();
    entry->set_text(size);
    entry->activate();
    w.text_.grab_focus();
  }
  static void style(MainWindow& w, const char* name)
  {
    w.apply_named_style(name);
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindow;
using P = writeit::MainWindowProbe;

void settle(int rounds = 10)
{
  auto context = Glib::MainContext::get_default();
  for (int round = 0; round < rounds; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(3000);
  }
}

void key(MainWindow& w, guint keyval, guint state = 0)
{
  GdkEvent* event = gdk_event_new(GDK_KEY_PRESS);
  event->key.window = GDK_WINDOW(g_object_ref(w.get_window()->gobj()));
  event->key.send_event = TRUE;
  event->key.time = GDK_CURRENT_TIME;
  event->key.state = state;
  event->key.keyval = keyval;
  GdkKeymapKey* keys = nullptr;
  gint n = 0;
  if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_for_display(gdk_display_get_default()),
                                        keyval, &keys, &n) &&
      n > 0) {
    event->key.hardware_keycode = keys[0].keycode;
    event->key.group = keys[0].group;
  }
  g_free(keys);
  gdk_event_set_device(event,
                       gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display_get_default())));
  gtk_main_do_event(event);
  gdk_event_free(event);
  settle(2);
}

void type(MainWindow& w, const std::string& text)
{
  for (char c : text)
    key(w, c == '\n' ? GDK_KEY_Return : (c == ' ' ? GDK_KEY_space : gdk_unicode_to_keyval(c)));
}

void ctrl(MainWindow& w, guint keyval)
{
  key(w, keyval, GDK_CONTROL_MASK);
  settle(5);
}

void select(MainWindow& w, int from, int to)
{
  auto buffer = P::buffer(w);
  buffer->select_range(buffer->get_iter_at_offset(from), buffer->get_iter_at_offset(to));
  settle(2);
}

void caret(MainWindow& w, int at)
{
  auto buffer = P::buffer(w);
  buffer->place_cursor(buffer->get_iter_at_offset(at));
  settle(2);
}

void wait_paste_ready(MainWindow& w)
{
  for (int i = 0; i < 400 && !P::paste_ready(w); ++i)
    settle(1);
  settle(5);
}

// Ctrl+C, then the window's Paste turning on.
void copy(MainWindow& w)
{
  Gtk::Clipboard::get()->clear();
  settle(5);
  for (int i = 0; i < 400 && P::paste_ready(w); ++i)
    settle(1);
  ctrl(w, GDK_KEY_c);
  wait_paste_ready(w);
}

// Ctrl+V, then the text changing (the paste answers through the loop).
void paste(MainWindow& w)
{
  const Glib::ustring before = P::buffer(w)->get_text();
  ctrl(w, GDK_KEY_v);
  for (int i = 0; i < 400 && P::buffer(w)->get_text() == before; ++i)
    settle(1);
  settle(5);
}

// What GTK draws: CAPS for a character drawn bold, | for a paragraph end.
std::string on_screen(MainWindow& w)
{
  auto buffer = P::buffer(w);
  std::string shown;
  for (auto iter = buffer->begin(); !iter.is_end(); ++iter) {
    GtkTextAttributes* attrs = gtk_text_attributes_new();
    GtkTextIter raw = *iter.gobj();
    gtk_text_iter_get_attributes(&raw, attrs);
    const bool bold = pango_font_description_get_weight(attrs->font) >= PANGO_WEIGHT_BOLD;
    gtk_text_attributes_unref(attrs);
    const gunichar c = iter.get_char();
    shown += c == '\n' ? std::string("|")
                       : Glib::ustring(1, bold ? g_unichar_toupper(c) : c).raw();
  }
  return shown;
}

// What capture() records, the same way.
std::string captured(MainWindow& w)
{
  const writeit::Document doc = P::capture(w);
  std::string shown;
  for (size_t p = 0; p < doc.paragraphs.size(); ++p) {
    if (p > 0)
      shown += "|";
    for (const writeit::Run& run : doc.paragraphs[p].runs)
      for (char c : run.text)
        shown += run.bold ? static_cast<char>(g_ascii_toupper(c)) : c;
  }
  return shown;
}

// One letter per character from capture(): L for over 11 pt, . for 11 pt.
std::string sizes(MainWindow& w)
{
  const writeit::Document doc = P::capture(w);
  std::string out;
  for (size_t p = 0; p < doc.paragraphs.size(); ++p) {
    if (p > 0)
      out += "|";
    for (const writeit::Run& run : doc.paragraphs[p].runs)
      out += std::string(run.text.size(), run.size > 11 ? 'L' : '.');
  }
  return out;
}

// Four checks: the text, the screen, capture() and the one-tag rule.
void expect(MainWindow& w, const char* name, const std::string& text, const std::string& shown)
{
  const std::string got_text = P::buffer(w)->get_text().raw();
  const std::string got_screen = on_screen(w);
  const std::string got_capture = captured(w);
  if (got_text != text || got_screen != shown || got_capture != shown)
    std::cerr << name << ": text \"" << got_text << "\", screen \"" << got_screen
              << "\", capture \"" << got_capture << "\"; expected \"" << text << "\" shown \""
              << shown << "\"\n";
  CHECK(got_text == text);
  CHECK(got_screen == shown);
  CHECK(got_capture == shown);
  assert_single_format_tag(P::buffer(w));
}

void run(const char* name, const std::function<void(MainWindow&)>& steps)
{
  MainWindow w;
  w.show();
  settle(20);
  P::focus(w);
  steps(w);
  w.hide();
  settle(5);
  std::cout << name << ": done\n";
}

// "one two" with "two" bold by Ctrl+B.
void one_bold_two(MainWindow& w)
{
  type(w, "one two");
  select(w, 4, 7);
  ctrl(w, GDK_KEY_b);
}

// The paste of plain "one" into bold "two" (the reported bug).
void plain_into_bold(MainWindow& w)
{
  one_bold_two(w);
  select(w, 0, 3);
  copy(w);
  caret(w, 5);
  paste(w);
}

}  // namespace

constexpr int kChecks = 87;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-paste-format-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "paste-format: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  // The six cases of the report (/workspace/paste-repro), each in a fresh window.
  // 1. Bold "two" pasted after plain "one": stays bold.
  run("bold-into-plain", [](MainWindow& w) {
    one_bold_two(w);
    select(w, 4, 7);
    copy(w);
    caret(w, 3);
    paste(w);
    expect(w, "bold-into-plain", "onetwo two", "oneTWO TWO");
  });
  // 2. Plain "one" pasted inside bold "two": stays plain.
  run("plain-into-bold", [](MainWindow& w) {
    plain_into_bold(w);
    expect(w, "plain-into-bold", "one tonewo", "one ToneWO");
  });
  // 3. A 16 pt word pasted into 11 pt text: stays 16 pt.
  run("size-into-plain", [](MainWindow& w) {
    type(w, "one two");
    select(w, 4, 7);
    P::size_box(w, "16");
    settle(5);
    select(w, 4, 7);
    copy(w);
    caret(w, 0);
    paste(w);
    expect(w, "size-into-plain", "twoone two", "twoone two");
    CHECK(sizes(w) == "LLL....LLL");
  });
  // 4. Across a paragraph end, into another paragraph.
  run("paragraph-into-other", [](MainWindow& w) {
    type(w, "one\ntwo\nthree");
    select(w, 2, 6);
    copy(w);
    caret(w, 10);
    paste(w);
    expect(w, "paragraph-into-other", "one\ntwo\nthe\ntwree", "one|two|the|twree");
  });
  // 5 and 6: bold typed first, so the bold tag is the older one.
  run("bold-first-bold-into-plain", [](MainWindow& w) {
    ctrl(w, GDK_KEY_b);
    type(w, "one");
    ctrl(w, GDK_KEY_b);
    type(w, " two");
    select(w, 0, 3);
    copy(w);
    caret(w, 7);
    paste(w);
    expect(w, "bold-first-bold-into-plain", "one twoone", "ONE twoONE");
  });
  run("bold-first-plain-into-bold", [](MainWindow& w) {
    ctrl(w, GDK_KEY_b);
    type(w, "one");
    ctrl(w, GDK_KEY_b);
    type(w, " two");
    select(w, 4, 7);
    copy(w);
    caret(w, 1);
    paste(w);
    expect(w, "bold-first-plain-into-bold", "otwone two", "OtwoNE two");
  });

  // Plain text from another program has no format of its own: it takes the
  // caret's, here bold.
  run("other-app-into-bold", [](MainWindow& w) {
    one_bold_two(w);
    Gtk::Clipboard::get()->set_text("xyz");
    wait_paste_ready(w);
    caret(w, 5);
    paste(w);
    expect(w, "other-app-into-bold", "one txyzwo", "one TXYZWO");
  });
  // Into an empty last paragraph after a bold word (the empty line takes
  // bold for typing): the pasted plain word stays plain.
  run("into-empty-paragraph", [](MainWindow& w) {
    one_bold_two(w);
    caret(w, 7);
    type(w, "\n");
    select(w, 0, 3);
    copy(w);
    caret(w, 8);
    paste(w);
    expect(w, "into-empty-paragraph", "one two\none", "one TWO|one");
  });
  // At the start of a run, at the end of one mid-paragraph, and at the start
  // of a paragraph whose newline before has the same format as its text.
  run("start-of-run", [](MainWindow& w) {
    one_bold_two(w);
    select(w, 0, 3);
    copy(w);
    caret(w, 4);
    paste(w);
    expect(w, "start-of-run", "one onetwo", "one oneTWO");
  });
  run("end-of-run", [](MainWindow& w) {
    type(w, "one two three");
    select(w, 4, 7);
    ctrl(w, GDK_KEY_b);
    select(w, 0, 3);
    copy(w);
    caret(w, 7);
    paste(w);
    expect(w, "end-of-run", "one twoone three", "one TWOone three");
  });
  run("start-of-paragraph", [](MainWindow& w) {
    type(w, "one\ntwo three");
    select(w, 8, 13);
    ctrl(w, GDK_KEY_b);
    select(w, 8, 13);
    copy(w);
    caret(w, 4);
    paste(w);
    expect(w, "start-of-paragraph", "one\nthreetwo three", "one|THREEtwo THREE");
  });

  // Drag and drop within the document, as GtkTextView does it: the drop
  // inserts the selection's range in one user action, then the drag source
  // deletes the selection in another.
  auto drag = [](MainWindow& w, int from, int to, int drop) {
    auto buffer = P::buffer(w);
    select(w, from, to);
    Gtk::TextIter start;
    Gtk::TextIter end;
    buffer->get_selection_bounds(start, end);
    buffer->begin_user_action();
    auto at = buffer->get_iter_at_offset(drop);
    gtk_text_buffer_insert_range_interactive(buffer->gobj(), at.gobj(), start.gobj(), end.gobj(),
                                             TRUE);
    buffer->end_user_action();
    settle(5);
    buffer->erase_selection(true, true);
    settle(5);
  };
  run("drag-plain-into-bold", [&drag](MainWindow& w) {
    one_bold_two(w);
    drag(w, 0, 3, 5);
    expect(w, "drag-plain-into-bold", " tonewo", " ToneWO");
  });
  run("drag-bold-into-plain", [&drag](MainWindow& w) {
    type(w, "one two three");
    select(w, 4, 7);
    ctrl(w, GDK_KEY_b);
    drag(w, 4, 7, 1);
    expect(w, "drag-bold-into-plain", "otwone  three", "oTWOne  three");
  });

  // Undo and redo of the paste.
  run("undo-redo", [](MainWindow& w) {
    plain_into_bold(w);
    expect(w, "undo-redo paste", "one tonewo", "one ToneWO");
    ctrl(w, GDK_KEY_z);
    expect(w, "undo-redo undo", "one two", "one TWO");
    ctrl(w, GDK_KEY_y);
    expect(w, "undo-redo redo", "one tonewo", "one ToneWO");
  });
  // Bold over the pasted range and back.
  run("bold-over-paste", [](MainWindow& w) {
    plain_into_bold(w);
    expect(w, "bold-over-paste paste", "one tonewo", "one ToneWO");
    select(w, 5, 8);
    ctrl(w, GDK_KEY_b);
    expect(w, "bold-over-paste bold", "one tonewo", "one TONEWO");
    select(w, 5, 8);
    ctrl(w, GDK_KEY_b);
    expect(w, "bold-over-paste plain", "one tonewo", "one ToneWO");
  });
  // A named style over the pasted range, and Normal back: the bold set
  // directly with Ctrl+B and the plain pasted text both stay.
  run("style-over-paste", [](MainWindow& w) {
    plain_into_bold(w);
    expect(w, "style-over-paste paste", "one tonewo", "one ToneWO");
    select(w, 0, 10);
    P::style(w, "Heading 1");
    settle(5);
    CHECK(P::buffer(w)->get_text() == "one tonewo");
    assert_single_format_tag(P::buffer(w));
    select(w, 0, 10);
    P::style(w, "Normal");
    settle(5);
    expect(w, "style-over-paste normal", "one tonewo", "one ToneWO");
  });

  return suite_test::done("paste-format", kChecks);
}
