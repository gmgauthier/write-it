/* SPDX-License-Identifier: Unlicense */

#include "open_plan.hpp"

#include <algorithm>

namespace writeit {

std::vector<OpenAction> plan_open(const std::vector<OpenRequest>& requests,
                                  const std::vector<WindowState>& windows,
                                  const std::function<bool(const std::string&)>& exists)
{
  std::vector<OpenAction> actions;
  std::vector<bool> used(windows.size(), false);
  std::vector<std::string> seen;

  auto plan_file = [&](const std::string& path) {
    if (std::find(seen.begin(), seen.end(), path) != seen.end())
      return;
    seen.push_back(path);
    const auto open = std::find_if(windows.begin(), windows.end(),
                                   [&](const WindowState& w) { return w.path == path; });
    if (open != windows.end()) {
      actions.push_back({OpenStep::Present, static_cast<int>(open - windows.begin()), path});
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

std::string missing_message(const std::string&)
{
  return "That file is missing.";
}

std::string unreadable_message(const std::string&)
{
  return "That file could not be opened.";
}

std::string not_local_message(const std::string&)
{
  return "Write-It can only open files on this computer.";
}

}  // namespace writeit
