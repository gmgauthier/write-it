/* SPDX-License-Identifier: Unlicense */

// Header, footer, and note tab stops use the story zoom, which stops at 100%,
// and they are rebuilt when that zoom changes. A later footnote column takes
// a new default interval. A header selection sets every selected paragraph.
// A cell selection shows the paragraph that has the caret.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"
#include "view.hpp"

#include <glib.h>
#include <glibmm/ustring.h>
#include <gtkmm.h>

#include <cstdlib>
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

  static void zoom(MainWindow& window, int percent)
  {
    window.set_zoom(percent);
  }

  static Gtk::TextView& header(MainWindow& window)
  {
    return window.header_view_;
  }

  static Gtk::TextView& footer(MainWindow& window)
  {
    return window.footer_view_;
  }

  static Gtk::TextView* cell(MainWindow& window)
  {
    return window.nth_cell_view(0, 0, 0);
  }

  static void story(MainWindow& window, int target)
  {
    window.story_target_ = target;
  }

  static void apply_tabs(MainWindow& window, std::vector<TabStop> tabs)
  {
    window.apply_focused_tabs(std::move(tabs));
  }

  static ParaFormat focused(MainWindow& window)
  {
    return window.focused_para();
  }

  static Document doc(MainWindow& window)
  {
    return window.capture();
  }

  // The view that shows note `index` once the sheets have split the column.
  // A single column is not this case.
  static Gtk::TextView* note_column(MainWindow& window, int index)
  {
    if (window.note_groups_.size() <= 1)
      return nullptr;
    for (size_t group = 0; group < window.note_groups_.size(); ++group) {
      bool hit = false;
      for (int item : window.note_groups_[group].indexes) {
        if (item == index)
          hit = true;
      }
      if (!hit)
        continue;
      if (group == 0)
        return &window.notes_view_;
      if (group - 1 < window.note_bands_.size() && window.note_bands_[group - 1])
        return &window.note_bands_[group - 1]->view;
      return nullptr;
    }
    return nullptr;
  }

  static int note_group_count(MainWindow& window)
  {
    return static_cast<int>(window.note_groups_.size());
  }

  static void default_tab(MainWindow& window, int twips)
  {
    Document before = window.capture();
    Document after = before;
    after.default_tab = twips;
    window.commit_document(before, after);
  }
};

}  // namespace writeit

namespace {

using writeit::Document;
using writeit::MainWindowProbe;
using writeit::TabAlign;
using writeit::TabLeader;
using writeit::TabStop;

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

writeit::Run text_run(const char* text)
{
  writeit::Run run;
  run.font = "Sans";
  run.size = 11;
  run.text = text;
  return run;
}

writeit::Paragraph line(const char* text)
{
  writeit::Paragraph paragraph;
  paragraph.runs.push_back(text_run(text));
  return paragraph;
}

TabStop stop_at(int position)
{
  TabStop stop;
  stop.position = position;
  stop.align = TabAlign::Left;
  stop.leader = TabLeader::None;
  return stop;
}

double story_factor(int percent)
{
  const double zoom = percent / 100.0;
  return zoom < 1.0 ? zoom : 1.0;
}

int gap_px(int position, int origin, double zoom)
{
  return writeit::twips_to_px(position, zoom) - writeit::twips_to_px(origin, zoom);
}

int story_margin(int indent, double zoom)
{
  const int base = std::max(1, static_cast<int>(std::lround(8.0 * zoom)));
  return std::max(0, base + writeit::twips_to_px(indent, zoom));
}

bool stab_named(Gtk::TextView& view, const std::string& mark, int& tab_px, int& left_margin)
{
  bool found = false;
  auto buffer = view.get_buffer();
  if (!buffer)
    return false;
  buffer->get_tag_table()->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    if (found)
      return;
    const std::string name = tag->property_name().get_value();
    if (name.compare(0, 4, "stab") != 0 || name.find(mark) == std::string::npos)
      return;
    if (!tag->property_tabs_set())
      return;
    Pango::TabArray tabs = tag->property_tabs();
    if (tabs.get_size() < 1)
      return;
    tab_px = tabs.get_tab(0).second;
    left_margin = static_cast<int>(tag->property_left_margin());
    found = true;
  });
  return found;
}

int applied_stab(Gtk::TextView& view)
{
  auto buffer = view.get_buffer();
  if (!buffer || buffer->get_char_count() < 1)
    return -1;
  int px = -1;
  for (const auto& tag : buffer->begin().get_tags()) {
    const std::string name = tag->property_name().get_value();
    if (name.compare(0, 4, "stab") != 0 || !tag->property_tabs_set())
      continue;
    Pango::TabArray tabs = tag->property_tabs();
    if (tabs.get_size() < 1)
      continue;
    px = tabs.get_tab(0).second;
  }
  return px;
}

void expect_footer(writeit::MainWindow& window, int percent)
{
  const double zoom = story_factor(percent);
  const int want_px = gap_px(2160, 720, zoom);
  const int body_px = gap_px(2160, 720, percent / 100.0);
  const int want_margin = story_margin(720, zoom);
  int got_px = -1;
  int got_margin = -1;
  const bool found = stab_named(MainWindowProbe::footer(window), "2160", got_px, got_margin);
  if (!found || got_px != want_px || got_margin != want_margin)
    std::cerr << percent << "% footer tab " << got_px << " want " << want_px << " margin "
              << got_margin << " want " << want_margin << " body " << body_px << "\n";
  CHECK(found);
  CHECK(got_px == want_px);
  CHECK(got_margin == want_margin);
  if (percent > 100)
    CHECK(got_px != body_px);
}

}  // namespace

// 3 setup + 4 at 200% + 3 cell + 3 header + 3 at 50% + 4 at 200% + 3 note columns.
constexpr int kChecks = 23;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-story-tabs-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "story-tabs: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  Document doc = writeit::blank_document("Sans", 11);
  doc.header.push_back(line("One"));
  doc.header.push_back(line("Two"));
  writeit::Paragraph footer = line("A\tB");
  footer.indents.left = 720;
  footer.tabs = {stop_at(2160)};
  doc.footer.push_back(footer);
  doc.notes.push_back({line("N\t1")});
  doc.notes.push_back({line("N\t2")});
  CHECK(writeit::insert_table(doc.paragraphs, doc.paragraphs.size(), 1, 1,
                              writeit::page_text_twips(doc.page)));
  bool duplicated = false;
  for (size_t i = 0; i < doc.paragraphs.size(); ++i) {
    if (doc.paragraphs[i].cell.table == 0)
      continue;
    doc.paragraphs[i].runs = {text_run("alpha")};
    doc.paragraphs[i].tabs = {stop_at(720)};
    writeit::Paragraph second = doc.paragraphs[i];
    second.runs = {text_run("beta")};
    second.tabs = {stop_at(1440)};
    doc.paragraphs.insert(doc.paragraphs.begin() + static_cast<std::ptrdiff_t>(i + 1), second);
    duplicated = true;
    break;
  }
  CHECK(duplicated);
  bool marked = false;
  for (writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table != 0)
      continue;
    paragraph.runs = {text_run("See")};
    paragraph.runs[0].note = 1;
    marked = true;
    break;
  }
  CHECK(marked);
  writeit::Paragraph next = line("Next");
  next.page_break = true;
  next.runs[0].note = 2;
  doc.paragraphs.push_back(next);

  writeit::MainWindow window;
  window.show();
  settle();
  MainWindowProbe::zoom(window, 200);
  settle();
  MainWindowProbe::load(window, doc);
  settle();
  expect_footer(window, 200);

  Gtk::TextView* cell = MainWindowProbe::cell(window);
  CHECK(cell != nullptr);
  if (cell) {
    cell->grab_focus();
    settle();
    auto buffer = cell->get_buffer();
    const bool split = buffer && buffer->get_text().find("\n") != Glib::ustring::npos;
    CHECK(split);
    if (buffer)
      buffer->select_range(buffer->end(), buffer->begin());
    const std::vector<TabStop> shown = MainWindowProbe::focused(window).tabs;
    if (shown.size() != 1 || shown[0].position != 1440)
      std::cerr << "cell caret stop " << (shown.empty() ? 0 : shown[0].position) << "\n";
    CHECK(shown.size() == 1 && shown[0].position == 1440);
  } else {
    CHECK(false);
    CHECK(false);
  }

  Gtk::TextView& header_view = MainWindowProbe::header(window);
  header_view.grab_focus();
  settle();
  MainWindowProbe::story(window, 1);
  auto header = header_view.get_buffer();
  CHECK(static_cast<bool>(header));
  if (header)
    header->select_range(header->begin(), header->end());
  MainWindowProbe::apply_tabs(window, {stop_at(720)});
  const Document headed = MainWindowProbe::doc(window);
  const int first_stop = headed.header.size() > 0 && !headed.header[0].tabs.empty()
                             ? headed.header[0].tabs[0].position
                             : 0;
  const int second_stop = headed.header.size() > 1 && !headed.header[1].tabs.empty()
                              ? headed.header[1].tabs[0].position
                              : 0;
  if (headed.header.size() != 2 || first_stop != 720 || second_stop != 720)
    std::cerr << "header stops " << headed.header.size() << " " << first_stop << " " << second_stop
              << "\n";
  CHECK(headed.header.size() == 2 && !headed.header[0].tabs.empty() &&
        headed.header[0].tabs[0].position == 720);
  CHECK(headed.header.size() == 2 && !headed.header[1].tabs.empty() &&
        headed.header[1].tabs[0].position == 720);

  MainWindowProbe::zoom(window, 50);
  settle();
  expect_footer(window, 50);
  MainWindowProbe::zoom(window, 200);
  settle();
  expect_footer(window, 200);

  Gtk::TextView* second = MainWindowProbe::note_column(window, 1);
  CHECK(second != nullptr);
  const int before = second ? applied_stab(*second) : -1;
  const int at720 = writeit::twips_to_px(720, 1.0);
  if (before != at720)
    std::cerr << "second column " << before << " want " << at720 << " groups "
              << MainWindowProbe::note_group_count(window) << "\n";
  CHECK(before == at720);
  MainWindowProbe::default_tab(window, 1440);
  settle();
  second = MainWindowProbe::note_column(window, 1);
  const int after = second ? applied_stab(*second) : -1;
  const int at1440 = writeit::twips_to_px(1440, 1.0);
  if (after != at1440)
    std::cerr << "second column after " << after << " want " << at1440 << " groups "
              << MainWindowProbe::note_group_count(window) << "\n";
  CHECK(second != nullptr && after == at1440);

  return suite_test::done("story-tabs", kChecks);
}
