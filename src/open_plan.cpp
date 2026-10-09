/* SPDX-License-Identifier: Unlicense */

#include "open_plan.hpp"

namespace writeit {

std::vector<OpenAction> plan_open(const std::vector<std::string>& paths,
                                  const std::vector<WindowState>&)
{
  std::vector<OpenAction> actions;
  if (!paths.empty())
    actions.push_back({OpenStep::LoadNew, -1, paths.back()});
  return actions;
}

std::vector<bool> close_after_open(int, const std::vector<bool>& created_failed)
{
  return std::vector<bool>(created_failed.size(), false);
}

}  // namespace writeit
