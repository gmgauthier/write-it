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

  static Gtk::TextView& header_view(MainWindow& window)
  {
    return window.header_view_;
  }

  static Gtk::TextView& footer_view(MainWindow& window)
  {
    return window.footer_view_;
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

  static void place_end(MainWindow& window)
  {
    if (!window.buffer_)
      return;
    window.buffer_->place_cursor(window.buffer_->end());
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

// The justification GTK lays out the paragraph at `offset` with: the one set
// by a tag on its first character, or the view's own (left).
Gtk::Justification justification_at(Gtk::TextView& view, int offset)
{
  auto buffer = view.get_buffer();
  Gtk::TextIter iter = buffer->get_iter_at_offset(offset);
  iter.set_line_offset(0);
  Gtk::Justification justification = view.get_justification();
  for (const auto& tag : iter.get_tags()) {
    if (tag->property_justification_set())
      justification = tag->property_justification();
  }
  return justification;
}

// The first character wearing a spacing tag. -1 when the story has none.
int gap_at(Gtk::TextView& view)
{
  auto buffer = view.get_buffer();
  const int count = buffer->get_char_count();
  for (int i = 0; i < count; ++i) {
    for (const auto& tag : buffer->get_iter_at_offset(i).get_tags()) {
      const auto name = tag->property_name().get_value();
      if (name.compare(0, 3, "gap") == 0)
        return i;
    }
  }
  return -1;
}

writeit::Paragraph page_field(writeit::PageSlot slot)
{
  writeit::Paragraph paragraph;
  writeit::Run run;
  run.page = true;
  run.page_slot = slot;
  paragraph.runs.push_back(run);
  paragraph.align = writeit::page_slot_align(slot);
  return paragraph;
}

}  // namespace

// Exactly the checks this suite runs. Update it with the tests.
constexpr int kChecks = 126;

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
    CHECK(justification_at(Probe::footer_view(window), 0) == Gtk::JUSTIFY_RIGHT);
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
    CHECK(justification_at(Probe::header_view(window), 0) == Gtk::JUSTIFY_CENTER);
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

    // The number is Normal's face, not the Sans 11 a run starts as.
    writeit::Document times;
    times.styles = writeit::builtin_styles("Times", 12);
    times.paragraphs.push_back(body("Hello"));
    Probe::load(window, times);
    settle();
    writeit::PageNumberChoice times_choice;
    Probe::insert(window, times_choice);
    settle();
    doc = Probe::doc(window);
    const writeit::Run* times_footer = field_in(doc.footer);
    CHECK(times_footer && times_footer->font == "Times");
    CHECK(times_footer && times_footer->size == 12);
    CHECK(!Probe::marks(window).empty());
    CHECK(Probe::marks(window).front().footer_font == "Times");
    CHECK(Probe::marks(window).front().footer_size == 12);

    // "10" is two characters. The paragraph after the field starts at 3.
    writeit::Document spaced;
    spaced.paragraphs.push_back(body("Hello"));
    spaced.numbering.restart = true;
    spaced.numbering.start = 10;
    spaced.footer.push_back(page_field(writeit::PageSlot::Right));
    writeit::Paragraph after = body("After");
    after.spacing.before = 240;
    spaced.footer.push_back(after);
    Probe::load(window, spaced);
    settle();
    CHECK(Probe::footer_text(window) == "10\nAfter");
    CHECK(gap_at(Probe::footer_view(window)) == 3);

    // A continuous section on the sheet it shares keeps the count that
    // opened the sheet. Restart at 10 must not become 1.
    writeit::Document shared;
    shared.paragraphs.push_back(body("Alpha"));
    writeit::Paragraph beta = body("Beta");
    beta.section_break = writeit::SectionBreak::Continuous;
    shared.paragraphs.push_back(beta);
    shared.numbering.restart = true;
    shared.numbering.start = 10;
    shared.footer.push_back(page_field(writeit::PageSlot::Right));
    writeit::Section continued;
    continued.break_before = writeit::SectionBreak::Continuous;
    continued.footer.push_back(page_field(writeit::PageSlot::Right));
    shared.sections.push_back(continued);
    Probe::load(window, shared);
    settle();
    settle();
    CHECK(Probe::marks(window).size() == 1);
    CHECK(Probe::sheet_footer(window) == "10");

    // A next-page section that restarts numbers its own sheet, and leaves
    // the sheet before it on the count that opened it.
    writeit::Document next_page;
    next_page.paragraphs.push_back(body("Alpha"));
    writeit::Paragraph gamma = body("Gamma");
    gamma.section_break = writeit::SectionBreak::NextPage;
    next_page.paragraphs.push_back(gamma);
    next_page.numbering.restart = true;
    next_page.numbering.start = 10;
    next_page.footer.push_back(page_field(writeit::PageSlot::Right));
    writeit::Section next;
    next.break_before = writeit::SectionBreak::NextPage;
    next.numbering.restart = true;
    next.numbering.start = 1;
    next.footer.push_back(page_field(writeit::PageSlot::Right));
    next_page.sections.push_back(next);
    Probe::load(window, next_page);
    settle();
    settle();
    CHECK(Probe::marks(window).size() == 2);
    CHECK(Probe::marks(window)[0].footer_label == "10");
    CHECK(Probe::marks(window)[1].footer_label == "1");

    // A page break opens the next sheet. A continuous section that begins
    // later on that sheet leaves it with the count that opened it, and
    // numbers the sheet after that in its own format.
    writeit::Document mid;
    mid.paragraphs.push_back(body("Alpha"));
    writeit::Paragraph opened;
    opened.page_break = true;
    mid.paragraphs.push_back(opened);
    mid.paragraphs.push_back(body("Still"));
    writeit::Paragraph cont = body("Cont");
    cont.section_break = writeit::SectionBreak::Continuous;
    mid.paragraphs.push_back(cont);
    writeit::Paragraph opened_again;
    opened_again.page_break = true;
    mid.paragraphs.push_back(opened_again);
    mid.paragraphs.push_back(body("After"));
    mid.numbering.restart = true;
    mid.numbering.start = 10;
    mid.footer.push_back(page_field(writeit::PageSlot::Right));
    writeit::Section later;
    later.break_before = writeit::SectionBreak::Continuous;
    later.numbering.numeral = writeit::PageNumeral::LowerRoman;
    later.footer.push_back(page_field(writeit::PageSlot::Right));
    mid.sections.push_back(later);
    Probe::load(window, mid);
    settle();
    settle();
    CHECK(Probe::marks(window).size() == 3);
    CHECK(Probe::marks(window)[0].footer_label == "10");
    CHECK(Probe::marks(window)[1].footer_label == "11");
    CHECK(Probe::marks(window)[2].footer_label == "xii");

    // The same sheet is the continuous section's when that section restarts.
    later.numbering.restart = true;
    later.numbering.start = 1;
    mid.sections[0] = later;
    Probe::load(window, mid);
    settle();
    settle();
    CHECK(Probe::marks(window).size() == 3);
    CHECK(Probe::marks(window)[0].footer_label == "10");
    CHECK(Probe::marks(window)[1].footer_label == "i");
    CHECK(Probe::marks(window)[2].footer_label == "ii");

    // Moving onto the next sheet refills the bar. The place and the space
    // after the digits have to follow the new number.
    writeit::Document bar;
    bar.paragraphs.push_back(body("Hello"));
    writeit::Paragraph broken;
    broken.page_break = true;
    bar.paragraphs.push_back(broken);
    bar.paragraphs.push_back(body("Next"));
    bar.numbering.restart = true;
    bar.numbering.start = 9;
    bar.footer.push_back(page_field(writeit::PageSlot::Inside));
    writeit::Paragraph after_field = body("After");
    after_field.spacing.before = 240;
    bar.footer.push_back(after_field);
    Probe::load(window, bar);
    settle();
    settle();
    CHECK(Probe::footer_text(window) == "9\nAfter");
    CHECK(justification_at(Probe::footer_view(window), 0) == Gtk::JUSTIFY_LEFT);
    CHECK(gap_at(Probe::footer_view(window)) == 2);
    Probe::place_end(window);
    settle();
    settle();
    CHECK(Probe::footer_text(window) == "10\nAfter");
    CHECK(justification_at(Probe::footer_view(window), 0) == Gtk::JUSTIFY_RIGHT);
    CHECK(gap_at(Probe::footer_view(window)) == 3);

    // Inside follows the printed number: even is the right, odd is the left.
    writeit::Document inside;
    inside.paragraphs.push_back(body("Hello"));
    inside.numbering.restart = true;
    inside.numbering.start = 2;
    Probe::load(window, inside);
    settle();
    writeit::PageNumberChoice inside_choice;
    inside_choice.slot = writeit::PageSlot::Inside;
    inside_choice.numbering.restart = true;
    inside_choice.numbering.start = 2;
    Probe::insert(window, inside_choice);
    settle();
    CHECK(justification_at(Probe::footer_view(window), 0) == Gtk::JUSTIFY_RIGHT);
    inside_choice.numbering.start = 1;
    Probe::insert(window, inside_choice);
    settle();
    CHECK(justification_at(Probe::footer_view(window), 0) == Gtk::JUSTIFY_LEFT);
  }
  return suite_test::done("page-number-ui", kChecks);
}
