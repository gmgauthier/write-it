/* SPDX-License-Identifier: Unlicense */

#include "view.hpp"

#include <algorithm>
#include <cmath>

namespace writeit {
namespace {

int scaled(double value, double zoom)
{
  return static_cast<int>(std::lround(value * zoom));
}

}  // namespace

ViewGeometry view_geometry(ViewMode mode, double zoom)
{
  ViewGeometry page;
  page.page_width = std::max(1, scaled(kScreenPageWidth, zoom));
  page.page_height = std::max(1, scaled(kScreenPageHeight, zoom));
  page.margin_left = std::max(8, scaled(42, zoom));
  page.margin_right = page.margin_left;
  page.margin_y = std::max(8, scaled(36, zoom));
  page.gap = 18;
  page.chrome = true;
  page.centred = true;
  if (mode == ViewMode::Page)
    return page;
  // Draft keeps Page's text width, so lines break where they will print.
  // The left margin becomes a small gutter; the right takes the difference.
  // The white area keeps Page's width, because a GtkTextView will not shrink
  // below the lines it has already laid out.
  ViewGeometry draft;
  draft.page_width = page.page_width;
  draft.margin_left = std::max(4, scaled(8, zoom));
  draft.margin_right = page.margin_left + page.margin_right - draft.margin_left;
  draft.margin_y = std::max(4, scaled(6, zoom));
  return draft;
}

int twips_to_px(int twips, double zoom)
{
  const double px = static_cast<double>(twips) * kScreenPageWidth * zoom / kScreenPageTwips;
  return static_cast<int>(std::lround(px));
}

}  // namespace writeit
