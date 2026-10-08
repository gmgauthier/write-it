/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "settings.hpp"

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <cstdlib>
#include <string>

namespace {

std::string temp_ini()
{
  std::string dir = "/tmp/write-it-settings-XXXXXX";
  if (mkdtemp(dir.data()) == nullptr)
    return {};
  return Glib::build_filename(dir, "write-it.ini");
}

}  // namespace

int main()
{
  const std::string path = temp_ini();
  CHECK(!path.empty());
  if (path.empty())
    return suite_test::done("settings");

  writeit::Settings fresh;
  fresh.load_from(path);
  CHECK(fresh.window_width == 960);
  CHECK(fresh.window_height == 700);
  CHECK(fresh.zoom == 100);
  CHECK(fresh.show_standard_toolbar);
  CHECK(fresh.show_format_toolbar);
  CHECK(fresh.show_statusbar);
  CHECK(fresh.toolbars_side_by_side);

  fresh.window_width = 800;
  fresh.window_height = 600;
  fresh.zoom = 0;
  fresh.show_standard_toolbar = false;
  fresh.show_statusbar = false;
  fresh.toolbars_side_by_side = false;
  fresh.save_to(path);

  writeit::Settings loaded;
  loaded.load_from(path);
  CHECK(loaded.window_width == 800);
  CHECK(loaded.window_height == 600);
  CHECK(loaded.zoom == 0);
  CHECK(!loaded.show_standard_toolbar);
  CHECK(loaded.show_format_toolbar);
  CHECK(!loaded.show_statusbar);
  CHECK(!loaded.toolbars_side_by_side);

  const std::string text = Glib::file_get_contents(path);
  CHECK(text.find("window-width=800") != std::string::npos);
  CHECK(text.find("zoom=fit-width") != std::string::npos);
  CHECK(text.find("show-format-toolbar=true") != std::string::npos);
  CHECK(text.find("toolbars-side-by-side=false") != std::string::npos);

  return suite_test::done("settings");
}
