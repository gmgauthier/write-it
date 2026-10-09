/* SPDX-License-Identifier: Unlicense */

#include "view.hpp"

namespace writeit {

ViewGeometry view_geometry(ViewMode mode, double zoom)
{
  (void)mode;
  (void)zoom;
  return ViewGeometry{};
}

int twips_to_px(int twips, double zoom)
{
  (void)twips;
  (void)zoom;
  return 0;
}

}  // namespace writeit
