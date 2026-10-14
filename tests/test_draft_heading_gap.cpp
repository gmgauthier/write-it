/* SPDX-License-Identifier: Unlicense */

// A page break on a Heading 1 opens Draft's short gap. The heading's own
// space does not win. Page view keeps the seam and does not keep the tag.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"
#include "view.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <algorithm>
#include <iostream>
#include <string>

namespace writeit {

struct MainWindowProbe {
  static void load(MainWindow& window, const Document& doc)
  {
    window.undo_.clear();
    window.replace_buffer(doc, 0);
    window.saved_state_ = window.undo_.state_id();
    window.save_point_ = true;
    window.last_typed_us_ = 0;
    window.update_title();
    window.update_actions();
    window.sync_format_controls();
  }

  static void zoom(MainWindow& window, int percent)
  {
    window.set_zoom(percent);
  }

  static void draft(MainWindow& window, bool on)
  {
    (on ? window.draft_item_ : window.page_item_)->set_active(true);
  }

  static int find(MainWindow& window, const char* word)
  {
    Gtk::TextBuffer::iterator start;
    Gtk::TextBuffer::iterator end;
    if (!window.buffer_->begin().forward_search(word, Gtk::TEXT_SEARCH_TEXT_ONLY, start, end))
      return -1;
    return start.get_offset();
  }

  static int pixels_above(MainWindow& window, int offset)
  {
    if (offset < 0)
      return -1;
    auto iter = window.buffer_->get_iter_at_offset(offset);
    GtkTextAttributes* attrs = gtk_text_attributes_new();
    gtk_text_iter_get_attributes(iter.gobj(), attrs);
    const int above = attrs->pixels_above_lines;
    gtk_text_attributes_unref(attrs);
    return above;
  }

  static bool draft_gap_on(MainWindow& window, int offset)
  {
    if (offset < 0)
      return false;
    for (const auto& tag : window.buffer_->get_iter_at_offset(offset).get_tags()) {
      if (is_draft_gap(tag->property_name().get_value()))
        return true;
    }
    return false;
  }

  static int seam_px(MainWindow& window, int offset)
  {
    if (offset < 0)
      return 0;
    int pixels = 0;
    for (const auto& tag : window.buffer_->get_iter_at_offset(offset).get_tags())
      pixels = std::max(pixels, page_seam_pixels(tag->property_name().get_value()));
    return pixels;
  }

  static int priority(MainWindow& window, const char* name)
  {
    auto tag = window.buffer_->get_tag_table()->lookup(name);
    if (!tag)
      return -1;
    return tag->get_priority();
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
  for (int round = 0; round < 8; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(20000);
  }
  while (context->pending())
    context->iteration(false);
}

Document sample()
{
  Document doc = writeit::blank_document("Sans", 11);
  doc.paragraphs.clear();
  auto line = [](const char* text, int heading, bool page_break) {
    Paragraph paragraph;
    paragraph.heading = heading;
    paragraph.page_break = page_break;
    Run run;
    run.font = "Sans";
    run.size = 11;
    run.text = text;
    paragraph.runs.push_back(run);
    return paragraph;
  };
  doc.paragraphs.push_back(line("Hello", 0, false));
  doc.paragraphs.push_back(line("Title", 1, true));
  doc.paragraphs.push_back(line("Other", 1, false));
  return doc;
}

}  // namespace

constexpr int kChecks = 7;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-draft-gap-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "draft-heading-gap: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    window.resize(900, 700);
    settle();
    MainWindowProbe::zoom(window, 100);
    settle();
    MainWindowProbe::load(window, sample());
    settle();
    MainWindowProbe::draft(window, true);
    settle();

    const int hello = MainWindowProbe::find(window, "Hello");
    const int title = MainWindowProbe::find(window, "Title");
    const int other = MainWindowProbe::find(window, "Other");
    const int title_above = MainWindowProbe::pixels_above(window, title);
    const int other_above = MainWindowProbe::pixels_above(window, other);
    const int hello_above = MainWindowProbe::pixels_above(window, hello);
    if (title_above != 18 || other_above != 8 || hello_above == 18)
      std::cerr << "draft above hello " << hello_above << " title " << title_above << " other "
                << other_above << "\n";
    CHECK(title_above == 18);
    CHECK(other_above == 8);
    CHECK(hello_above != 18);
    CHECK(MainWindowProbe::priority(window, "draft-gap") >
          MainWindowProbe::priority(window, "heading-1"));

    MainWindowProbe::draft(window, false);
    settle();
    const int seam = MainWindowProbe::seam_px(window, title);
    const int paged = MainWindowProbe::pixels_above(window, title);
    if (seam <= 18 || paged != seam || MainWindowProbe::draft_gap_on(window, title))
      std::cerr << "page seam " << seam << " above " << paged << " gap "
                << MainWindowProbe::draft_gap_on(window, title) << "\n";
    CHECK(!MainWindowProbe::draft_gap_on(window, title));
    CHECK(seam > 18);
    CHECK(paged == seam);
  }
  return suite_test::done("draft-heading-gap", kChecks);
}
