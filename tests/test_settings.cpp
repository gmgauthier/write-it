/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "settings.hpp"

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <cstdio>
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

// Exactly the checks this suite runs on a working temp directory.
constexpr int kChecks = 93;

}  // namespace

int main()
{
  const std::string path = temp_ini();
  CHECK(!path.empty());
  if (path.empty())
    return suite_test::done("settings", kChecks);

  writeit::Settings fresh;
  fresh.load_from(path);
  CHECK(fresh.window_width == 960);
  CHECK(fresh.window_height == 700);
  // No saved setting: Fit width, in a 960 px window, not maximised.
  CHECK(fresh.zoom == 0);
  CHECK(!fresh.window_maximized);
  CHECK(fresh.show_standard_toolbar);
  CHECK(fresh.show_format_toolbar);
  CHECK(fresh.show_statusbar);
  CHECK(fresh.toolbars_side_by_side);
  CHECK(fresh.default_font == "Sans");
  CHECK(fresh.default_size == 11);
  CHECK(fresh.recent_count == 8);
  CHECK(fresh.recent.empty());
  CHECK(fresh.units == writeit::Units::Inches);
  CHECK(fresh.dictionary == "en");

  fresh.window_width = 800;
  fresh.window_height = 600;
  fresh.zoom = 0;
  fresh.window_maximized = true;
  fresh.show_standard_toolbar = false;
  fresh.show_statusbar = false;
  fresh.toolbars_side_by_side = false;
  fresh.default_font = "Times New Roman";
  fresh.default_size = 18;
  fresh.recent_count = 4;
  fresh.last_dir = "/tmp";
  fresh.recent = {"/tmp/a.rtf", "/tmp/b.rtf"};
  fresh.units = writeit::Units::Centimetres;
  fresh.dictionary = "en_GB";
  fresh.save_to(path);

  writeit::Settings loaded;
  loaded.load_from(path);
  CHECK(loaded.window_width == 800);
  CHECK(loaded.window_height == 600);
  CHECK(loaded.zoom == 0);
  CHECK(loaded.window_maximized);
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
  CHECK(loaded.dictionary == "en_GB");

  const std::string text = Glib::file_get_contents(path);
  CHECK(text.find("window-width=800") != std::string::npos);
  CHECK(text.find("zoom=fit-width") != std::string::npos);
  CHECK(text.find("window-maximized=true") != std::string::npos);
  CHECK(text.find("show-format-toolbar=true") != std::string::npos);
  CHECK(text.find("toolbars-side-by-side=false") != std::string::npos);
  CHECK(text.find("recent-count=4") != std::string::npos);
  CHECK(text.find("Times New Roman") != std::string::npos);
  CHECK(text.find("units=cm") != std::string::npos);
  CHECK(text.find("dictionary=en_GB") != std::string::npos);

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
  Glib::file_set_contents(path, "[write-it]\ndictionary=not a name\n");
  {
    writeit::Settings settings;
    settings.load_from(path);
    CHECK(settings.dictionary == "en");
  }
  {
    writeit::Settings inches;
    inches.load_from(path);
    inches.save_to(path);
    CHECK(Glib::file_get_contents(path).find("units=in") != std::string::npos);
  }

  // Window size, maximised state and zoom, as the window leaves them.
  {
    writeit::Settings left;
    left.window_width = 1100;
    left.window_height = 760;
    left.window_maximized = false;
    left.zoom = 150;
    left.save_to(path);
    writeit::Settings back;
    back.load_from(path);
    CHECK(back.window_width == 1100);
    CHECK(back.window_height == 760);
    CHECK(!back.window_maximized);
    CHECK(back.zoom == 150);
    CHECK(Glib::file_get_contents(path).find("window-maximized=false") != std::string::npos);
  }
  // A zoom key that is missing or not a choice is the default, Fit width;
  // each choice reads back.
  auto zoom_after = [&](const std::string& line) {
    Glib::file_set_contents(path, "[write-it]\n" + line + "\n");
    writeit::Settings settings;
    settings.load_from(path);
    return settings.zoom;
  };
  CHECK(zoom_after("window-width=900") == 0);
  CHECK(zoom_after("zoom=") == 0);
  CHECK(zoom_after("zoom=125") == 0);
  CHECK(zoom_after("zoom=fit-width") == 0);
  for (int zoom : {50, 75, 100, 150, 200})
    CHECK(zoom_after("zoom=" + std::to_string(zoom)) == zoom);
  // A maximised flag that is missing or not a boolean is not maximised.
  auto maximized_after = [&](const std::string& line) {
    Glib::file_set_contents(path, "[write-it]\n" + line + "\n");
    writeit::Settings settings;
    settings.load_from(path);
    return settings.window_maximized;
  };
  CHECK(maximized_after("window-maximized=true"));
  CHECK(!maximized_after("window-maximized=false"));
  CHECK(!maximized_after("window-maximized=perhaps"));
  CHECK(!maximized_after("window-width=900"));

  // A corrupt ini, or a size that is not one, is the first launch's: 960 by
  // 700, Fit width, not maximised.
  auto first_launch = [](const writeit::Settings& settings) {
    return settings.window_width == 960 && settings.window_height == 700 && settings.zoom == 0 &&
           !settings.window_maximized;
  };
  auto after = [&](const std::string& text) {
    Glib::file_set_contents(path, text);
    writeit::Settings settings;
    settings.load_from(path);
    return settings;
  };
  CHECK(first_launch(after("")));
  CHECK(first_launch(after("this is not an ini file\n\x01\x02\n")));
  CHECK(first_launch(after("[write-it\nwindow-width=1200\nzoom=150\n")));
  CHECK(first_launch(after("window-width=1200\nzoom=150\n")));
  CHECK(first_launch(after("[other]\nwindow-width=1200\nzoom=150\n")));
  CHECK(
      first_launch(after("[write-it]\nwindow-width=wide\nwindow-height=tall\nzoom=big\n"
                         "window-maximized=perhaps\n")));
  CHECK(first_launch(after("[write-it]\nwindow-width=-5\nwindow-height=0\n")));
  CHECK(first_launch(after("[write-it]\nwindow-width=100\nwindow-height=100\n")));
  CHECK(first_launch(after("[write-it]\nwindow-width=99999999999\nwindow-height=1e9\n")));
  // A missing ini, too.
  std::remove(path.c_str());
  {
    writeit::Settings settings;
    settings.load_from(path);
    CHECK(first_launch(settings));
  }

  // The saved size, clamped to the current screen's work area.
  {
    auto clamp = [](int width, int height, int area_width, int area_height) {
      writeit::clamp_window(width, height, area_width, area_height);
      return std::to_string(width) + "x" + std::to_string(height);
    };
    CHECK(clamp(960, 700, 1280, 1024) == "960x700");
    CHECK(clamp(5000, 4000, 1280, 1024) == "1280x1024");
    CHECK(clamp(1400, 700, 1024, 768) == "1024x700");
    CHECK(clamp(960, 900, 1024, 768) == "960x768");
    CHECK(clamp(960, 700, 800, 600) == "800x600");
    // An unknown area (0) leaves that side alone.
    CHECK(clamp(960, 700, 0, 0) == "960x700");
    CHECK(clamp(1400, 700, 1024, 0) == "1024x700");
  }

  // The size the window remembers is the last it had while not maximised,
  // so a maximised window comes back maximised over its restored size.
  {
    writeit::WindowMemory memory;
    CHECK(memory.width == 960 && memory.height == 700 && !memory.maximized);
    memory.update(1000, 650, false);
    CHECK(memory.width == 1000 && memory.height == 650 && !memory.maximized);
    memory.update(1280, 1024, true);
    CHECK(memory.width == 1000 && memory.height == 650 && memory.maximized);
    memory.update(1280, 1000, true);
    CHECK(memory.width == 1000 && memory.height == 650);
    memory.update(1000, 650, false);
    CHECK(!memory.maximized);
    memory.update(900, 600, false);
    CHECK(memory.width == 900 && memory.height == 600);
    // Not a size: ignored.
    memory.update(0, 0, false);
    CHECK(memory.width == 900 && memory.height == 600);
    // Starting from saved settings.
    writeit::Settings saved;
    saved.window_width = 1100;
    saved.window_height = 760;
    saved.window_maximized = true;
    const writeit::WindowMemory from(saved);
    CHECK(from.width == 1100 && from.height == 760 && from.maximized);
    writeit::Settings into;
    from.store(into);
    CHECK(into.window_width == 1100 && into.window_height == 760 && into.window_maximized);
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
