/* SPDX-License-Identifier: Unlicense */

#include "font_sizes.hpp"

namespace writeit {

const std::vector<int>& preset_sizes()
{
  static const std::vector<int> kPresets = {8, 9, 10, 11, 12, 14, 16, 18, 24, 36};
  return kPresets;
}

bool preset_size(int size)
{
  (void)size;
  return false;
}

std::vector<int> size_choices(int actual)
{
  (void)actual;
  return preset_sizes();
}

int parse_size(const std::string& text)
{
  (void)text;
  return 0;
}

}  // namespace writeit
