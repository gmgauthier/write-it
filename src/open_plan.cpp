/* SPDX-License-Identifier: Unlicense */

#include "open_plan.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace writeit {

namespace {

// The real path, or the path as written when it cannot be resolved.
std::string real_path(const std::string& path)
{
  char resolved[PATH_MAX];
  return realpath(path.c_str(), resolved) ? std::string(resolved) : path;
}

}  // namespace

bool same_file(const std::string& a, const std::string& b)
{
  if (a.empty() || b.empty())
    return false;
  if (a == b)
    return true;
  struct stat sa{};
  struct stat sb{};
  const bool stat_a = stat(a.c_str(), &sa) == 0;
  const bool stat_b = stat(b.c_str(), &sb) == 0;
  if (stat_a && stat_b)
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
  if (stat_a != stat_b)
    return false;
  return real_path(a) == real_path(b);
}

int window_holding(const std::vector<WindowState>& windows, const std::string& path)
{
  for (size_t i = 0; i < windows.size(); ++i) {
    if (same_file(windows[i].path, path) || same_file(windows[i].source, path))
      return static_cast<int>(i);
  }
  return -1;
}

std::vector<OpenAction> plan_open(const std::vector<OpenRequest>& requests,
                                  const std::vector<WindowState>& windows,
                                  const std::function<bool(const std::string&)>& exists)
{
  std::vector<OpenAction> actions;
  std::vector<bool> used(windows.size(), false);
  std::vector<std::string> seen;

  auto plan_file = [&](const std::string& path) {
    if (std::any_of(seen.begin(), seen.end(),
                    [&](const std::string& earlier) { return same_file(earlier, path); }))
      return;
    seen.push_back(path);
    const int open = window_holding(windows, path);
    if (open >= 0) {
      actions.push_back({OpenStep::Present, open, path});
      return;
    }
    for (size_t i = 0; i < windows.size(); ++i) {
      if (windows[i].pristine && !used[i]) {
        used[i] = true;
        actions.push_back({OpenStep::LoadInto, static_cast<int>(i), path});
        return;
      }
    }
    actions.push_back({OpenStep::LoadNew, -1, path});
  };

  std::vector<std::string> missing;
  std::vector<std::string> refusals;
  for (const OpenRequest& request : requests) {
    const std::string& path = request.path;
    if (path.empty())
      refusals.push_back(request.uri);
    else if (exists(path))
      plan_file(path);
    else
      missing.push_back(path);
  }
  for (const std::string& uri : refusals)
    actions.push_back({OpenStep::RefuseNotLocal, -1, {}, uri});
  for (const std::string& path : missing)
    plan_file(path);
  return actions;
}

std::vector<bool> close_after_open(int existing_windows, const std::vector<bool>& created_failed)
{
  const bool others =
      existing_windows > 0 ||
      std::find(created_failed.begin(), created_failed.end(), false) != created_failed.end();
  std::vector<bool> close = created_failed;
  if (!others) {
    // Every window here failed and there is no other: keep the first.
    const auto first = std::find(close.begin(), close.end(), true);
    if (first != close.end())
      *first = false;
  }
  return close;
}

namespace {

std::string name_of(const std::string& path)
{
  const auto slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string quoted(const std::string& text)
{
  return "\u201c" + text + "\u201d";
}

}  // namespace

std::string missing_message(const std::string& path)
{
  return "Could not find the file " + quoted(name_of(path)) + ".";
}

std::string unreadable_message(const std::string& path)
{
  return "Could not open the file " + quoted(name_of(path)) + ".";
}

std::string not_local_message(const std::string& uri)
{
  return "Write-It can only open files on this computer: " + uri;
}

}  // namespace writeit
