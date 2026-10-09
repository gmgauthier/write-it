/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "units.hpp"

#include <string>
#include <vector>

namespace writeit {

struct Settings {
  int window_width = 960;
  int window_height = 700;
  // Maximised when the window last closed. window_width and window_height
  // are then the size it had before it was maximised.
  bool window_maximized = false;
  // 50, 75, 100, 150, 200, or 0 for Fit width, the first launch's.
  int zoom = 0;
  bool show_standard_toolbar = true;
  bool show_format_toolbar = true;
  bool show_statusbar = true;
  // True puts both toolbars on one row. False stacks the standard bar above the format bar.
  bool toolbars_side_by_side = true;
  std::string default_font = "Sans";
  int default_size = 11;
  // 4, 8, or 12. Open Recent shows this many names.
  int recent_count = 8;
  std::string last_dir;
  std::vector<std::string> recent;
  // Tools > Options... "Measurement units". Stored as units=in or units=cm.
  Units units = Units::Inches;

  void load();
  void save() const;
  void load_from(const std::string& path);
  void save_to(const std::string& path) const;

  static std::string config_path();
};

// The saved size made to fit the current screen: no wider or taller than its
// work area. A side of the area that is 0 (unknown) leaves that side alone.
void clamp_window(int& width, int& height, int area_width, int area_height);

// What the window remembers of itself as it changes: the last size it had
// while not maximised, and whether it is maximised. A maximised window
// comes back maximised, and un-maximises to the size it had before.
struct WindowMemory {
  WindowMemory() = default;
  explicit WindowMemory(const Settings& settings);

  int width = 960;
  int height = 700;
  bool maximized = false;

  // A new size, and whether the window is maximised at it.
  void update(int new_width, int new_height, bool maximized_now);
  // Into the settings that are saved.
  void store(Settings& settings) const;
};

// Open Recent after opening `path`: it goes first, once, and the list is
// cut to `count`.
std::vector<std::string> push_recent(std::vector<std::string> recent, const std::string& path,
                                     int count);

}  // namespace writeit
