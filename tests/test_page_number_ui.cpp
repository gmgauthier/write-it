/* SPDX-License-Identifier: Unlicense */

// Insert → Page Numbers in the real window. The item opens a dialog, so the
// suite calls insert_page_number() and does not activate that item.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cctype>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

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

  static Document doc(MainWindow& window)
  {
    return window.capture();
  }

  static std::string page(MainWindow& window)
  {
    return window.page_label_.get_text();
  }

  static std::string story_text(Gtk::TextView& view)
  {
    auto buffer = view.get_buffer();
    return buffer ? buffer->get_text().raw() : std::string();
  }

  static std::string header_text(MainWindow& window)
  {
    return story_text(window.header_view_);
  }

  static std::string footer_text(MainWindow& window)
  {
    return story_text(window.footer_view_);
  }

  static void insert(MainWindow& window, const PageNumberChoice& choice)
  {
    window.insert_page_number(choice);
  }

  static void undo(MainWindow& window)
  {
    window.undo();
  }

  static void redo(MainWindow& window)
  {
    window.redo();
  }

  static int sheet_number(MainWindow& window)
  {
    if (window.page_number_marks_.empty())
      return 0;
    return window.page_number_marks_.front().number;
  }

  static std::string sheet_header(MainWindow& window)
  {
    if (window.page_number_marks_.empty())
      return {};
    return window.page_number_marks_.front().header_label;
  }

  static std::string sheet_footer(MainWindow& window)
  {
    if (window.page_number_marks_.empty())
      return {};
    return window.page_number_marks_.front().footer_label;
  }

  static const std::vector<MainWindow::PageNumberMark>& marks(MainWindow& window)
  {
    return window.page_number_marks_;
  }

  static void draw_numbers(MainWindow& window)
  {
    const auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, 16, 16);
    const auto cr = Cairo::Context::create(surface);
    window.draw_page_numbers(cr);
  }

  // Seam offsets in buffer order. A tag-table walk is not this order.
  static std::vector<int> seam_offsets(MainWindow& window)
  {
    std::vector<int> offsets;
    auto iter = window.buffer_->begin();
    while (true) {
      for (const auto& tag : iter.get_tags()) {
        if (page_seam_pixels(tag->property_name().get_value()) > 0) {
          offsets.push_back(iter.get_offset());
          break;
        }
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
    return offsets;
  }
};

}  // namespace writeit

namespace {

void settle()
{
  auto context = Glib::MainContext::get_default();
  for (int round = 0; round < 4; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(10000);
  }
  while (context->pending())
    context->iteration(false);
}

void walk(Gtk::Widget& widget, const std::function<void(Gtk::Widget&)>& visit)
{
  visit(widget);
  if (auto* container = dynamic_cast<Gtk::Container*>(&widget)) {
    for (Gtk::Widget* child : container->get_children())
      walk(*child, visit);
  }
}

std::vector<Gtk::MenuItem*> menu_items(Gtk::Window& window, const char* menu)
{
  std::vector<Gtk::MenuItem*> found;
  walk(window, [&](Gtk::Widget& widget) {
    auto* bar = dynamic_cast<Gtk::MenuBar*>(&widget);
    if (!bar)
      return;
    for (Gtk::Widget* child : bar->get_children()) {
      auto* top = dynamic_cast<Gtk::MenuItem*>(child);
      if (!top || top->get_label() != menu || !top->get_submenu())
        continue;
      for (Gtk::Widget* entry : top->get_submenu()->get_children()) {
        if (auto* item = dynamic_cast<Gtk::MenuItem*>(entry))
          found.push_back(item);
      }
    }
  });
  return found;
}

Gtk::MenuItem* item_named(const std::vector<Gtk::MenuItem*>& items, const char* label)
{
  for (Gtk::MenuItem* item : items) {
    if (item->get_label() == label)
      return item;
  }
  return nullptr;
}

writeit::Paragraph body(const char* text)
{
  writeit::Paragraph paragraph;
  writeit::Run run;
  run.text = text;
  paragraph.runs.push_back(run);
  return paragraph;
}

const writeit::Run* field_in(const std::vector<writeit::Paragraph>& story)
{
  for (const writeit::Paragraph& paragraph : story)
    for (const writeit::Run& run : paragraph.runs)
      if (run.page)
        return &run;
  return nullptr;
}

bool body_has_page(const writeit::Document& doc)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs)
    for (const writeit::Run& run : paragraph.runs)
      if (run.page)
        return true;
  return false;
}

std::string story(const std::vector<writeit::Paragraph>& paragraphs)
{
  std::string out;
  for (size_t i = 0; i < paragraphs.size(); ++i) {
    if (i)
      out.push_back('\n');
    for (const writeit::Run& run : paragraphs[i].runs)
      out += run.text;
  }
  return out;
}

bool has_word(const std::string& text, const std::string& word)
{
  for (size_t at = 0; (at = text.find(word, at)) != std::string::npos;) {
    const size_t end = at + word.size();
    if (end >= text.size() || std::isalpha(static_cast<unsigned char>(text[end])) == 0)
      return true;
    at = end;
  }
  return false;
}

bool has(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}

using Probe = writeit::MainWindowProbe;

}  // namespace

// Exactly the checks this suite runs. Update it with the tests.
constexpr int kChecks = 96;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-pagenum-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "page-number-ui: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.set_default_size(900, 700);
    window.show();
    settle();

    const auto insert = menu_items(window, "_Insert");
    Gtk::MenuItem* section_break = item_named(insert, "_Section Break…");
    Gtk::MenuItem* footnote = item_named(insert, "_Footnote");
    Gtk::MenuItem* numbers = item_named(insert, "Page _Numbers…");
    CHECK(section_break && section_break->get_sensitive());
    CHECK(footnote && footnote->get_sensitive());
    CHECK(numbers && numbers->get_sensitive());
    int section_at = -1;
    int note_at = -1;
    int numbers_at = -1;
    for (int i = 0; i < static_cast<int>(insert.size()); ++i) {
      if (insert[static_cast<size_t>(i)] == section_break)
        section_at = i;
      if (insert[static_cast<size_t>(i)] == footnote)
        note_at = i;
      if (insert[static_cast<size_t>(i)] == numbers)
        numbers_at = i;
    }
    CHECK(section_at >= 0 && note_at == section_at + 1);
    CHECK(note_at >= 0 && numbers_at == note_at + 1);

    writeit::Document start;
    start.paragraphs.push_back(body("Hello"));
    Probe::load(window, start);
    settle();
    CHECK(Probe::page(window) == "Page 1 of 1");

    writeit::PageNumberChoice choice;
    Probe::insert(window, choice);
    settle();
    writeit::Document doc = Probe::doc(window);
    const writeit::Run* footer = field_in(doc.footer);
    CHECK(footer && footer->page_slot == writeit::PageSlot::Right);
    CHECK(!doc.footer.empty() && doc.footer.back().align == writeit::Align::Right);
    CHECK(!field_in(doc.header));
    CHECK(!body_has_page(doc));
    CHECK(story(doc.paragraphs) == "Hello");
    CHECK(!doc.numbering.restart);
    CHECK(doc.numbering.start == 1);
    CHECK(doc.numbering.numeral == writeit::PageNumeral::Arabic);
    CHECK(doc.numbering.show_first);
    CHECK(Probe::page(window) == "Page 1 of 1");
    CHECK(Probe::footer_text(window) == "1");
    CHECK(Probe::header_text(window).empty());
    CHECK(Probe::sheet_number(window) == 1);
    CHECK(Probe::sheet_footer(window) == "1");
    CHECK(Probe::sheet_header(window).empty());
    const std::string rtf = writeit::rtf_export(doc);
    CHECK(has(rtf, "\\pgplace2\\chpgn"));
    CHECK(has(rtf, "Hello"));
    CHECK(!has_word(rtf, "\\sect"));
    CHECK(!has_word(rtf, "\\sectd"));
    CHECK(!has_word(rtf, "\\paperw"));
    CHECK(!has_word(rtf, "\\pgnrestart"));
    CHECK(!has_word(rtf, "\\titlepg"));

    Probe::undo(window);
    settle();
    doc = Probe::doc(window);
    CHECK(!field_in(doc.footer));
    CHECK(!field_in(doc.header));
    CHECK(story(doc.paragraphs) == "Hello");
    CHECK(Probe::footer_text(window).empty());
    CHECK(Probe::sheet_number(window) == 0);
    CHECK(!has_word(writeit::rtf_export(doc), "\\chpgn"));
    CHECK(Probe::page(window) == "Page 1 of 1");

    Probe::redo(window);
    settle();
    doc = Probe::doc(window);
    CHECK(field_in(doc.footer) && field_in(doc.footer)->page_slot == writeit::PageSlot::Right);
    CHECK(Probe::footer_text(window) == "1");
    CHECK(Probe::sheet_footer(window) == "1");

    choice.in_header = true;
    choice.slot = writeit::PageSlot::Center;
    Probe::insert(window, choice);
    settle();
    doc = Probe::doc(window);
    const writeit::Run* header = field_in(doc.header);
    CHECK(header && header->page_slot == writeit::PageSlot::Center);
    CHECK(!doc.header.empty() && doc.header.back().align == writeit::Align::Center);
    CHECK(!field_in(doc.footer));
    CHECK(Probe::header_text(window) == "1");
    CHECK(Probe::footer_text(window).empty());
    CHECK(Probe::sheet_header(window) == "1");
    CHECK(Probe::sheet_footer(window).empty());
    CHECK(Probe::page(window) == "Page 1 of 1");
    CHECK(has(writeit::rtf_export(doc), "\\pgplace1\\chpgn"));

    writeit::PageNumberChoice restart;
    restart.numbering.restart = true;
    restart.numbering.start = 5;
    restart.numbering.numeral = writeit::PageNumeral::LowerRoman;
    restart.numbering.show_first = false;
    Probe::insert(window, restart);
    settle();
    doc = Probe::doc(window);
    CHECK(doc.numbering.restart);
    CHECK(doc.numbering.start == 5);
    CHECK(doc.numbering.numeral == writeit::PageNumeral::LowerRoman);
    CHECK(!doc.numbering.show_first);
    CHECK(field_in(doc.footer) && field_in(doc.footer)->page_slot == writeit::PageSlot::Right);
    CHECK(!field_in(doc.header));
    CHECK(Probe::footer_text(window) == "#");
    CHECK(Probe::sheet_number(window) == 5);
    CHECK(Probe::sheet_footer(window).empty());
    CHECK(Probe::page(window) == "Page 1 of 1");
    const std::string restarted = writeit::rtf_export(doc);
    CHECK(has(restarted, "\\pgnrestart"));
    CHECK(has(restarted, "\\pgnstarts5"));
    CHECK(has_word(restarted, "\\pgnlcrm"));
    CHECK(has_word(restarted, "\\titlepg"));
    CHECK(has(restarted, "\\pgplace2\\chpgn"));

    Probe::undo(window);
    settle();
    doc = Probe::doc(window);
    CHECK(!doc.numbering.restart);
    CHECK(doc.numbering.start == 1);
    CHECK(doc.numbering.show_first);
    CHECK(doc.numbering.numeral == writeit::PageNumeral::Arabic);
    CHECK(field_in(doc.header) && field_in(doc.header)->page_slot == writeit::PageSlot::Center);
    CHECK(!field_in(doc.footer));
    CHECK(Probe::header_text(window) == "1");
    CHECK(Probe::footer_text(window).empty());
    CHECK(Probe::sheet_number(window) == 1);
    CHECK(Probe::sheet_header(window) == "1");

    Probe::redo(window);
    settle();
    doc = Probe::doc(window);
    CHECK(doc.numbering.restart && doc.numbering.start == 5);
    CHECK(!doc.numbering.show_first);
    CHECK(doc.numbering.numeral == writeit::PageNumeral::LowerRoman);
    CHECK(field_in(doc.footer));
    CHECK(!field_in(doc.header));
    CHECK(Probe::footer_text(window) == "#");
    CHECK(Probe::sheet_number(window) == 5);
    CHECK(Probe::sheet_footer(window).empty());
    CHECK(Probe::page(window) == "Page 1 of 1");

    // Three sheets that do not fill the page the same way, so each seam is
    // its own tag. The number under sheet n belongs to the n-th seam.
    writeit::Document pages;
    pages.paragraphs.push_back(body("Hello"));
    auto gap = [] {
      writeit::Paragraph paragraph;
      paragraph.page_break = true;
      return paragraph;
    };
    pages.paragraphs.push_back(gap());
    for (int i = 0; i < 6; ++i)
      pages.paragraphs.push_back(body("Short"));
    pages.paragraphs.push_back(gap());
    for (int i = 0; i < 18; ++i)
      pages.paragraphs.push_back(body("Middle"));
    pages.paragraphs.push_back(gap());
    pages.paragraphs.push_back(body("End"));
    Probe::load(window, pages);
    settle();
    settle();
    writeit::PageNumberChoice pages_choice;
    Probe::insert(window, pages_choice);
    settle();
    settle();
    Probe::draw_numbers(window);
    const std::vector<int> seams = Probe::seam_offsets(window);
    const auto& painted = Probe::marks(window);
    CHECK(seams.size() == 3);
    CHECK(painted.size() == seams.size() + 1);
    CHECK(painted.front().header_seam < 0);
    CHECK(painted.back().footer_seam < 0);
    for (size_t i = 0; i < seams.size(); ++i) {
      CHECK(painted[i].footer_seam == seams[i]);
      CHECK(painted[i + 1].header_seam == seams[i]);
      CHECK(painted[i].footer_label == std::to_string(i + 1));
    }
    CHECK(painted.back().footer_label == "4");
    CHECK(Probe::page(window) == "Page 1 of 4");
  }
  return suite_test::done("page-number-ui", kChecks);
}
