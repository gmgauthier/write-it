/* SPDX-License-Identifier: Unlicense */

// The real window, under a display. A page that fills more than one sheet
// shows the next sheet on the gray pasteboard, not one long white page.
// Draft stays one column. The gray is not a page in the status bar or on
// paper, and it is not stored in the document.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"
#include "view.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace writeit {

struct MainWindowProbe {
  static void load(MainWindow& w, const std::string& text)
  {
    w.buffer_->set_text(text);
  }
  static void zoom(MainWindow& w, int percent)
  {
    w.set_zoom(percent);
  }
  static void caret(MainWindow& w, bool end)
  {
    auto iter = end ? w.buffer_->end() : w.buffer_->begin();
    w.buffer_->place_cursor(iter);
  }
  static std::string page(MainWindow& w)
  {
    return w.page_label_.get_text();
  }
  static void draft(MainWindow& w, bool on)
  {
    (on ? w.draft_item_ : w.page_item_)->set_active(true);
  }
  static std::vector<int> seams(MainWindow& w)
  {
    std::vector<int> pixels;
    auto iter = w.buffer_->begin();
    while (true) {
      for (const auto& tag : iter.get_tags()) {
        const int height = page_seam_pixels(tag->property_name().get_value());
        if (height > 0)
          pixels.push_back(height);
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
    return pixels;
  }
  // Window y of the first gray band, in the page widget. -1 when there is
  // no band.
  static int first_gap(MainWindow& w)
  {
    const ViewGeometry sheet = w.geometry();
    auto iter = w.buffer_->begin();
    while (true) {
      int seam = 0;
      for (const auto& tag : iter.get_tags())
        seam = std::max(seam, page_seam_pixels(tag->property_name().get_value()));
      if (seam > 0) {
        int top = 0;
        int height = 0;
        w.text_.get_line_yrange(iter, top, height);
        const int gray = top + seam - sheet.gap - sheet.margin_y;
        int x = 0;
        int y = 0;
        w.text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, gray, x, y);
        int page_x = 0;
        int page_y = 0;
        w.text_.translate_coordinates(w.page_, 0, y, page_x, page_y);
        return page_y;
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
    return -1;
  }
  static Document doc(MainWindow& w)
  {
    return w.capture();
  }
  static bool print(MainWindow& w, const std::string& path)
  {
    return w.export_page(path);
  }
  static void place(MainWindow& w, int offset)
  {
    const int count = w.buffer_->get_char_count();
    const int at = std::max(0, std::min(offset, count));
    w.buffer_->place_cursor(w.buffer_->get_iter_at_offset(at));
  }
  static std::vector<int> seam_offsets(MainWindow& w)
  {
    std::vector<int> offsets;
    auto iter = w.buffer_->begin();
    while (true) {
      int seam = 0;
      for (const auto& tag : iter.get_tags())
        seam = std::max(seam, page_seam_pixels(tag->property_name().get_value()));
      if (seam > 0)
        offsets.push_back(iter.get_offset());
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
    return offsets;
  }
  static int iter_y(MainWindow& w, int offset)
  {
    const int count = w.buffer_->get_char_count();
    const int at = std::max(0, std::min(offset, count));
    Gdk::Rectangle loc;
    w.text_.get_iter_location(w.buffer_->get_iter_at_offset(at), loc);
    return loc.get_y();
  }
  static int per_page(MainWindow& w)
  {
    return std::max(1, page_text_height(w.page_setup_, w.zoom_factor()));
  }
  static int chars(MainWindow& w)
  {
    return w.buffer_->get_char_count();
  }
  static void load_doc(MainWindow& w, const Document& doc)
  {
    w.undo_.clear();
    w.replace_buffer(doc, 0);
    w.saved_state_ = w.undo_.state_id();
    w.save_point_ = true;
    w.update_title();
  }
  static void page_break(MainWindow& w)
  {
    w.on_page_break();
  }
  static std::string message(MainWindow& w)
  {
    return w.message_.get_text();
  }
  // separator: the newline after the anchor. Otherwise the first character
  // of the paragraph that follows the table.
  static int past_table(MainWindow& w, bool separator)
  {
    if (!w.buffer_)
      return -1;
    for (auto iter = w.buffer_->begin(); !iter.is_end(); ++iter) {
      if (!iter.get_child_anchor())
        continue;
      auto next = iter;
      if (!next.forward_char())
        return w.buffer_->get_char_count();
      if (next.get_char() == '\n' && !next.get_child_anchor()) {
        if (separator)
          return next.get_offset();
        auto body = next;
        if (!body.forward_char())
          return w.buffer_->get_char_count();
        return body.get_offset();
      }
      return next.get_offset();
    }
    return -1;
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

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

std::string letter(int paragraphs)
{
  std::string text;
  for (int i = 1; i <= paragraphs; ++i)
    text += "Paragraph " + std::to_string(i) + " of the letter.\n";
  return text;
}

int page_breaks(const writeit::Document& doc)
{
  int count = 0;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.page_break)
      ++count;
  }
  return count;
}

int break_index(const writeit::Document& doc)
{
  for (size_t i = 0; i < doc.paragraphs.size(); ++i) {
    if (doc.paragraphs[i].page_break)
      return static_cast<int>(i);
  }
  return -1;
}

std::string text_of(const writeit::Paragraph& paragraph)
{
  std::string out;
  for (const writeit::Run& run : paragraph.runs)
    out += run.text;
  return out;
}

bool break_on_after(const writeit::Document& doc)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.page_break)
      return paragraph.cell.table == 0 && text_of(paragraph) == "After";
  }
  return false;
}

int pages_in(const std::string& label)
{
  const auto of = label.find(" of ");
  if (label.rfind("Page ", 0) != 0 || of == std::string::npos)
    return -1;
  return std::atoi(label.c_str() + of + 4);
}

// Cairo writes the page tree into compressed streams. /Count is the number
// of pages; /Pages is the tree, not a page.
std::string inflate_pdf(const std::string& pdf)
{
  std::string out = pdf;
  size_t i = 0;
  while ((i = pdf.find("stream\n", i)) != std::string::npos) {
    i += 7;
    const size_t end = pdf.find("endstream", i);
    if (end == std::string::npos)
      break;
    size_t data_end = end;
    if (data_end > i && pdf[data_end - 1] == '\n')
      --data_end;
    std::string chunk = pdf.substr(i, data_end - i);
    z_stream strm{};
    strm.next_in = reinterpret_cast<Bytef*>(chunk.data());
    strm.avail_in = static_cast<uInt>(chunk.size());
    if (inflateInit(&strm) == Z_OK) {
      std::string buf(chunk.size() * 8 + 64, '\0');
      int ret = Z_OK;
      while (ret == Z_OK) {
        if (strm.total_out >= buf.size())
          buf.resize(buf.size() * 2);
        strm.next_out = reinterpret_cast<Bytef*>(&buf[strm.total_out]);
        strm.avail_out = static_cast<uInt>(buf.size() - strm.total_out);
        ret = inflate(&strm, Z_NO_FLUSH);
      }
      if (ret == Z_STREAM_END)
        out.append(buf.data(), strm.total_out);
      inflateEnd(&strm);
    }
    i = end + 9;
  }
  return out;
}

int pdf_pages(const std::string& pdf)
{
  const std::string plain = inflate_pdf(pdf);
  const size_t at = plain.find("/Type /Pages");
  if (at == std::string::npos)
    return -1;
  const size_t count = plain.find("/Count ", at);
  if (count == std::string::npos)
    return -1;
  return std::atoi(plain.c_str() + count + 7);
}

}  // namespace

constexpr int kChecks = 62;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-page-seam-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "page-seam: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.set_default_size(960, 700);
    window.show();
    settle();
    MainWindowProbe::zoom(window, 100);
    settle();

    MainWindowProbe::load(window, letter(2));
    settle();
    CHECK(MainWindowProbe::seams(window).empty());
    CHECK(MainWindowProbe::page(window) == "Page 1 of 1");

    MainWindowProbe::load(window, letter(80));
    MainWindowProbe::caret(window, false);
    settle();
    const std::vector<int> seams = MainWindowProbe::seams(window);
    const std::string at_start = MainWindowProbe::page(window);
    const int pages = pages_in(at_start);
    if (seams.empty() || pages < 2)
      std::cerr << "long letter: " << at_start << ", seams " << seams.size() << "\n";
    CHECK(!seams.empty());
    CHECK(pages >= 2);
    CHECK(at_start == "Page 1 of " + std::to_string(pages));
    const int gap = MainWindowProbe::first_gap(window);
    const int sheet = writeit::view_geometry(writeit::ViewMode::Page, 1.0).page_height;
    if (std::abs(gap - sheet) > 2)
      std::cerr << "first gap at " << gap << ", sheet " << sheet << "\n";
    CHECK(std::abs(gap - sheet) <= 2);

    // The first line of the next sheet is that page. A line that did not fit
    // the room left behind used to keep the previous page number.
    const std::vector<int> breaks = MainWindowProbe::seam_offsets(window);
    const int first_break = breaks.empty() ? 0 : breaks.front();
    MainWindowProbe::place(window, first_break > 0 ? first_break - 1 : 0);
    settle();
    CHECK(MainWindowProbe::page(window) == "Page 1 of " + std::to_string(pages));
    MainWindowProbe::place(window, first_break);
    settle();
    const std::string on_next = MainWindowProbe::page(window);
    if (on_next != "Page 2 of " + std::to_string(pages))
      std::cerr << "first line of sheet 2: " << on_next << "\n";
    CHECK(on_next == "Page 2 of " + std::to_string(pages));

    MainWindowProbe::caret(window, true);
    settle();
    const std::string at_end = MainWindowProbe::page(window);
    if (at_end != "Page " + std::to_string(pages) + " of " + std::to_string(pages))
      std::cerr << "caret at end: " << at_end << "\n";
    CHECK(at_end == "Page " + std::to_string(pages) + " of " + std::to_string(pages));

    // The gray is not another printed page, and not part of the document.
    const std::string pdf_path = Glib::build_filename(home, "long.pdf");
    CHECK(MainWindowProbe::print(window, pdf_path));
    const int printed = pdf_pages(Glib::file_get_contents(pdf_path));
    if (printed != pages)
      std::cerr << "printed " << printed << " pages, status " << pages << "\n";
    CHECK(printed == pages);
    const writeit::Document after = MainWindowProbe::doc(window);
    // 80 lines, and the trailing newline is an empty paragraph. A seam must
    // not add one of its own.
    if (after.paragraphs.size() != 81)
      std::cerr << "paragraphs " << after.paragraphs.size() << "\n";
    CHECK(after.paragraphs.size() == 81);
    bool stored = false;
    for (const writeit::Paragraph& paragraph : after.paragraphs) {
      if (writeit::is_page_seam(paragraph.style))
        stored = true;
    }
    CHECK(!stored);

    MainWindowProbe::draft(window, true);
    settle();
    CHECK(MainWindowProbe::seams(window).empty());
    const int draft_pages = pages_in(MainWindowProbe::page(window));
    CHECK(std::abs(draft_pages - pages) <= 1);

    MainWindowProbe::draft(window, false);
    settle();
    CHECK(!MainWindowProbe::seams(window).empty());

    // A block too tall for the room left on the sheet moves to the next one.
    // Its top, and a point a third of the way down that sheet, are the new
    // page. The old count stayed on the previous page until that leftover
    // room had been walked off, about halfway down when the block is tall.
    std::string tall;
    for (int i = 0; i < 15; ++i)
      tall += "Line " + std::to_string(i) + " of the first sheet.\n";
    for (int i = 0; i < 40; ++i)
      tall += "The quick brown fox jumps over the lazy dog. ";
    tall += "\n";
    MainWindowProbe::zoom(window, 100);
    MainWindowProbe::load(window, tall);
    settle();
    const std::vector<int> pushed = MainWindowProbe::seam_offsets(window);
    if (pushed.empty())
      std::cerr << "tall block did not cross a sheet, chars " << MainWindowProbe::chars(window)
                << " per " << MainWindowProbe::per_page(window) << " endy "
                << MainWindowProbe::iter_y(window, MainWindowProbe::chars(window)) << "\n";
    CHECK(!pushed.empty());
    const int at_block = pushed.empty() ? 0 : pushed.front();
    MainWindowProbe::place(window, at_block);
    settle();
    const std::string block_top = MainWindowProbe::page(window);
    const int block_pages = pages_in(block_top);
    if (block_top != "Page 2 of " + std::to_string(block_pages))
      std::cerr << "pushed block: " << block_top << "\n";
    CHECK(block_top == "Page 2 of " + std::to_string(block_pages));
    const int sample_y =
        MainWindowProbe::iter_y(window, at_block) + MainWindowProbe::per_page(window) / 3;
    int mid = at_block;
    const int count = MainWindowProbe::chars(window);
    for (int off = at_block; off < count; ++off) {
      if (MainWindowProbe::iter_y(window, off) >= sample_y) {
        mid = off;
        break;
      }
    }
    MainWindowProbe::place(window, mid);
    settle();
    const std::string block_mid = MainWindowProbe::page(window);
    if (block_mid != "Page 2 of " + std::to_string(block_pages))
      std::cerr << "a third down the pushed sheet: " << block_mid << "\n";
    CHECK(block_mid == "Page 2 of " + std::to_string(block_pages));

    // A page break on the blank line between two lines is one seam. The
    // blank lines after it stay blank. Folding those paragraphs into the
    // break painted a seam on every one of them, and the break itself did
    // not show until the file was opened again.
    MainWindowProbe::zoom(window, 100);
    MainWindowProbe::load(window, "Alpha\n\nBeta\n\n\n\n");
    settle();
    MainWindowProbe::place(window, 6);
    MainWindowProbe::page_break(window);
    settle();
    {
      const writeit::Document broken = MainWindowProbe::doc(window);
      const std::vector<int> one = MainWindowProbe::seam_offsets(window);
      if (broken.paragraphs.size() != 7 || page_breaks(broken) != 1 || break_index(broken) != 1 ||
          one.size() != 1) {
        std::cerr << "blank break: paragraphs " << broken.paragraphs.size() << " breaks "
                  << page_breaks(broken) << " at " << break_index(broken) << " page "
                  << MainWindowProbe::page(window) << " offsets";
        for (int at : one)
          std::cerr << " " << at;
        std::cerr << " pixels";
        for (int px : MainWindowProbe::seams(window))
          std::cerr << " " << px;
        std::cerr << "\n";
      }
      CHECK(broken.paragraphs.size() == 7);
      CHECK(page_breaks(broken) == 1);
      CHECK(break_index(broken) == 1);
      CHECK(text_of(broken.paragraphs[0]) == "Alpha");
      CHECK(text_of(broken.paragraphs[1]).empty());
      CHECK(text_of(broken.paragraphs[2]) == "Beta");
      CHECK(one.size() == 1);
      CHECK(!one.empty() && one.front() == 6);
      CHECK(MainWindowProbe::page(window) == "Page 2 of 2");
      CHECK(MainWindowProbe::message(window).empty());
    }
    {
      const writeit::Document broken = MainWindowProbe::doc(window);
      MainWindowProbe::load_doc(window, broken);
      settle();
      const writeit::Document again = MainWindowProbe::doc(window);
      const std::vector<int> one = MainWindowProbe::seam_offsets(window);
      CHECK(page_breaks(again) == 1);
      CHECK(break_index(again) == 1);
      CHECK(one.size() == 1);
      // The reload leaves the caret on the first line.
      CHECK(MainWindowProbe::page(window) == "Page 1 of 2");
    }
    {
      writeit::Document round;
      const bool imported = writeit::rtf_import(writeit::rtf_export(MainWindowProbe::doc(window)), round);
      CHECK(imported);
      if (imported) {
        MainWindowProbe::load_doc(window, round);
        settle();
      }
      const std::vector<int> one = MainWindowProbe::seam_offsets(window);
      if (page_breaks(MainWindowProbe::doc(window)) != 1 || one.size() != 1)
        std::cerr << "reopen: breaks " << page_breaks(MainWindowProbe::doc(window)) << " seams "
                  << one.size() << " page " << MainWindowProbe::page(window) << "\n";
      CHECK(page_breaks(MainWindowProbe::doc(window)) == 1);
      CHECK(one.size() == 1);
      CHECK(MainWindowProbe::page(window) == "Page 1 of 2");
    }

    // The caret between two lines of text flags the second line, and that
    // line is the only seam.
    MainWindowProbe::load(window, "Alpha\nBeta\n");
    settle();
    MainWindowProbe::place(window, 5);
    MainWindowProbe::page_break(window);
    settle();
    {
      const writeit::Document broken = MainWindowProbe::doc(window);
      const std::vector<int> one = MainWindowProbe::seam_offsets(window);
      if (one.size() != 1 || MainWindowProbe::page(window) != "Page 2 of 2")
        std::cerr << "between lines: seams " << one.size() << " page "
                  << MainWindowProbe::page(window) << "\n";
      CHECK(broken.paragraphs.size() == 3);
      CHECK(!broken.paragraphs[0].page_break);
      CHECK(broken.paragraphs[1].page_break);
      CHECK(text_of(broken.paragraphs[1]) == "Beta");
      CHECK(one.size() == 1);
      CHECK(MainWindowProbe::page(window) == "Page 2 of 2");
    }

    // A caret on the paragraph after a table, and on the newline that ends
    // the table's line, starts a sheet. A caret on the anchor is the table.
    {
      writeit::Document doc;
      writeit::Paragraph before;
      writeit::Run run;
      run.text = "Before";
      before.runs.push_back(run);
      doc.paragraphs.push_back(before);
      CHECK(writeit::insert_table(doc.paragraphs, 1, 2, 2, writeit::page_text_twips(doc.page)));
      writeit::Paragraph after;
      writeit::Run after_run;
      after_run.text = "After";
      after.runs.push_back(after_run);
      doc.paragraphs.push_back(after);
      MainWindowProbe::load_doc(window, doc);
      settle();
      MainWindowProbe::place(window, MainWindowProbe::past_table(window, false));
      MainWindowProbe::page_break(window);
      settle();
      const writeit::Document on_after = MainWindowProbe::doc(window);
      if (!MainWindowProbe::message(window).empty() || page_breaks(on_after) != 1)
        std::cerr << "after table: " << MainWindowProbe::message(window) << " breaks "
                  << page_breaks(on_after) << "\n";
      CHECK(MainWindowProbe::message(window).empty());
      CHECK(page_breaks(on_after) == 1);
      CHECK(break_on_after(on_after));
      CHECK(MainWindowProbe::seam_offsets(window).size() == 1);
      CHECK(MainWindowProbe::page(window) == "Page 2 of 2");

      MainWindowProbe::load_doc(window, doc);
      settle();
      MainWindowProbe::place(window, MainWindowProbe::past_table(window, true));
      MainWindowProbe::page_break(window);
      settle();
      const writeit::Document on_sep = MainWindowProbe::doc(window);
      if (!MainWindowProbe::message(window).empty())
        std::cerr << "separator: " << MainWindowProbe::message(window) << "\n";
      CHECK(MainWindowProbe::message(window).empty());
      CHECK(page_breaks(on_sep) == 1);
      CHECK(break_on_after(on_sep));
      CHECK(MainWindowProbe::page(window) == "Page 2 of 2");
    }
    {
      writeit::Document only;
      CHECK(writeit::insert_table(only.paragraphs, 0, 2, 2, writeit::page_text_twips(only.page)));
      MainWindowProbe::load_doc(window, only);
      settle();
      MainWindowProbe::place(window, MainWindowProbe::chars(window));
      MainWindowProbe::page_break(window);
      settle();
      const writeit::Document broken = MainWindowProbe::doc(window);
      bool body = false;
      if (!broken.paragraphs.empty()) {
        const writeit::Paragraph& last = broken.paragraphs.back();
        body = last.page_break && last.cell.table == 0 && text_of(last).empty();
      }
      if (!MainWindowProbe::message(window).empty() || !body ||
          MainWindowProbe::seam_offsets(window).size() != 1 ||
          MainWindowProbe::page(window) != "Page 2 of 2") {
        std::cerr << "table end: " << MainWindowProbe::message(window) << " body " << body
                  << " page " << MainWindowProbe::page(window) << " offsets";
        for (int at : MainWindowProbe::seam_offsets(window))
          std::cerr << " " << at;
        std::cerr << " pixels";
        for (int px : MainWindowProbe::seams(window))
          std::cerr << " " << px;
        std::cerr << "\n";
      }
      CHECK(MainWindowProbe::message(window).empty());
      CHECK(page_breaks(broken) == 1);
      CHECK(body);
      CHECK(MainWindowProbe::seam_offsets(window).size() == 1);
      CHECK(MainWindowProbe::page(window) == "Page 2 of 2");

      MainWindowProbe::load_doc(window, only);
      settle();
      MainWindowProbe::place(window, 0);
      MainWindowProbe::page_break(window);
      settle();
      CHECK(MainWindowProbe::message(window) == "Cannot break the page inside a table.");
      CHECK(page_breaks(MainWindowProbe::doc(window)) == 0);
      CHECK(MainWindowProbe::page(window) == "Page 1 of 1");
    }
    window.hide();
    settle();
  }
  g_remove(Glib::build_filename(home, "long.pdf").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("page-seam", kChecks);
}
