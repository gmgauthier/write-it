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
#include <utility>
#include <vector>

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
  static void load(MainWindow& w, const Document& doc)
  {
    w.replace_buffer(doc, 0);
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


// Paragraph formats, one line per paragraph: its text, style, list kind and
// level, indents, alignment, outline level, direct paragraph bits and the
// direct bits of its runs. List numbers are checked on their own.
std::string describe(const writeit::Paragraph& p, const std::string& text)
{
  unsigned run_direct = 0;
  for (const writeit::Run& run : p.runs)
    run_direct |= run.direct;
  return "[" + text + " " + p.style + " list" + std::to_string(static_cast<int>(p.list.kind)) + "." +
         std::to_string(p.list.level) + " li" + std::to_string(p.indents.left) + " ri" +
         std::to_string(p.indents.right) + " fi" + std::to_string(p.indents.first) + " al" +
         std::to_string(static_cast<int>(p.align)) + " h" + std::to_string(p.heading) + " pd" +
         std::to_string(p.direct) + " rd" + std::to_string(run_direct) + "]";
}

std::string text_of(const writeit::Paragraph& p)
{
  std::string text;
  for (const writeit::Run& run : p.runs)
    text += run.text;
  return text;
}

std::string describe(const writeit::Document& doc)
{
  std::string out;
  for (const writeit::Paragraph& p : doc.paragraphs)
    out += describe(p, text_of(p));
  return out;
}

// A paragraph's format with other text: what a pasted or joined paragraph
// should look like.
std::string as(const writeit::Document& doc, size_t i, const std::string& text)
{
  return describe(doc.paragraphs[i], text);
}

// A document of one paragraph per text, each in the style given ("Bullet"
// and "Number" make a Normal list item), through RTF as a file would come.
writeit::Document make(const std::vector<std::pair<std::string, std::string>>& paras,
                       const std::vector<int>& left = {})
{
  writeit::Document d = writeit::blank_document("Sans", 11);
  d.paragraphs.clear();
  for (const auto& [text, style] : paras) {
    writeit::Paragraph p;
    writeit::Run r;
    r.text = text;
    p.runs.push_back(r);
    d.paragraphs.push_back(p);
  }
  for (size_t i = 0; i < paras.size(); ++i) {
    const std::string& style = paras[i].second;
    if (style == "Bullet")
      d.paragraphs[i].list = writeit::ListFormat(writeit::ListKind::Bullet, 0);
    else if (style == "Number")
      d.paragraphs[i].list = writeit::ListFormat(writeit::ListKind::Number, 0);
    else if (style != "Normal")
      writeit::apply_style(d, i, i, style);
    // A left indent set directly, in twips.
    if (i < left.size() && left[i] > 0) {
      d.paragraphs[i].indents.left = left[i];
      d.paragraphs[i].direct |= writeit::kDirectLeft;
    }
  }
  writeit::Document back;
  writeit::rtf_import(writeit::rtf_export(d), back);
  return back;
}

// Copies [from, to) with Ctrl+C and pastes it at `at` with Ctrl+V, in a
// window holding `doc`. Then: the text, the paragraph formats (built from
// the original's by `want`), a save and reopen giving the same paragraphs,
// and the one-tag rule (4 checks); Ctrl+Z back to the original and Ctrl+Y
// back to the paste, each with the one-tag rule (4 more).
void para_case(const char* name, const writeit::Document& doc, int from, int to, int at,
               const std::string& text,
               const std::function<std::string(const writeit::Document&)>& want,
               const std::function<void(const writeit::Document&)>& more = {})
{
  run(name, [&](MainWindow& w) {
    P::load(w, doc);
    settle(10);
    const writeit::Document before = P::capture(w);
    const std::string original = describe(before);
    select(w, from, to);
    copy(w);
    caret(w, at);
    paste(w);
    const writeit::Document after = P::capture(w);
    const std::string got = describe(after);
    const std::string expected = want(before);
    writeit::Document reopened;
    writeit::rtf_import(writeit::rtf_export(after), reopened);
    if (got != expected || describe(reopened) != got)
      std::cerr << name << ":\n  got      " << got << "\n  expected " << expected
                << "\n  reopened " << describe(reopened) << "\n";
    CHECK(P::buffer(w)->get_text().raw() == text);
    CHECK(got == expected);
    CHECK(describe(reopened) == got);
    assert_single_format_tag(P::buffer(w));
    if (more)
      more(after);
    ctrl(w, GDK_KEY_z);
    const std::string undone = describe(P::capture(w));
    if (undone != original)
      std::cerr << name << " undo:\n  got      " << undone << "\n  expected " << original << "\n";
    CHECK(undone == original);
    assert_single_format_tag(P::buffer(w));
    ctrl(w, GDK_KEY_y);
    const std::string redone = describe(P::capture(w));
    if (redone != expected)
      std::cerr << name << " redo:\n  got      " << redone << "\n  expected " << expected << "\n";
    CHECK(redone == expected);
    assert_single_format_tag(P::buffer(w));
  });
}
}  // namespace

constexpr int kChecks = 176;

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

  // Paragraph format belongs to the paragraph's end. A pasted end brings its
  // paragraph's format: a paragraph copied whole keeps it, and the paragraph
  // the first pasted end closes takes the copied paragraph's. Text after the
  // last pasted end joins the paragraph it lands in. Each case: Ctrl+Z and
  // Ctrl+Y too. "aaaa\nbbbb\ncccc", copy "aa\nbbbb\ncc", paste into "cc|cc".
  for (const char* style : {"Block Text", "Heading 1", "Bullet", "Number"}) {
    const writeit::Document doc = make({{"aaaa", "Normal"}, {"bbbb", style}, {"cccc", "Normal"}});
    const std::string name = std::string("whole ") + style + " paragraph";
    para_case(
        name.c_str(), doc, 2, 12, 12, "aaaa\nbbbb\nccaa\nbbbb\ncccc",
        [](const writeit::Document& d) {
          return as(d, 0, "aaaa") + as(d, 1, "bbbb") + as(d, 0, "ccaa") + as(d, 1, "bbbb") +
                 as(d, 2, "cccc");
        },
        std::string(style) == "Number"
            ? std::function<void(const writeit::Document&)>([](const writeit::Document& d) {
                // Both items in one list, counting 1 and 2.
                const std::vector<int> numbers = writeit::list_numbers(d.paragraphs);
                CHECK(numbers.size() == 5 && numbers[1] == 1 && numbers[3] == 2);
              })
            : std::function<void(const writeit::Document&)>());
  }
  // Two whole paragraphs, Block Text and Heading 1.
  para_case("two whole paragraphs",
            make({{"aaaa", "Normal"},
                  {"bbbb", "Block Text"},
                  {"cccc", "Heading 1"},
                  {"dddd", "Normal"}}),
            2, 17, 17, "aaaa\nbbbb\ncccc\nddaa\nbbbb\ncccc\ndddd", [](const writeit::Document& d) {
              return as(d, 0, "aaaa") + as(d, 1, "bbbb") + as(d, 2, "cccc") + as(d, 0, "ddaa") +
                     as(d, 1, "bbbb") + as(d, 2, "cccc") + as(d, 3, "dddd");
            });
  // From the middle of a Heading 1 through a Block Text paragraph into the
  // middle of a Normal one: Heading 1, Block Text, Normal.
  const writeit::Document hbn =
      make({{"hhhh", "Heading 1"}, {"bbbb", "Block Text"}, {"nnnn", "Normal"}});
  para_case("heading through block text into normal", hbn, 2, 12, 12,
            "hhhh\nbbbb\nnnhh\nbbbb\nnnnn", [](const writeit::Document& d) {
              return as(d, 0, "hhhh") + as(d, 1, "bbbb") + as(d, 0, "nnhh") + as(d, 1, "bbbb") +
                     as(d, 2, "nnnn");
            });
  // The same at the start of the Normal paragraph, at its end (the end of
  // the document), and at the end of the Block Text paragraph.
  para_case("at the start of a paragraph", hbn, 2, 12, 10, "hhhh\nbbbb\nhh\nbbbb\nnnnnnn",
            [](const writeit::Document& d) {
              return as(d, 0, "hhhh") + as(d, 1, "bbbb") + as(d, 0, "hh") + as(d, 1, "bbbb") +
                     as(d, 2, "nnnnnn");
            });
  para_case("at the end of the last paragraph", hbn, 2, 12, 14, "hhhh\nbbbb\nnnnnhh\nbbbb\nnn",
            [](const writeit::Document& d) {
              return as(d, 0, "hhhh") + as(d, 1, "bbbb") + as(d, 0, "nnnnhh") + as(d, 1, "bbbb") +
                     as(d, 2, "nn");
            });
  para_case("at the end of a paragraph", hbn, 2, 12, 9, "hhhh\nbbbbhh\nbbbb\nnn\nnnnn",
            [](const writeit::Document& d) {
              return as(d, 0, "hhhh") + as(d, 0, "bbbbhh") + as(d, 1, "bbbb") + as(d, 1, "nn") +
                     as(d, 2, "nnnn");
            });
  // Direct indents: A at 1", D at 0.5". The paragraph the first pasted end
  // closes gets A's indents; the text after the last end keeps D's.
  {
    const writeit::Document doc =
        make({{"aaaa", "Normal"}, {"bbbb", "Block Text"}, {"dddd", "Normal"}}, {1440, 0, 720});
    para_case("direct indents", doc, 2, 12, 12, "aaaa\nbbbb\nddaa\nbbbb\ndddd",
              [](const writeit::Document& d) {
                return as(d, 0, "aaaa") + as(d, 1, "bbbb") + as(d, 0, "ddaa") + as(d, 1, "bbbb") +
                       as(d, 2, "dddd");
              });
  }
  // No paragraph end in the paste: no paragraph format changes.
  para_case("no paragraph end", hbn, 6, 8, 12, "hhhh\nbbbb\nnnbbnn",
            [](const writeit::Document& d) {
              return as(d, 0, "hhhh") + as(d, 1, "bbbb") + as(d, 2, "nnbbnn");
            });

  return suite_test::done("paste-format", kChecks);
}
