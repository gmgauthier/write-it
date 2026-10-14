/* SPDX-License-Identifier: Unlicense */

// A numbered label follows the item's first character, including underline.
// The layout cache key includes underline, so a plain "1." is not reused.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace writeit {

struct MainWindowProbe {
  static Glib::RefPtr<Pango::Layout> label(MainWindow& window, bool underline, int& width,
                                           int& gap)
  {
    Paragraph paragraph;
    paragraph.list.kind = ListKind::Number;
    Run run;
    run.text = "Item";
    run.font = "Sans";
    run.size = 11;
    run.underline = underline;
    paragraph.runs.push_back(run);
    return window.list_label_layout(paragraph, 0, 1, width, gap);
  }
};

}  // namespace writeit

namespace {

bool layout_underlined(const Glib::RefPtr<Pango::Layout>& layout)
{
  if (!layout)
    return false;
  Pango::AttrList attrs = layout->get_attributes();
  if (!attrs)
    return false;
  PangoAttrIterator* iter = pango_attr_list_get_iterator(attrs.gobj());
  if (!iter)
    return false;
  bool found = false;
  do {
    PangoAttribute* attr = pango_attr_iterator_get(iter, PANGO_ATTR_UNDERLINE);
    if (attr && reinterpret_cast<PangoAttrInt*>(attr)->value == PANGO_UNDERLINE_SINGLE)
      found = true;
  } while (pango_attr_iterator_next(iter));
  pango_attr_iterator_destroy(iter);
  return found;
}

}  // namespace

constexpr int kChecks = 7;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-label-underline-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "list-label-underline: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  writeit::Run plain;
  plain.underline = false;
  writeit::Run marked = plain;
  marked.underline = true;
  CHECK(!writeit::label_underlined(plain));
  CHECK(writeit::label_underlined(marked));
  const std::string plain_key = writeit::list_label_key("1.", "Sans", 11, false, false, false);
  const std::string under_key = writeit::list_label_key("1.", "Sans", 11, false, false, true);
  CHECK(plain_key != under_key);
  CHECK(plain_key == writeit::list_label_key("1.", "Sans", 11, false, false, false));
  CHECK(under_key != writeit::list_label_key("1.", "Sans", 11, true, false, false));

  writeit::MainWindow window;
  window.show();
  int width = 0;
  int gap = 0;
  // Plain first. A cache key that omits underline would hand this layout back
  // for the underlined item.
  auto plain_layout = writeit::MainWindowProbe::label(window, false, width, gap);
  auto under_layout = writeit::MainWindowProbe::label(window, true, width, gap);
  CHECK(!layout_underlined(plain_layout));
  CHECK(layout_underlined(under_layout));

  return suite_test::done("list-label-underline", kChecks);
}
