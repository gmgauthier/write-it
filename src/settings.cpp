/* SPDX-License-Identifier: Unlicense */

#include "settings.hpp"

#include "spelling.hpp"

#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/keyfile.h>
#include <glibmm/miscutils.h>

#include <algorithm>
#include <vector>

namespace writeit {
namespace {

constexpr const char* kGroup = "write-it";

int get_int(Glib::KeyFile& kf, const char* key, int fallback)
{
  try {
    if (kf.has_key(kGroup, key))
      return kf.get_integer(kGroup, key);
  } catch (const Glib::Error&) {
  }
  return fallback;
}

bool get_bool(Glib::KeyFile& kf, const char* key, bool fallback)
{
  try {
    if (kf.has_key(kGroup, key))
      return kf.get_boolean(kGroup, key);
  } catch (const Glib::Error&) {
  }
  return fallback;
}

std::string get_str(Glib::KeyFile& kf, const char* key)
{
  try {
    if (kf.has_key(kGroup, key))
      return kf.get_string(kGroup, key);
  } catch (const Glib::Error&) {
  }
  return {};
}

int zoom_from_text(const std::string& text, int fallback)
{
  if (text == "fit-width")
    return 0;
  if (text == "50" || text == "75" || text == "100" || text == "150" || text == "200")
    return std::stoi(text);
  return fallback;
}

bool allowed_size(int size)
{
  switch (size) {
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 14:
    case 16:
    case 18:
    case 24:
    case 36:
      return true;
    default:
      return false;
  }
}

std::vector<std::string> get_list(Glib::KeyFile& kf, const char* key)
{
  try {
    if (!kf.has_key(kGroup, key))
      return {};
    std::vector<std::string> out;
    for (const auto& item : kf.get_string_list(kGroup, key))
      out.emplace_back(item);
    return out;
  } catch (const Glib::Error&) {
    return {};
  }
}

}  // namespace

std::string Settings::config_path()
{
  return Glib::build_filename(Glib::get_user_config_dir(), "write-it", "write-it.ini");
}

void Settings::load()
{
  load_from(config_path());
}

void Settings::save() const
{
  save_to(config_path());
}

void Settings::load_from(const std::string& path)
{
  Glib::KeyFile kf;
  try {
    kf.load_from_file(path);
  } catch (const Glib::Error&) {
    return;
  }
  // An empty ini, or one without the [write-it] group, is no settings at
  // all. Without the group KeyFile::has_key throws rather than answering.
  if (!kf.has_group(kGroup))
    return;
  window_width = get_int(kf, "window-width", window_width);
  window_height = get_int(kf, "window-height", window_height);
  window_maximized = get_bool(kf, "window-maximized", window_maximized);
  if (window_width < 320)
    window_width = 960;
  if (window_height < 240)
    window_height = 700;
  const std::string zoom_text = get_str(kf, "zoom");
  if (!zoom_text.empty())
    zoom = zoom_from_text(zoom_text, zoom);
  show_standard_toolbar = get_bool(kf, "show-standard-toolbar", show_standard_toolbar);
  show_format_toolbar = get_bool(kf, "show-format-toolbar", show_format_toolbar);
  show_statusbar = get_bool(kf, "show-statusbar", show_statusbar);
  toolbars_side_by_side = get_bool(kf, "toolbars-side-by-side", toolbars_side_by_side);
  const std::string font = get_str(kf, "default-font");
  if (!font.empty())
    default_font = font;
  const int size = get_int(kf, "default-size", default_size);
  if (allowed_size(size))
    default_size = size;
  const int count = get_int(kf, "recent-count", recent_count);
  if (count == 4 || count == 8 || count == 12)
    recent_count = count;
  const std::string dir = get_str(kf, "last-dir");
  if (!dir.empty())
    last_dir = dir;
  if (kf.has_key(kGroup, "recent"))
    recent = get_list(kf, "recent");
  if (static_cast<int>(recent.size()) > recent_count)
    recent.resize(static_cast<size_t>(recent_count));
  units = units_from_text(get_str(kf, "units"));
  const std::string dictionary_name = get_str(kf, "dictionary");
  if (dictionary_name_ok(dictionary_name))
    dictionary = dictionary_name;
}

void Settings::save_to(const std::string& path) const
{
  const std::string dir = Glib::path_get_dirname(path);
  g_mkdir_with_parents(dir.c_str(), 0700);
  Glib::KeyFile kf;
  try {
    kf.load_from_file(path);
  } catch (const Glib::Error&) {
  }
  kf.set_integer(kGroup, "window-width", window_width);
  kf.set_integer(kGroup, "window-height", window_height);
  kf.set_boolean(kGroup, "window-maximized", window_maximized);
  kf.set_string(kGroup, "zoom", zoom == 0 ? "fit-width" : std::to_string(zoom));
  kf.set_boolean(kGroup, "show-standard-toolbar", show_standard_toolbar);
  kf.set_boolean(kGroup, "show-format-toolbar", show_format_toolbar);
  kf.set_boolean(kGroup, "show-statusbar", show_statusbar);
  kf.set_boolean(kGroup, "toolbars-side-by-side", toolbars_side_by_side);
  kf.set_string(kGroup, "default-font", default_font);
  kf.set_integer(kGroup, "default-size", default_size);
  kf.set_integer(kGroup, "recent-count", recent_count);
  kf.set_string(kGroup, "last-dir", last_dir);
  kf.set_string_list(kGroup, "recent", recent);
  kf.set_string(kGroup, "units", units_text(units));
  kf.set_string(kGroup, "dictionary", dictionary_name_ok(dictionary) ? dictionary : "en");
  try {
    Glib::file_set_contents(path, kf.to_data());
  } catch (const Glib::Error&) {
  }
}

void clamp_window(int& width, int& height, int area_width, int area_height)
{
  if (area_width > 0)
    width = std::min(width, area_width);
  if (area_height > 0)
    height = std::min(height, area_height);
}

WindowMemory::WindowMemory(const Settings& settings)
    : width(settings.window_width),
      height(settings.window_height),
      maximized(settings.window_maximized)
{
}

void WindowMemory::update(int new_width, int new_height, bool maximized_now)
{
  maximized = maximized_now;
  // A maximised size is the screen's, not the window's own.
  if (maximized_now || new_width <= 0 || new_height <= 0)
    return;
  width = new_width;
  height = new_height;
}

void WindowMemory::store(Settings& settings) const
{
  settings.window_width = width;
  settings.window_height = height;
  settings.window_maximized = maximized;
}

std::vector<std::string> push_recent(std::vector<std::string> recent, const std::string& path,
                                     int count)
{
  recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
  recent.insert(recent.begin(), path);
  if (count >= 0 && static_cast<int>(recent.size()) > count)
    recent.resize(static_cast<size_t>(count));
  return recent;
}

}  // namespace writeit
