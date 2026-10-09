/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "units.hpp"

#include <string>
#include <vector>

namespace writeit {

struct Settings {
  int window_width = 960;
  int window_height = 700;
  // 50, 75, 100, 150, 200, or 0 for Fit width.
  int zoom = 100;
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

// Open Recent after opening `path`: it goes first, once, and the list is
// cut to `count`.
std::vector<std::string> push_recent(std::vector<std::string> recent, const std::string& path,
                                     int count);

}  // namespace writeit
