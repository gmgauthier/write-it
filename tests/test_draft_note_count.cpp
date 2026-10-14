/* SPDX-License-Identifier: Unlicense */

// Draft's page count gives a footnote the same room Page view does. The note
// is not drawn in Draft. A body with no note stays one page in both views.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"
#include "view.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
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

  static std::string page(MainWindow& window)
  {
    return window.page_label_.get_text();
  }

  static void draft(MainWindow& window, bool on)
  {
    (on ? window.draft_item_ : window.page_item_)->set_active(true);
  }

  static bool note_visible(MainWindow& window)
  {
    return window.note_box_.get_visible();
  }
};

}  // namespace writeit

namespace {

using writeit::Document;
using writeit::FootnoteRoom;
using writeit::MainWindowProbe;
using writeit::Paragraph;
using writeit::Run;

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

int pages_in(const std::string& label)
{
  const auto of = label.find(" of ");
  if (label.rfind("Page ", 0) != 0 || of == std::string::npos)
    return -1;
  return std::atoi(label.c_str() + of + 4);
}

Document letter(int paragraphs, int note_lines)
{
  Document doc = writeit::blank_document("Sans", 11);
  doc.paragraphs.clear();
  for (int i = 0; i < paragraphs; ++i) {
    Paragraph paragraph;
    Run run;
    run.font = "Sans";
    run.size = 11;
    run.text = "Line " + std::to_string(i + 1) + " of the letter.";
    paragraph.runs.push_back(run);
    if (i == 0 && note_lines > 0) {
      Run mark;
      mark.font = "Sans";
      mark.size = 11;
      mark.text = "1";
      mark.note = 1;
      paragraph.runs.push_back(mark);
    }
    doc.paragraphs.push_back(paragraph);
  }
  if (note_lines > 0) {
    std::vector<Paragraph> note;
    for (int i = 0; i < note_lines; ++i) {
      Paragraph paragraph;
      Run run;
      run.font = "Sans";
      run.size = 11;
      run.text = "Note line " + std::to_string(i + 1) + " explains the marker.";
      paragraph.runs.push_back(run);
      note.push_back(paragraph);
    }
    doc.notes.push_back(note);
  }
  return doc;
}

int page_count_of(writeit::MainWindow& window, const Document& doc)
{
  MainWindowProbe::draft(window, false);
  MainWindowProbe::load(window, doc);
  settle();
  return pages_in(MainWindowProbe::page(window));
}

void room_cases()
{
  const FootnoteRoom none = writeit::footnote_room(692, 0);
  CHECK(none.cut == 0 && none.room == 692);
  const FootnoteRoom some = writeit::footnote_room(692, 100);
  CHECK(some.cut == 100 && some.room == 592);
  const FootnoteRoom over = writeit::footnote_room(692, 800);
  CHECK(over.cut == 800 && over.room == 1);
  const FootnoteRoom negative = writeit::footnote_room(692, -4);
  CHECK(negative.cut == 0 && negative.room == 692);
  const FootnoteRoom flat = writeit::footnote_room(0, 10);
  CHECK(flat.cut == 10 && flat.room == 1);
}

}  // namespace

constexpr int kChecks = 12;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-draft-note-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "draft-note-count: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  room_cases();
  {
    writeit::MainWindow window;
    window.show();
    window.resize(900, 700);
    settle();
    MainWindowProbe::zoom(window, 100);
    settle();

    MainWindowProbe::load(window, letter(3, 0));
    settle();
    CHECK(MainWindowProbe::page(window) == "Page 1 of 1");
    MainWindowProbe::draft(window, true);
    settle();
    CHECK(MainWindowProbe::page(window) == "Page 1 of 1");

    // The fullest body that is still one page with a one-line note. The next
    // line would start a sheet, so a taller note has to as well.
    int lo = 1;
    int hi = 60;
    int fit = 1;
    while (lo <= hi) {
      const int mid = (lo + hi) / 2;
      const int pages = page_count_of(window, letter(mid, 1));
      if (pages <= 1) {
        fit = mid;
        lo = mid + 1;
      } else {
        hi = mid - 1;
      }
    }
    const int fitted = page_count_of(window, letter(fit, 1));
    if (fitted != 1)
      std::cerr << "one-line note, " << fit << " lines: " << MainWindowProbe::page(window) << "\n";
    CHECK(fitted == 1);

    MainWindowProbe::load(window, letter(fit, 14));
    settle();
    const std::string paged = MainWindowProbe::page(window);
    const int page_pages = pages_in(paged);
    if (page_pages != 2)
      std::cerr << "page view, tall note: " << paged << "\n";
    CHECK(page_pages == 2);
    CHECK(MainWindowProbe::note_visible(window));

    MainWindowProbe::draft(window, true);
    settle();
    const std::string drafted = MainWindowProbe::page(window);
    const int draft_pages = pages_in(drafted);
    if (draft_pages != 2)
      std::cerr << "draft, tall note: " << drafted << "\n";
    CHECK(draft_pages == 2);
    CHECK(!MainWindowProbe::note_visible(window));
  }
  return suite_test::done("draft-note-count", kChecks);
}
