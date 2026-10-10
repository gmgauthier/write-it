/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>
#include <vector>

namespace writeit {

// RTF starters shipped with the program, source tree first, then the
// installed data directory. Sorted by file name.
std::vector<std::string> shipped_templates();

}  // namespace writeit
