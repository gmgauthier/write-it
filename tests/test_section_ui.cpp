/* SPDX-License-Identifier: Unlicense */

// Section breaks in the real window. The Section Break item opens a dialog,
// so the suite calls insert_section_break() and does not activate that item.
// Page Break stays a command and is activated.

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

  static std::string message(MainWindow& window)
  {
    return window.message_.get_text();
  }

  static void undo(MainWindow& window)
  {
    window.undo();
  }

  static void redo(MainWindow& window)
  {
    window.redo();
  }

  static void caret(MainWindow& window, int offset)
  {
    auto buffer = window.buffer_;
    const int count = buffer->get_char_count();
    const int at = offset < 0 ? 0 : offset > count ? count : offset;
    buffer->place_cursor(buffer->get_iter_at_offset(at));
  }

  static int cursor(MainWindow& window)
  {
    return window.cursor_offset();
  }

  static int paper_width(MainWindow& window)
  {
    return window.page_setup_.paper_width;
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

  static void set_story(Gtk::TextView& view, const char* text)
  {
    auto buffer = view.get_buffer();
    if (!buffer)
      return;
    buffer->begin_user_action();
    buffer->set_text(text);
    buffer->end_user_action();
  }

  static void set_header(MainWindow& window, const char* text)
  {
    set_story(window.header_view_, text);
  }

  static bool sheet(MainWindow& window, int offset)
  {
    return window.starts_sheet(offset);
  }

  static void insert(MainWindow& window, SectionBreak kind)
  {
    window.insert_section_break(kind);
  }

  static void assign(MainWindow& window, Document& doc, PageSetup page)
  {
    window.assign_section_page(doc, page);
  }

  static void commit(MainWindow& window, const Document& before, const Document& after, int caret)
  {
    window.commit_document(before, after, caret);
  }

  static void focus_table_cell(MainWindow& window, int which, int row, int column)
  {
    Gtk::TextView* view = window.nth_cell_view(which, row, column);
    if (!view || !view->get_buffer())
      return;
    view->grab_focus();
    auto buffer = view->get_buffer();
    buffer->place_cursor(buffer->get_char_count() > 0 ? buffer->end() : buffer->begin());
  }

  static void focus_body_quietly(MainWindow& window)
  {
    window.text_.grab_focus();
  }

  static bool cell_focused(MainWindow& window, int which, int row, int column)
  {
    Gtk::TextView* view = window.nth_cell_view(which, row, column);
    return view && view->has_focus();
  }

  static void type(MainWindow& window, const char* text)
  {
    auto buffer = window.buffer_;
    if (!buffer)
      return;
    buffer->begin_user_action();
    buffer->insert_at_cursor(text);
    buffer->end_user_action();
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

void activate(Gtk::MenuItem* item)
{
  if (item)
    item->activate();
  settle();
}

writeit::Paragraph body(const char* text)
{
  writeit::Paragraph paragraph;
  writeit::Run run;
  run.text = text;
  paragraph.runs.push_back(run);
  return paragraph;
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

bool any_section(const writeit::Document& doc)
{
  if (!doc.sections.empty())
    return true;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.section_break != writeit::SectionBreak::None)
      return true;
  }
  return false;
}

bool any_page_break(const writeit::Document& doc)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.page_break)
      return true;
  }
  return false;
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

std::string cell_text(const writeit::Document& doc, int row, int column)
{
  std::string out;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0 || paragraph.cell.row != row || paragraph.cell.column != column)
      continue;
    for (const writeit::Run& run : paragraph.runs)
      out += run.text;
  }
  return out;
}

using Probe = writeit::MainWindowProbe;

}  // namespace

// Exactly the checks this suite runs. Update it with the tests.
constexpr int kChecks = 185;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-section-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "section-ui: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();

    const auto insert = menu_items(window, "_Insert");
    Gtk::MenuItem* picture = item_named(insert, "_Picture…");
    Gtk::MenuItem* table = item_named(insert, "_Table…");
    Gtk::MenuItem* page_break = item_named(insert, "Page _Break");
    Gtk::MenuItem* section_break = item_named(insert, "_Section Break…");
    Gtk::MenuItem* footnote = item_named(insert, "_Footnote");
    CHECK(picture && picture->get_sensitive());
    CHECK(table && table->get_sensitive());
    CHECK(page_break && page_break->get_sensitive());
    CHECK(section_break && section_break->get_sensitive());
    CHECK(footnote && footnote->get_sensitive());
    int page_at = -1;
    int section_at = -1;
    int note_at = -1;
    for (int i = 0; i < static_cast<int>(insert.size()); ++i) {
      if (insert[static_cast<size_t>(i)] == page_break)
        page_at = i;
      if (insert[static_cast<size_t>(i)] == section_break)
        section_at = i;
      if (insert[static_cast<size_t>(i)] == footnote)
        note_at = i;
    }
    CHECK(page_at >= 0 && section_at == page_at + 1 && note_at == section_at + 1);

    // A one-section file still saves as 1.0 saved it, including through the window.
    writeit::Document hello;
    hello.paragraphs.push_back(body("Hello"));
    Probe::load(window, hello);
    settle();
    writeit::Document plain = Probe::doc(window);
    const std::string plain_rtf = writeit::rtf_export(plain);
    CHECK(story(plain.paragraphs) == "Hello");
    CHECK(!any_section(plain));
    CHECK(!has_word(plain_rtf, "\\sect"));
    CHECK(!has_word(plain_rtf, "\\sectd"));
    CHECK(!has_word(plain_rtf, "\\sbkpage"));
    CHECK(!has_word(plain_rtf, "\\sbknone"));
    CHECK(!has(plain_rtf, "\\pgwsxn"));
    CHECK(!has(plain_rtf, "\\lndscpsxn"));
    CHECK(!has(plain_rtf, "\\paperw"));

    hello.page.paper_width = 15840;
    hello.page.paper_height = 12240;
    hello.page.landscape = true;
    hello.page.columns = 2;
    hello.page.column_gap = 360;
    Probe::load(window, hello);
    settle();
    writeit::Document wide = Probe::doc(window);
    const std::string wide_rtf = writeit::rtf_export(wide);
    CHECK(!any_section(wide));
    CHECK(wide.page.paper_width == 15840 && wide.page.columns == 2 && wide.page.column_gap == 360);
    CHECK(has(wide_rtf, "\\paperw15840"));
    CHECK(has(wide_rtf, "\\cols2"));
    CHECK(!has_word(wide_rtf, "\\sect"));
    CHECK(!has(wide_rtf, "\\pgwsxn"));

    // Page Break stays a direct command. It does not start a section.
    Probe::load(window, hello);
    settle();
    Probe::caret(window, 0);
    activate(page_break);
    writeit::Document broken = Probe::doc(window);
    CHECK(broken.paragraphs.size() == 1 && broken.paragraphs[0].page_break);
    CHECK(!any_section(broken));
    CHECK(Probe::page(window) == "Page 2 of 2");
    CHECK(Probe::message(window).empty());

    // Next page copies the section and starts a sheet, without a page break.
    writeit::Document letter;
    letter.page = hello.page;
    letter.header.push_back(body("H"));
    letter.footer.push_back(body("F"));
    letter.paragraphs.push_back(body("Alpha"));
    letter.paragraphs.push_back(body("Beta"));
    const int beta = writeit::chars_before(letter.paragraphs, 1);
    Probe::load(window, letter);
    settle();
    Probe::caret(window, beta);
    settle();
    Probe::insert(window, writeit::SectionBreak::NextPage);
    settle();
    writeit::Document next = Probe::doc(window);
    const std::string next_rtf = writeit::rtf_export(next);
    const int next_at = Probe::cursor(window);
    CHECK(next.sections.size() == 1);
    CHECK(next.sections.size() == 1 &&
          next.sections[0].break_before == writeit::SectionBreak::NextPage);
    CHECK(next.paragraphs.size() == 2);
    CHECK(next.paragraphs.size() == 2 &&
          next.paragraphs[0].section_break == writeit::SectionBreak::None);
    CHECK(next.paragraphs.size() == 2 &&
          next.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
    CHECK(!any_page_break(next));
    CHECK(next.sections.size() == 1 && next.sections[0].page.columns == 2 &&
          next.sections[0].page.column_gap == 360 && next.sections[0].page.paper_width == 15840);
    CHECK(next.sections.size() == 1 && story(next.sections[0].header) == "H" &&
          story(next.sections[0].footer) == "F");
    CHECK(story(next.header) == "H" && story(next.footer) == "F");
    CHECK(next.page.columns == 2 && next.page.paper_width == 15840);
    CHECK(Probe::sheet(window, next_at));
    CHECK(!Probe::sheet(window, 0));
    CHECK(Probe::page(window) == "Page 2 of 2");
    CHECK(Probe::message(window).empty());
    CHECK(has_word(next_rtf, "\\sect"));
    CHECK(has_word(next_rtf, "\\sectd"));
    CHECK(has_word(next_rtf, "\\sbkpage"));
    CHECK(has(next_rtf, "\\pgwsxn"));
    CHECK(has_word(next_rtf, "\\cols"));
    CHECK(!has_word(next_rtf, "\\page"));

    // Undo drops the break. The slots may linger; a later break still works.
    Probe::undo(window);
    settle();
    writeit::Document undone = Probe::doc(window);
    CHECK(!any_section(undone));
    CHECK(story(undone.paragraphs) == "Alpha\nBeta");
    CHECK(!Probe::sheet(window, 0));
    CHECK(!Probe::sheet(window, beta));
    CHECK(Probe::page(window) == "Page 1 of 1");
    Probe::caret(window, beta);
    settle();
    Probe::insert(window, writeit::SectionBreak::NextPage);
    settle();
    writeit::Document again = Probe::doc(window);
    CHECK(again.sections.size() == 1 &&
          again.sections[0].break_before == writeit::SectionBreak::NextPage);
    CHECK(Probe::sheet(window, Probe::cursor(window)));
    CHECK(Probe::page(window) == "Page 2 of 2");
    Probe::undo(window);
    settle();
    Probe::redo(window);
    settle();
    writeit::Document redone = Probe::doc(window);
    CHECK(redone.sections.size() == 1 &&
          redone.sections[0].break_before == writeit::SectionBreak::NextPage);
    CHECK(redone.paragraphs.size() == 2 &&
          redone.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
    CHECK(Probe::page(window) == "Page 2 of 2");

    // The same kind is refused. A different kind replaces the break.
    const int broke = writeit::chars_before(redone.paragraphs, 1);
    Probe::caret(window, broke);
    settle();
    Probe::insert(window, writeit::SectionBreak::NextPage);
    settle();
    writeit::Document same = Probe::doc(window);
    CHECK(same.sections.size() == 1);
    CHECK(same.paragraphs.size() == 2 &&
          same.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
    Probe::insert(window, writeit::SectionBreak::Continuous);
    settle();
    writeit::Document flowed = Probe::doc(window);
    const std::string flowed_rtf = writeit::rtf_export(flowed);
    CHECK(flowed.sections.size() == 1 &&
          flowed.sections[0].break_before == writeit::SectionBreak::Continuous);
    CHECK(flowed.paragraphs.size() == 2 &&
          flowed.paragraphs[1].section_break == writeit::SectionBreak::Continuous);
    CHECK(!any_page_break(flowed));
    CHECK(!Probe::sheet(window, Probe::cursor(window)));
    CHECK(Probe::page(window) == "Page 1 of 1");
    CHECK(has_word(flowed_rtf, "\\sbknone"));
    CHECK(!has_word(flowed_rtf, "\\sbkpage"));

    // A break in the middle splits the paragraph and does not start a sheet.
    Probe::load(window, hello);
    settle();
    Probe::caret(window, 2);
    Probe::insert(window, writeit::SectionBreak::Continuous);
    settle();
    writeit::Document split = Probe::doc(window);
    CHECK(split.paragraphs.size() == 2 && story(std::vector<writeit::Paragraph>{split.paragraphs[0]}) == "He");
    CHECK(split.paragraphs.size() == 2 &&
          story(std::vector<writeit::Paragraph>{split.paragraphs[1]}) == "llo");
    CHECK(split.sections.size() == 1 &&
          split.sections[0].break_before == writeit::SectionBreak::Continuous);
    CHECK(split.paragraphs.size() == 2 &&
          split.paragraphs[0].section_break == writeit::SectionBreak::None &&
          split.paragraphs[1].section_break == writeit::SectionBreak::Continuous);
    CHECK(!any_page_break(split));
    CHECK(!Probe::sheet(window, Probe::cursor(window)));
    CHECK(Probe::page(window) == "Page 1 of 1");

    // A break at the start keeps an empty paragraph in the first section.
    writeit::Document start_doc;
    start_doc.paragraphs.push_back(body("Hello"));
    Probe::load(window, start_doc);
    settle();
    Probe::caret(window, 0);
    Probe::insert(window, writeit::SectionBreak::NextPage);
    settle();
    writeit::Document started = Probe::doc(window);
    CHECK(started.paragraphs.size() == 2 && story(started.paragraphs) == "\nHello");
    CHECK(started.paragraphs.size() == 2 &&
          started.paragraphs[0].section_break == writeit::SectionBreak::None &&
          started.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
    CHECK(started.sections.size() == 1 &&
          started.sections[0].break_before == writeit::SectionBreak::NextPage);
    CHECK(!any_page_break(started));
    CHECK(Probe::sheet(window, Probe::cursor(window)));
    CHECK(!Probe::sheet(window, 0));
    CHECK(Probe::page(window) == "Page 2 of 2");

    // The caret's section supplies the paper, the header, and the footer.
    writeit::Document mixed;
    mixed.header.push_back(body("H1"));
    mixed.footer.push_back(body("F1"));
    mixed.paragraphs.push_back(body("Before"));
    mixed.paragraphs.push_back(body("After"));
    const int after_at = writeit::chars_before(mixed.paragraphs, 1);
    CHECK(writeit::break_section(mixed, after_at, writeit::SectionBreak::NextPage) >= 0);
    CHECK(mixed.sections.size() == 1);
    mixed.sections[0].header = {body("H2")};
    mixed.sections[0].footer = {body("F2")};
    mixed.sections[0].page.paper_width = 14000;
    mixed.sections[0].page.columns = 2;
    mixed.sections[0].page.column_gap = 360;
    Probe::load(window, mixed);
    settle();
    CHECK(Probe::paper_width(window) == writeit::default_page().paper_width);
    CHECK(Probe::header_text(window) == "H1");
    CHECK(Probe::footer_text(window) == "F1");
    CHECK(Probe::page(window) == "Page 1 of 2");
    Probe::caret(window, after_at);
    settle();
    CHECK(Probe::paper_width(window) == 14000);
    CHECK(Probe::header_text(window) == "H2");
    CHECK(Probe::footer_text(window) == "F2");
    CHECK(Probe::page(window) == "Page 2 of 2");
    writeit::Document shown = Probe::doc(window);
    const std::string shown_rtf = writeit::rtf_export(shown);
    CHECK(shown.page.paper_width == writeit::default_page().paper_width && shown.page.columns == 1);
    CHECK(story(shown.header) == "H1" && story(shown.footer) == "F1");
    CHECK(shown.sections.size() == 1 && shown.sections[0].page.paper_width == 14000 &&
          shown.sections[0].page.columns == 2 && shown.sections[0].page.column_gap == 360);
    CHECK(shown.sections.size() == 1 && story(shown.sections[0].header) == "H2" &&
          story(shown.sections[0].footer) == "F2");
    CHECK(has(shown_rtf, "\\pgwsxn14000"));
    CHECK(has(shown_rtf, "\\cols2"));
    CHECK(has(shown_rtf, "H1") && has(shown_rtf, "H2"));
    Probe::caret(window, 0);
    settle();
    CHECK(Probe::paper_width(window) == writeit::default_page().paper_width);
    CHECK(Probe::header_text(window) == "H1");
    CHECK(Probe::footer_text(window) == "F1");

    // A header edit writes the caret's section and undoes there.
    Probe::set_header(window, "H1b");
    settle();
    writeit::Document edited = Probe::doc(window);
    CHECK(story(edited.header) == "H1b");
    CHECK(edited.sections.size() == 1 && story(edited.sections[0].header) == "H2");
    CHECK(Probe::header_text(window) == "H1b");
    Probe::undo(window);
    settle();
    writeit::Document edit_back = Probe::doc(window);
    CHECK(story(edit_back.header) == "H1");
    CHECK(edit_back.sections.size() == 1 && story(edit_back.sections[0].header) == "H2");
    CHECK(Probe::header_text(window) == "H1");
    Probe::caret(window, after_at);
    settle();
    CHECK(Probe::header_text(window) == "H2");
    Probe::set_header(window, "H2b");
    settle();
    writeit::Document second = Probe::doc(window);
    CHECK(story(second.header) == "H1");
    CHECK(second.sections.size() == 1 && story(second.sections[0].header) == "H2b");
    CHECK(Probe::header_text(window) == "H2b");
    Probe::undo(window);
    settle();
    writeit::Document second_back = Probe::doc(window);
    CHECK(second_back.sections.size() == 1 && story(second_back.sections[0].header) == "H2");
    CHECK(story(second_back.header) == "H1");
    CHECK(Probe::header_text(window) == "H2");

    // Page Setup writes the caret's section. One column cannot keep a custom gap.
    writeit::Document before_page = Probe::doc(window);
    writeit::Document after_page = before_page;
    writeit::PageSetup section_page = after_page.sections.empty() ? after_page.page : after_page.sections[0].page;
    section_page.paper_width = 12240;
    section_page.columns = 3;
    section_page.column_gap = 360;
    const int page_caret = Probe::cursor(window);
    Probe::assign(window, after_page, section_page);
    Probe::commit(window, before_page, after_page, page_caret);
    settle();
    writeit::Document paged = Probe::doc(window);
    CHECK(Probe::paper_width(window) == 12240);
    CHECK(paged.page.paper_width == writeit::default_page().paper_width && paged.page.columns == 1);
    CHECK(paged.sections.size() == 1 && paged.sections[0].page.paper_width == 12240 &&
          paged.sections[0].page.columns == 3 && paged.sections[0].page.column_gap == 360);
    Probe::undo(window);
    settle();
    writeit::Document page_back = Probe::doc(window);
    CHECK(Probe::paper_width(window) == 14000);
    CHECK(page_back.sections.size() == 1 && page_back.sections[0].page.paper_width == 14000 &&
          page_back.sections[0].page.columns == 2);
    CHECK(page_back.page.paper_width == writeit::default_page().paper_width);

    Probe::caret(window, 0);
    settle();
    writeit::Document before_first = Probe::doc(window);
    writeit::Document after_first = before_first;
    writeit::PageSetup first_page = after_first.page;
    first_page.paper_width = 15840;
    first_page.columns = 2;
    first_page.column_gap = 360;
    Probe::assign(window, after_first, first_page);
    Probe::commit(window, before_first, after_first, 0);
    settle();
    writeit::Document first_paged = Probe::doc(window);
    CHECK(Probe::paper_width(window) == 15840);
    CHECK(first_paged.page.paper_width == 15840 && first_paged.page.columns == 2 &&
          first_paged.page.column_gap == 360);
    CHECK(first_paged.sections.size() == 1 && first_paged.sections[0].page.paper_width == 14000 &&
          first_paged.sections[0].page.columns == 2);
    Probe::undo(window);
    settle();
    writeit::Document first_back = Probe::doc(window);
    CHECK(Probe::paper_width(window) == writeit::default_page().paper_width);
    CHECK(first_back.page.paper_width == writeit::default_page().paper_width &&
          first_back.page.columns == 1);
    CHECK(first_back.sections.size() == 1 && first_back.sections[0].page.paper_width == 14000);

    Probe::caret(window, after_at);
    settle();
    writeit::Document before_gap = Probe::doc(window);
    writeit::Document after_gap = before_gap;
    writeit::PageSetup gap_page = after_gap.sections.empty() ? after_gap.page : after_gap.sections[0].page;
    gap_page.columns = 1;
    gap_page.column_gap = 100;
    Probe::assign(window, after_gap, gap_page);
    Probe::commit(window, before_gap, after_gap, Probe::cursor(window));
    settle();
    writeit::Document gapped = Probe::doc(window);
    CHECK(gapped.sections.size() == 1 && gapped.sections[0].page.columns == 1 &&
          gapped.sections[0].page.column_gap == 720 && gapped.sections[0].page.paper_width == 14000);
    CHECK(gapped.page.columns == 1 && gapped.page.column_gap == 720 &&
          gapped.page.paper_width == writeit::default_page().paper_width);

    // A section break inside a table is refused. The page-break message stays its own.
    writeit::Document grid;
    writeit::insert_table(grid.paragraphs, 0, 2, 2, writeit::page_text_twips(grid.page));
    const char* names[] = {"A", "B", "C", "D"};
    for (int i = 0; i < 4; ++i) {
      writeit::Run run;
      run.text = names[i];
      grid.paragraphs[static_cast<size_t>(i)].runs.push_back(run);
    }
    Probe::load(window, grid);
    settle();
    Probe::focus_table_cell(window, 0, 1, 1);
    settle();
    CHECK(Probe::cell_focused(window, 0, 1, 1));
    Probe::focus_body_quietly(window);
    settle();
    CHECK(section_break && section_break->get_sensitive());
    Probe::insert(window, writeit::SectionBreak::NextPage);
    settle();
    writeit::Document refused = Probe::doc(window);
    CHECK(Probe::message(window) == "Cannot break inside a table.");
    CHECK(!any_section(refused));
    CHECK(!any_page_break(refused));
    CHECK(cell_text(refused, 1, 1) == "D");
    CHECK(Probe::cell_focused(window, 0, 1, 1));
    CHECK(!Probe::cell_focused(window, 0, 0, 0));
    CHECK(Probe::page(window) == "Page 1 of 1");

    // Typing into a new next-page section keeps the sheet. The break is a pad
    // until the first character, and that character has to carry the mark.
    writeit::Document fresh;
    fresh.paragraphs.push_back(body("Hello"));
    Probe::load(window, fresh);
    settle();
    Probe::caret(window, 5);
    Probe::insert(window, writeit::SectionBreak::NextPage);
    settle();
    writeit::Document opened = Probe::doc(window);
    CHECK(opened.paragraphs.size() == 2 && opened.paragraphs[1].runs.empty());
    CHECK(opened.sections.size() == 1 &&
          opened.sections[0].break_before == writeit::SectionBreak::NextPage);
    CHECK(Probe::page(window) == "Page 2 of 2");
    Probe::type(window, "A");
    settle();
    writeit::Document typed = Probe::doc(window);
    const std::string typed_rtf = writeit::rtf_export(typed);
    const int a_at = writeit::chars_before(typed.paragraphs, 1);
    CHECK(story(typed.paragraphs) == "Hello\nA");
    CHECK(typed.paragraphs.size() == 2 &&
          typed.paragraphs[0].section_break == writeit::SectionBreak::None &&
          typed.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
    CHECK(typed.paragraphs.size() == 2 && !typed.paragraphs[1].page_break);
    CHECK(typed.sections.size() == 1 &&
          typed.sections[0].break_before == writeit::SectionBreak::NextPage);
    CHECK(Probe::page(window) == "Page 2 of 2");
    CHECK(Probe::sheet(window, a_at));
    CHECK(!Probe::sheet(window, 0));
    CHECK(has_word(typed_rtf, "\\sect"));
    CHECK(has_word(typed_rtf, "\\sbkpage"));
    CHECK(!has_word(typed_rtf, "\\page"));

    Probe::undo(window);
    settle();
    writeit::Document typed_back = Probe::doc(window);
    CHECK(story(typed_back.paragraphs) == "Hello\n");
    CHECK(typed_back.paragraphs.size() == 2 && typed_back.paragraphs[1].runs.empty() &&
          typed_back.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
    CHECK(typed_back.sections.size() == 1 &&
          typed_back.sections[0].break_before == writeit::SectionBreak::NextPage);
    CHECK(Probe::page(window) == "Page 2 of 2");
    CHECK(Probe::sheet(window, writeit::chars_before(typed_back.paragraphs, 1)));
    Probe::redo(window);
    settle();
    writeit::Document typed_again = Probe::doc(window);
    CHECK(story(typed_again.paragraphs) == "Hello\nA");
    CHECK(typed_again.paragraphs.size() == 2 &&
          typed_again.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
    CHECK(Probe::page(window) == "Page 2 of 2");

    // Enter at the end of the section adds a paragraph. It does not add a section.
    Probe::type(window, "\nB");
    settle();
    writeit::Document entered = Probe::doc(window);
    CHECK(entered.paragraphs.size() == 3 && story(entered.paragraphs) == "Hello\nA\nB");
    CHECK(entered.paragraphs.size() == 3 &&
          entered.paragraphs[1].section_break == writeit::SectionBreak::NextPage &&
          entered.paragraphs[2].section_break == writeit::SectionBreak::None);
    CHECK(entered.sections.size() == 1);
    CHECK(Probe::page(window) == "Page 2 of 2");
    CHECK(Probe::sheet(window, writeit::chars_before(entered.paragraphs, 1)));
    CHECK(!Probe::sheet(window, writeit::chars_before(entered.paragraphs, 2)));

    // Enter in the middle. The break stays on the first half.
    Probe::load(window, fresh);
    settle();
    Probe::caret(window, 5);
    Probe::insert(window, writeit::SectionBreak::NextPage);
    settle();
    Probe::type(window, "After");
    settle();
    Probe::caret(window, writeit::chars_before(Probe::doc(window).paragraphs, 1) + 2);
    Probe::type(window, "\n");
    settle();
    writeit::Document halved = Probe::doc(window);
    CHECK(halved.paragraphs.size() == 3 && story(halved.paragraphs) == "Hello\nAf\nter");
    CHECK(halved.paragraphs.size() == 3 &&
          halved.paragraphs[1].section_break == writeit::SectionBreak::NextPage &&
          halved.paragraphs[2].section_break == writeit::SectionBreak::None);
    CHECK(halved.sections.size() == 1);
    CHECK(Probe::page(window) == "Page 2 of 2");
    CHECK(Probe::sheet(window, writeit::chars_before(halved.paragraphs, 1)));
    CHECK(!Probe::sheet(window, writeit::chars_before(halved.paragraphs, 2)));

    // Enter at the start. The break stays on the text that moved down.
    Probe::load(window, fresh);
    settle();
    Probe::caret(window, 5);
    Probe::insert(window, writeit::SectionBreak::NextPage);
    settle();
    Probe::type(window, "Go");
    settle();
    Probe::caret(window, writeit::chars_before(Probe::doc(window).paragraphs, 1));
    Probe::type(window, "\n");
    settle();
    writeit::Document above = Probe::doc(window);
    CHECK(above.paragraphs.size() == 3 && story(above.paragraphs) == "Hello\n\nGo");
    CHECK(above.paragraphs.size() == 3 &&
          above.paragraphs[1].section_break == writeit::SectionBreak::None &&
          above.paragraphs[2].section_break == writeit::SectionBreak::NextPage);
    CHECK(above.sections.size() == 1);
    CHECK(Probe::page(window) == "Page 2 of 2");
    CHECK(!Probe::sheet(window, writeit::chars_before(above.paragraphs, 1)));
    CHECK(Probe::sheet(window, writeit::chars_before(above.paragraphs, 2)));

    // A continuous break stays a section and does not start a sheet.
    Probe::load(window, fresh);
    settle();
    Probe::caret(window, 5);
    Probe::insert(window, writeit::SectionBreak::Continuous);
    settle();
    CHECK(Probe::page(window) == "Page 1 of 1");
    Probe::type(window, "C");
    settle();
    writeit::Document flowed_type = Probe::doc(window);
    CHECK(story(flowed_type.paragraphs) == "Hello\nC");
    CHECK(flowed_type.paragraphs.size() == 2 &&
          flowed_type.paragraphs[1].section_break == writeit::SectionBreak::Continuous &&
          !flowed_type.paragraphs[1].page_break);
    CHECK(flowed_type.sections.size() == 1 &&
          flowed_type.sections[0].break_before == writeit::SectionBreak::Continuous);
    CHECK(!Probe::sheet(window, writeit::chars_before(flowed_type.paragraphs, 1)));
    CHECK(Probe::page(window) == "Page 1 of 1");

    // Typing at the start of a section that already has text keeps the break.
    writeit::Document pair;
    pair.paragraphs.push_back(body("Before"));
    pair.paragraphs.push_back(body("After"));
    const int after = writeit::chars_before(pair.paragraphs, 1);
    Probe::load(window, pair);
    settle();
    Probe::caret(window, after);
    Probe::insert(window, writeit::SectionBreak::NextPage);
    settle();
    Probe::caret(window, after);
    Probe::type(window, "X");
    settle();
    writeit::Document prefixed = Probe::doc(window);
    CHECK(story(prefixed.paragraphs) == "Before\nXAfter");
    CHECK(prefixed.paragraphs.size() == 2 &&
          prefixed.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
    CHECK(prefixed.sections.size() == 1 &&
          prefixed.sections[0].break_before == writeit::SectionBreak::NextPage);
    CHECK(Probe::page(window) == "Page 2 of 2");
    CHECK(Probe::sheet(window, writeit::chars_before(prefixed.paragraphs, 1)));
  }
  return suite_test::done("section-ui", kChecks);
}
