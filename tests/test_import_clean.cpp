/* SPDX-License-Identifier: Unlicense */

// Opening a .txt or .md file, as Word 97 opens a text file: the document is
// unmodified until it is edited. The title has no "*", Close and closing the
// window ask nothing, and the first edit marks it. The file is not RTF, so
// Save still goes through Save As and offers the name with .rtf.
//
// The real window under a display (CI runs the suite in xvfb-run), driven
// through File > Open…, File > Open Recent, File > Save and File > Close.
// Dialogs are answered by a watcher that runs inside their own loops.

#include "check.hpp"
#include "main_window.hpp"
#include "settings.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

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

Gtk::MenuItem* find_item(const std::vector<Gtk::Widget*>& children, const Glib::ustring& label)
{
  for (Gtk::Widget* child : children) {
    auto* item = dynamic_cast<Gtk::MenuItem*>(child);
    if (item && item->get_label() == label)
      return item;
  }
  return nullptr;
}

// A top-level menu's item by its label, or nullptr.
Gtk::MenuItem* menu_item(Gtk::Window& window, const char* menu, const char* label)
{
  Gtk::MenuItem* found = nullptr;
  walk(window, [&](Gtk::Widget& widget) {
    auto* bar = dynamic_cast<Gtk::MenuBar*>(&widget);
    if (!bar || found)
      return;
    Gtk::MenuItem* top = find_item(bar->get_children(), menu);
    if (top && top->get_submenu())
      found = find_item(top->get_submenu()->get_children(), label);
  });
  return found;
}

Gtk::MenuItem* recent_item(Gtk::Window& window, const char* name)
{
  Gtk::MenuItem* recent = menu_item(window, "_File", "Open _Recent");
  if (!recent || !recent->get_submenu())
    return nullptr;
  return find_item(recent->get_submenu()->get_children(), name);
}

Gtk::TextView* text_view(Gtk::Window& window)
{
  Gtk::TextView* found = nullptr;
  walk(window, [&found](Gtk::Widget& widget) {
    if (!found)
      found = dynamic_cast<Gtk::TextView*>(&widget);
  });
  return found;
}

// Answers whatever dialog comes up next, from inside its run() loop, and
// remembers what it saw.
struct Watcher {
  int questions = 0;  // the Save / Don't Save / Cancel question
  int save_choosers = 0;
  int other = 0;
  std::string question;
  std::string suggested;  // the Save As chooser's name
  // What to do: answer the question Cancel (or Don't Save once), and the Save As chooser Cancel or
  // save to `save_to`; pick `open` in an Open chooser.
  std::string save_to;
  std::string open;
  bool discard = false;  // answer the next question Don't Save
  int open_wait = 0;
  // Choosers already answered, which stay up until their caller returns.
  std::set<Gtk::Window*> answered;
  sigc::connection tick;

  void start()
  {
    tick = Glib::signal_timeout().connect(
        [this] {
          poll();
          return true;
        },
        20);
  }
  void stop()
  {
    tick.disconnect();
  }
  // Answered until it goes away. A dialog on the stack can come back at
  // the same address, so forget it when it unmaps.
  void answer(Gtk::Window* top)
  {
    if (answered.insert(top).second)
      top->signal_unmap().connect([this, top] { answered.erase(top); });
  }
  void poll()
  {
    // A chooser stays up while its file is opened or saved, so a question
    // asked then sits above it: answer questions first.
    for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
      auto* question_dialog = dynamic_cast<Gtk::MessageDialog*>(top);
      if (question_dialog && question_dialog->get_visible()) {
        ++questions;
        question = question_dialog->property_text().get_value();
        question_dialog->response(discard ? Gtk::RESPONSE_REJECT : Gtk::RESPONSE_CANCEL);
        discard = false;
        return;
      }
    }
    for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
      if (!top->get_visible() || dynamic_cast<Gtk::MessageDialog*>(top) || answered.count(top))
        continue;
      if (auto* chooser = dynamic_cast<Gtk::FileChooserDialog*>(top)) {
        if (chooser->get_action() == Gtk::FILE_CHOOSER_ACTION_SAVE) {
          if (save_to.empty()) {
            ++save_choosers;
            suggested = chooser->get_current_name();
            answer(top);
            chooser->response(Gtk::RESPONSE_CANCEL);
            return;
          }
          // Name the file, let the folder load, then save.
          if (open_wait == 0) {
            ++save_choosers;
            suggested = chooser->get_current_name();
            chooser->set_current_folder(Glib::path_get_dirname(save_to));
            chooser->set_current_name(Glib::path_get_basename(save_to));
          }
          if (++open_wait > 25) {
            answer(top);
            chooser->response(Gtk::RESPONSE_ACCEPT);
            open_wait = 0;
          }
          return;
        }
        // The open chooser: select the file, let the folder load (the
        // chooser ignores Open until it has), then accept.
        if (open_wait == 0)
          chooser->select_filename(open);
        if (++open_wait > 25) {
          answer(top);
          chooser->response(Gtk::RESPONSE_ACCEPT);
          open_wait = 0;
        }
        return;
      }
      if (dynamic_cast<Gtk::Dialog*>(top)) {
        ++other;
        static_cast<Gtk::Dialog*>(top)->response(Gtk::RESPONSE_CANCEL);
        return;
      }
    }
  }
};

void activate(Gtk::MenuItem* item)
{
  if (item)
    item->activate();
  settle();
}

void type(Gtk::TextView& view, const char* text)
{
  auto buffer = view.get_buffer();
  buffer->begin_user_action();
  buffer->insert_at_cursor(text);
  buffer->end_user_action();
  settle();
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 45;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-import-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  const std::string txt = Glib::build_filename(home, "notes.txt");
  const std::string md = Glib::build_filename(home, "readme.md");
  const std::string rtf = Glib::build_filename(home, "letter.rtf");
  const std::string saved = Glib::build_filename(home, "notes.rtf");
  const std::string md_saved = Glib::build_filename(home, "readme.rtf");
  // The originals, to compare byte for byte after each Save As.
  const std::string txt_bytes = "First line.\nSecond line.\n";
  const std::string md_bytes = "# Title\n\nSome *Markdown* text.\n";
  Glib::file_set_contents(txt, txt_bytes);
  Glib::file_set_contents(md, md_bytes);
  Glib::file_set_contents(rtf,
                          "{\\rtf1\\ansi{\\fonttbl{\\f0 Sans;}}\\pard\\f0\\fs22 A letter.\\par}");
  {
    writeit::Settings settings;
    settings.recent = {txt, md, rtf};
    settings.save();
  }
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "import-clean: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  Watcher watcher;
  watcher.start();
  // A dialog nobody answers would hang the suite: fail instead.
  Glib::signal_timeout().connect_seconds(
      [] {
        std::cerr << "import-clean: stuck in a dialog\n";
        std::exit(EXIT_FAILURE);
        return false;
      },
      45);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    Gtk::TextView* view = text_view(window);
    CHECK(view != nullptr);
    Gtk::MenuItem* close = menu_item(window, "_File", "_Close");
    Gtk::MenuItem* save = menu_item(window, "_File", "_Save");
    Gtk::MenuItem* open = menu_item(window, "_File", "_Open…");
    CHECK(close && save && open);

    // Open Recent, a .txt: no "*", and Close asks nothing.
    activate(recent_item(window, "notes.txt"));
    CHECK(window.get_title() == "Write-It - notes.txt");
    CHECK(view && view->get_buffer()->get_text().find("Second line.") != Glib::ustring::npos);
    activate(close);
    CHECK(watcher.questions == 0);
    CHECK(window.get_title() == "Write-It - Untitled");

    // Open Recent, a .md: the same.
    activate(recent_item(window, "readme.md"));
    CHECK(window.get_title() == "Write-It - readme.md");
    activate(close);
    CHECK(watcher.questions == 0);
    CHECK(window.get_title() == "Write-It - Untitled");

    // File > Open…, a .txt: the same.
    watcher.open = txt;
    activate(open);
    CHECK(window.get_title() == "Write-It - notes.txt");
    CHECK(watcher.questions == 0);

    // Opening another file over it asks nothing either.
    activate(recent_item(window, "letter.rtf"));
    CHECK(watcher.questions == 0);
    CHECK(window.get_title() == "Write-It - letter.rtf");
    activate(recent_item(window, "notes.txt"));
    CHECK(watcher.questions == 0);
    CHECK(window.get_title() == "Write-It - notes.txt");

    // The first edit marks it, and then Close asks, naming the file.
    if (view) {
      view->get_buffer()->place_cursor(view->get_buffer()->end());
      type(*view, "More.");
    }
    CHECK(window.get_title() == "Write-It - notes.txt *");
    activate(close);
    CHECK(watcher.questions == 1);
    CHECK(watcher.question == "Save changes to notes.txt?");
    // Cancelled: still there, still marked.
    CHECK(window.get_title() == "Write-It - notes.txt *");

    // Don't Save this time: Close discards the edit.
    watcher.discard = true;
    activate(close);
    CHECK(watcher.questions == 2);
    CHECK(window.get_title() == "Write-It - Untitled");

    // Back to an unedited import. Save is Save As, offering notes.rtf, since
    // the file is not RTF. Cancelled, nothing changes.
    activate(recent_item(window, "notes.txt"));
    const int asked = watcher.questions;
    CHECK(window.get_title() == "Write-It - notes.txt");
    activate(save);
    CHECK(watcher.save_choosers == 1);
    CHECK(watcher.suggested == "notes.rtf");
    CHECK(window.get_title() == "Write-It - notes.txt");
    CHECK(!Glib::file_test(saved, Glib::FILE_TEST_EXISTS));

    // Saved through Save As: the RTF file is written and the window is it.
    watcher.save_to = saved;
    activate(save);
    CHECK(watcher.save_choosers == 2);
    CHECK(Glib::file_test(saved, Glib::FILE_TEST_EXISTS));
    CHECK(Glib::file_test(saved, Glib::FILE_TEST_EXISTS) &&
          Glib::file_get_contents(saved).compare(0, 6, "{\\rtf1") == 0);
    CHECK(window.get_title() == "Write-It - notes.rtf");
    // The text file is still there, byte for byte.
    CHECK(Glib::file_test(txt, Glib::FILE_TEST_IS_REGULAR));
    CHECK(Glib::file_test(txt, Glib::FILE_TEST_IS_REGULAR) &&
          Glib::file_get_contents(txt) == txt_bytes);
    CHECK(watcher.questions == asked);

    // The same for Markdown: the first Save is Save As, offering readme.rtf,
    // and the .md file stays as it was.
    activate(recent_item(window, "readme.md"));
    CHECK(window.get_title() == "Write-It - readme.md");
    watcher.save_to = md_saved;
    activate(save);
    CHECK(watcher.save_choosers == 3);
    CHECK(watcher.suggested == "readme.rtf");
    CHECK(Glib::file_test(md_saved, Glib::FILE_TEST_EXISTS) &&
          Glib::file_get_contents(md_saved).compare(0, 6, "{\\rtf1") == 0);
    CHECK(window.get_title() == "Write-It - readme.rtf");
    CHECK(Glib::file_test(md, Glib::FILE_TEST_IS_REGULAR));
    CHECK(Glib::file_test(md, Glib::FILE_TEST_IS_REGULAR) &&
          Glib::file_get_contents(md) == md_bytes);
    CHECK(watcher.questions == asked);

    // Closing the window over an unedited import asks nothing.
    activate(recent_item(window, "readme.md"));
    CHECK(window.get_title() == "Write-It - readme.md");
    window.close();
    settle();
    CHECK(watcher.questions == asked);
    CHECK(!window.get_visible());
    CHECK(watcher.other == 0);
  }
  watcher.stop();
  for (const auto& file : {txt, md, rtf, saved, md_saved})
    g_remove(file.c_str());
  g_remove(Glib::build_filename(home, "write-it", "write-it.ini").c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("import-clean", kChecks);
}
