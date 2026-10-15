/* SPDX-License-Identifier: Unlicense */

// Enter after a page break must not start another sheet. The second Enter,
// the blank line, used to copy the break onto that new paragraph, and every
// Enter after it did the same. A break on the empty paragraph after the text
// stays there.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <iostream>
#include <string>

namespace writeit {

struct MainWindowProbe {
  static void load(MainWindow& window, const Document& doc, int offset)
  {
    window.undo_.clear();
    window.replace_buffer(doc, offset);
    window.saved_state_ = window.undo_.state_id();
    window.save_point_ = true;
    window.last_typed_us_ = 0;
    window.update_title();
    window.update_actions();
    window.sync_format_controls();
  }

  static Document doc(MainWindow& window)
  {
    return window.capture();
  }

  static Glib::RefPtr<Gtk::TextBuffer> buffer(MainWindow& window)
  {
    return window.buffer_;
  }

  static void caret(MainWindow& window, int offset)
  {
    auto buffer = window.buffer_;
    const int count = buffer->get_char_count();
    const int at = offset < 0 ? 0 : offset > count ? count : offset;
    buffer->place_cursor(buffer->get_iter_at_offset(at));
  }

  static std::string page(MainWindow& window)
  {
    return window.page_label_.get_text();
  }

  static int seams(MainWindow& window)
  {
    int count = 0;
    auto iter = window.buffer_->begin();
    while (true) {
      for (const auto& tag : iter.get_tags()) {
        if (page_seam_pixels(tag->property_name().get_value()) > 0)
          ++count;
      }
      bool more = false;
      while (!iter.is_end()) {
        if (iter.get_char() == '\n') {
          iter.forward_char();
          more = !iter.is_end();
          break;
        }
        iter.forward_char();
      }
      if (!more)
        break;
    }
    return count;
  }
};

}  // namespace writeit

namespace {

using writeit::Document;
using writeit::MainWindowProbe;
using writeit::Paragraph;
using writeit::Run;

void settle()
{
  auto context = Glib::MainContext::get_default();
  for (int round = 0; round < 6; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(10000);
  }
  while (context->pending())
    context->iteration(false);
}

Paragraph text_para(const std::string& text, bool page_break)
{
  Paragraph paragraph;
  paragraph.page_break = page_break;
  if (!text.empty()) {
    Run run;
    run.text = text;
    paragraph.runs.push_back(std::move(run));
  }
  return paragraph;
}

std::string text_of(const Paragraph& paragraph)
{
  std::string out;
  for (const Run& run : paragraph.runs)
    out += run.text;
  return out;
}

void enter(writeit::MainWindow& window)
{
  auto buffer = MainWindowProbe::buffer(window);
  buffer->begin_user_action();
  buffer->insert_at_cursor("\n");
  buffer->end_user_action();
  settle();
}

int breaks_of(const Document& doc)
{
  int n = 0;
  for (const Paragraph& paragraph : doc.paragraphs)
    n += paragraph.page_break ? 1 : 0;
  return n;
}

// Hello itself starts the sheet. Each Enter at the end adds a blank line and
// leaves that one break where it was.
void enter_after_broken_hello(writeit::MainWindow& window)
{
  Document doc;
  doc.paragraphs = {text_para("Hello", true)};
  MainWindowProbe::load(window, doc, 5);
  settle();
  CHECK(breaks_of(MainWindowProbe::doc(window)) == 1);

  enter(window);
  CHECK(breaks_of(MainWindowProbe::doc(window)) == 1);
  CHECK(!MainWindowProbe::doc(window).paragraphs.back().page_break);

  enter(window);
  CHECK(breaks_of(MainWindowProbe::doc(window)) == 1);

  enter(window);
  CHECK(breaks_of(MainWindowProbe::doc(window)) == 1);

  enter(window);
  CHECK(breaks_of(MainWindowProbe::doc(window)) == 1);
  const Document after = MainWindowProbe::doc(window);
  CHECK(after.paragraphs.size() == 5);
  CHECK(after.paragraphs[0].page_break);
  CHECK(text_of(after.paragraphs[0]) == "Hello");
  for (size_t i = 1; i < after.paragraphs.size(); ++i)
    CHECK(!after.paragraphs[i].page_break);
  CHECK(MainWindowProbe::page(window) == "Page 2 of 2");
  CHECK(MainWindowProbe::seams(window) == 1);
}

// Insert → Page Break at the end of Hello: the break is the empty paragraph
// after it. Enter in front of that break, then past it, adds no second sheet.
void enter_after_following_break(writeit::MainWindow& window)
{
  Document doc;
  doc.paragraphs = {text_para("Hello", false), text_para("", true)};
  MainWindowProbe::load(window, doc, 6);
  settle();
  CHECK(breaks_of(MainWindowProbe::doc(window)) == 1);

  enter(window);
  CHECK(breaks_of(MainWindowProbe::doc(window)) == 1);

  enter(window);
  CHECK(breaks_of(MainWindowProbe::doc(window)) == 1);

  MainWindowProbe::caret(window, MainWindowProbe::buffer(window)->get_char_count());
  enter(window);
  CHECK(breaks_of(MainWindowProbe::doc(window)) == 1);

  enter(window);
  CHECK(breaks_of(MainWindowProbe::doc(window)) == 1);
  CHECK(MainWindowProbe::page(window) == "Page 2 of 2");
  CHECK(MainWindowProbe::seams(window) == 1);
}

void enter_without_break(writeit::MainWindow& window)
{
  Document doc;
  doc.paragraphs = {text_para("Hello", false)};
  MainWindowProbe::load(window, doc, 5);
  settle();
  enter(window);
  enter(window);
  CHECK(breaks_of(MainWindowProbe::doc(window)) == 0);
  CHECK(MainWindowProbe::page(window) == "Page 1 of 1");
}

}  // namespace

constexpr int kChecks = 24;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-double-return-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "double-return: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    enter_after_broken_hello(window);
    enter_after_following_break(window);
    enter_without_break(window);
    window.hide();
    settle();
  }
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("double-return", kChecks);
}
