/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>

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

  void load();
  void save() const;
  void load_from(const std::string& path);
  void save_to(const std::string& path) const;

  static std::string config_path();
};

}  // namespace writeit
