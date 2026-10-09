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

// Every check in main runs on a working temp directory. Fewer means the
// suite returned early.
constexpr int kChecks = 42;

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
  CHECK(fresh.default_font == "Sans");
  CHECK(fresh.default_size == 11);
  CHECK(fresh.recent_count == 8);
  CHECK(fresh.recent.empty());
  CHECK(fresh.units == writeit::Units::Inches);

  fresh.window_width = 800;
  fresh.window_height = 600;
  fresh.zoom = 0;
  fresh.show_standard_toolbar = false;
  fresh.show_statusbar = false;
  fresh.toolbars_side_by_side = false;
  fresh.default_font = "Times New Roman";
  fresh.default_size = 18;
  fresh.recent_count = 4;
  fresh.last_dir = "/tmp";
  fresh.recent = {"/tmp/a.rtf", "/tmp/b.rtf"};
  fresh.units = writeit::Units::Centimetres;
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
  CHECK(loaded.default_font == "Times New Roman");
  CHECK(loaded.default_size == 18);
  CHECK(loaded.recent_count == 4);
  CHECK(loaded.last_dir == "/tmp");
  CHECK(loaded.recent.size() == 2);
  CHECK(loaded.recent[0] == "/tmp/a.rtf");
  CHECK(loaded.units == writeit::Units::Centimetres);

  const std::string text = Glib::file_get_contents(path);
  CHECK(text.find("window-width=800") != std::string::npos);
  CHECK(text.find("zoom=fit-width") != std::string::npos);
  CHECK(text.find("show-format-toolbar=true") != std::string::npos);
  CHECK(text.find("toolbars-side-by-side=false") != std::string::npos);
  CHECK(text.find("recent-count=4") != std::string::npos);
  CHECK(text.find("Times New Roman") != std::string::npos);
  CHECK(text.find("units=cm") != std::string::npos);

  // Measurement units: inches unless the ini says cm. Invalid means inches.
  auto units_after = [&](const std::string& line) {
    Glib::file_set_contents(path, "[write-it]\n" + line + "\n");
    writeit::Settings settings;
    settings.load_from(path);
    return settings.units;
  };
  CHECK(units_after("units=cm") == writeit::Units::Centimetres);
  CHECK(units_after("units=in") == writeit::Units::Inches);
  CHECK(units_after("units=furlongs") == writeit::Units::Inches);
  CHECK(units_after("units=") == writeit::Units::Inches);
  CHECK(units_after("window-width=900") == writeit::Units::Inches);
  {
    writeit::Settings inches;
    inches.load_from(path);
    inches.save_to(path);
    CHECK(Glib::file_get_contents(path).find("units=in") != std::string::npos);
  }

  // View > Page / Draft is not a setting: the Config key list has no view
  // key, so every launch opens in Page, the default. Saving writes none.
  {
    writeit::Settings plain;
    plain.save_to(path);
    const std::string saved = Glib::file_get_contents(path);
    CHECK(saved.find("view") == std::string::npos);
    CHECK(saved.find("draft") == std::string::npos);
  }

  return suite_test::done("settings", kChecks);
}
