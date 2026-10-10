/* SPDX-License-Identifier: Unlicense */

// New from Template loads a shipped RTF starter as a new unsaved document.
// The starter is not the file Save would overwrite.

#include "check.hpp"
#include "main_window.hpp"
#include "templates.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
#include <string>

namespace writeit {

struct MainWindowProbe {
  static bool menu_on(MainWindow& window)
  {
    return window.template_item_ && window.template_item_->get_sensitive();
  }
  static bool apply(MainWindow& window, const std::string& path)
  {
    return window.apply_template(path);
  }
  static bool dirty(MainWindow& window)
  {
    return window.dirty();
  }
  static const std::string& title(MainWindow& window)
  {
    return window.title_name_;
  }
  static const std::string& save_path(MainWindow& window)
  {
    return window.save_path_;
  }
  static const std::string& source_path(MainWindow& window)
  {
    return window.source_path_;
  }
  static Document capture(MainWindow& window)
  {
    return window.capture();
  }
  static const std::vector<std::string>& recent(MainWindow& window)
  {
    return window.settings_.recent;
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

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

std::string document_text(const writeit::Document& doc)
{
  std::string text;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    for (const writeit::Run& run : paragraph.runs)
      text += run.text;
    text += '\n';
  }
  return text;
}

}  // namespace

constexpr int kChecks = 14;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-template-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "template: no display, skipped\n";
    return 77;
  }

  const std::vector<std::string> starters = writeit::shipped_templates();
  CHECK(!starters.empty());
  std::string letter;
  for (const std::string& path : starters) {
    if (Glib::path_get_basename(path) == "Letter.rtf")
      letter = path;
  }
  CHECK(!letter.empty());
  writeit::Document starter;
  CHECK(writeit::rtf_import(Glib::file_get_contents(letter), starter));
  const std::string starter_text = document_text(starter);
  CHECK(starter_text.find("Dear friend,") != std::string::npos);
  CHECK(starter_text.find("Thank you for your letter.") != std::string::npos);
  CHECK(starter_text.find("Yours sincerely,") != std::string::npos);

  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    CHECK(MainWindowProbe::menu_on(window));
    CHECK(MainWindowProbe::apply(window, letter));
    CHECK(MainWindowProbe::title(window) == "Letter.rtf");
    CHECK(MainWindowProbe::dirty(window));
    CHECK(MainWindowProbe::save_path(window).empty());
    CHECK(MainWindowProbe::source_path(window).empty());
    CHECK(document_text(MainWindowProbe::capture(window)).find("Dear friend,") !=
          std::string::npos);
    CHECK(MainWindowProbe::recent(window).empty());
  }
  return suite_test::done("template", kChecks);
}
