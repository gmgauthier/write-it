/* SPDX-License-Identifier: Unlicense */

// Insert → Footnote opens Footnote and Endnote. The default is a footnote.
// Endnote collects the note under the body.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

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

  static std::string story_text(Gtk::TextView& view)
  {
    auto buffer = view.get_buffer();
    return buffer ? buffer->get_text().raw() : std::string();
  }

  static Gtk::Widget* focus(MainWindow& window)
  {
    return window.get_focus();
  }

  static Gtk::TextView& notes_view(MainWindow& window)
  {
    return window.notes_view_;
  }

  static Gtk::TextView& endnote_view(MainWindow& window)
  {
    return window.endnote_view_;
  }

  static Gtk::Widget& endnote_box(MainWindow& window)
  {
    return window.endnote_box_;
  }

  static Gtk::Widget& endnote_rule(MainWindow& window)
  {
    return window.endnote_rule_;
  }

  static Gtk::Widget& note_box(MainWindow& window)
  {
    return window.note_box_;
  }

  static Gtk::Box& page_box(MainWindow& window)
  {
    return window.page_box_;
  }

  static Gtk::Widget& page_overlay(MainWindow& window)
  {
    return window.page_overlay_;
  }

  static Gtk::Widget& text(MainWindow& window)
  {
    return window.text_;
  }

  static std::string gutter(MainWindow& window)
  {
    return window.endnote_label_.get_text();
  }

  static std::string endnote_text(MainWindow& window)
  {
    return story_text(window.endnote_view_);
  }

  static void show_draft(MainWindow& window)
  {
    if (window.draft_item_)
      window.draft_item_->set_active(true);
  }

  static void show_page(MainWindow& window)
  {
    if (window.page_item_)
      window.page_item_->set_active(true);
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

const writeit::Run* marked(const writeit::Document& doc, bool endnote)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    for (const writeit::Run& run : paragraph.runs) {
      if (endnote ? run.endnote != 0 : run.note != 0)
        return &run;
    }
  }
  return nullptr;
}

void answer_dialog(bool choose_endnote)
{
  Glib::signal_idle().connect_once([choose_endnote] {
    Gtk::Dialog* dialog = nullptr;
    for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
      auto* candidate = dynamic_cast<Gtk::Dialog*>(top);
      if (candidate && candidate->get_title() == "Footnote and Endnote")
        dialog = candidate;
    }
    CHECK(dialog != nullptr);
    CHECK(dialog && dialog->get_title() == "Footnote and Endnote");
    Gtk::RadioButton* footnote = nullptr;
    Gtk::RadioButton* endnote = nullptr;
    if (dialog) {
      walk(*dialog, [&](Gtk::Widget& widget) {
        auto* radio = dynamic_cast<Gtk::RadioButton*>(&widget);
        if (!radio)
          return;
        if (radio->get_label() == "Footnote")
          footnote = radio;
        if (radio->get_label() == "Endnote")
          endnote = radio;
      });
    }
    CHECK(footnote && footnote->get_active());
    CHECK(endnote && !endnote->get_active());
    if (choose_endnote && endnote)
      endnote->set_active(true);
    if (dialog)
      dialog->response(Gtk::RESPONSE_OK);
  });
}

using Probe = writeit::MainWindowProbe;

}  // namespace

constexpr int kChecks = 34;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-endnote-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "endnote-ui: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();

    const auto insert = menu_items(window, "_Insert");
    Gtk::MenuItem* item = item_named(insert, "_Footnote");
    CHECK(item && item->get_label() == "_Footnote");
    CHECK(item && item->get_sensitive());

    writeit::Document hello;
    hello.paragraphs.push_back(body("Hello"));
    Probe::load(window, hello);
    settle();
    answer_dialog(false);
    item->activate();
    settle();
    writeit::Document foot = Probe::doc(window);
    CHECK(foot.notes.size() == 1);
    CHECK(foot.endnotes.empty());
    const writeit::Run* foot_mark = marked(foot, false);
    CHECK(foot_mark && foot_mark->text == "1" && foot_mark->note == 1);
    CHECK(Probe::focus(window) == &Probe::notes_view(window));
    CHECK(!Probe::endnote_box(window).get_visible());
    CHECK(Probe::endnote_box(window).get_parent() != &Probe::page_box(window));

    Probe::load(window, hello);
    settle();
    answer_dialog(true);
    item->activate();
    settle();
    writeit::Document end = Probe::doc(window);
    CHECK(end.notes.empty());
    CHECK(end.endnotes.size() == 1);
    const writeit::Run* end_mark = marked(end, true);
    CHECK(end_mark && end_mark->text == "i" && end_mark->endnote == 1 && end_mark->note == 0);
    CHECK(Probe::endnote_box(window).get_visible());
    CHECK(Probe::endnote_box(window).get_parent() == &Probe::page_box(window));
    CHECK(Probe::endnote_box(window).get_parent() != &Probe::page_overlay(window));
    CHECK(!Probe::note_box(window).get_visible());
    CHECK(Probe::gutter(window) == "i");
    CHECK(Probe::endnote_text(window) == " ");
    CHECK(Probe::focus(window) == &Probe::endnote_view(window));
    int text_at = -1;
    int rule_at = -1;
    int box_at = -1;
    const std::vector<Gtk::Widget*> children = Probe::page_box(window).get_children();
    for (int i = 0; i < static_cast<int>(children.size()); ++i) {
      if (children[static_cast<size_t>(i)] == &Probe::text(window))
        text_at = i;
      if (children[static_cast<size_t>(i)] == &Probe::endnote_rule(window))
        rule_at = i;
      if (children[static_cast<size_t>(i)] == &Probe::endnote_box(window))
        box_at = i;
    }
    CHECK(text_at >= 0 && rule_at == text_at + 1 && box_at == text_at + 2);
    Probe::show_draft(window);
    settle();
    CHECK(Probe::endnote_box(window).get_visible());
    CHECK(Probe::endnote_box(window).get_parent() == &Probe::page_box(window));
    Probe::show_page(window);
    settle();

    writeit::Document almost;
    almost.paragraphs.push_back(body("Hello"));
    for (int i = 0; i < 199; ++i)
      almost.notes.push_back({writeit::Paragraph{}});
    Probe::load(window, almost);
    settle();
    CHECK(item->get_sensitive());
    CHECK(item->get_label() == "_Footnote");
    almost.endnotes.push_back({writeit::Paragraph{}});
    Probe::load(window, almost);
    settle();
    CHECK(!item->get_sensitive());

    writeit::Document full;
    full.paragraphs.push_back(body("Hello"));
    for (int i = 0; i < 200; ++i)
      full.notes.push_back({writeit::Paragraph{}});
    Probe::load(window, full);
    settle();
    CHECK(!item->get_sensitive());
    CHECK(item->get_label() == "_Footnote");
  }
  return suite_test::done("endnote-ui", kChecks);
}
