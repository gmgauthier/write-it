/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "units.hpp"

#include <string>
#include <vector>

namespace writeit {

// Keys one action owns. A save writes these and leaves every other key.
constexpr unsigned kSetWindow = 1u << 0;
constexpr unsigned kSetZoom = 1u << 1;
constexpr unsigned kSetStandardBar = 1u << 2;
constexpr unsigned kSetFormatBar = 1u << 3;
constexpr unsigned kSetStatusBar = 1u << 4;
constexpr unsigned kSetToolbarsSide = 1u << 5;
constexpr unsigned kSetFont = 1u << 6;
constexpr unsigned kSetSize = 1u << 7;
constexpr unsigned kSetRecentCount = 1u << 8;
constexpr unsigned kSetLastDir = 1u << 9;
constexpr unsigned kSetRecent = 1u << 10;
constexpr unsigned kSetUnits = 1u << 11;
constexpr unsigned kSetDictionary = 1u << 12;

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
  // Tools > Options... "Dictionary". The aspell dictionary Spelling… uses.
  std::string dictionary = "en";

  void load();
  void save() const;
  void load_from(const std::string& path);
  void save_to(const std::string& path) const;
  // Writes these keys only. The rest of the file is read back first, so one
  // window does not replace another window's zoom, units, or frame.
  void save_fields(unsigned fields) const;
  void save_fields_to(const std::string& path, unsigned fields) const;

  static std::string config_path();
};

// The saved size made to fit the current screen: no wider or taller than its
// work area. A side of the area that is 0 (unknown) leaves that side alone.
void clamp_window(int& width, int& height, int area_width, int area_height);

// Shrink the client so it plus the window-manager frame stays inside the
// work area. The frame is the decoration outside the client. A zero frame
// is one the window manager has not reported, and is not guessed. A zero
// area side is unknown and left alone. The window never grows, and a shrink
// leaves at least one pixel.
void fit_window_frame(int& width, int& height, int area_width, int area_height, int frame_width,
                      int frame_height);

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
