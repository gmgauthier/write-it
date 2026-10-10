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
  page.page_height = page_sheet_height(zoom);
  page.margin_left = std::max(8, scaled(kPageInsetPx, zoom));
  page.margin_right = page.margin_left;
  page.margin_y = std::max(8, scaled(36, zoom));
  page.gap = 18;
  page.chrome = true;
  page.centred = true;
  if (mode == ViewMode::Page)
    return page;
  // Draft keeps Page's text width, so lines break where they will print.
  // The left margin becomes a small gutter; the right takes the difference.
  // The white area keeps Page's width; the text moves left into it.
  ViewGeometry draft;
  draft.page_width = page.page_width;
  draft.margin_left = std::max(4, scaled(8, zoom));
  draft.margin_right = page.margin_left + page.margin_right - draft.margin_left;
  draft.margin_y = std::max(4, scaled(6, zoom));
  return draft;
}

int page_sheet_height(double zoom)
{
  return std::max(1, twips_to_px(kA4Twips, zoom));
}

int page_text_height(double zoom)
{
  const ViewGeometry page = view_geometry(ViewMode::Page, zoom);
  return std::max(1, page.page_height - 2 * page.margin_y);
}

PageCount page_count(int text_height, int caret_y, double zoom)
{
  const int per = page_text_height(zoom);
  PageCount count;
  count.pages = std::max(1, (std::max(0, text_height) + per - 1) / per);
  count.page = std::clamp(std::max(0, caret_y) / per + 1, 1, count.pages);
  return count;
}

std::string page_label(const PageCount& count)
{
  return "Page " + std::to_string(count.page) + " of " + std::to_string(count.pages);
}

PageWidths page_widths(ViewMode mode, double zoom)
{
  const ViewGeometry g = view_geometry(mode, zoom);
  PageWidths widths;
  widths.page = g.page_width;
  widths.wrap = std::max(1, g.page_width - g.margin_left - g.margin_right);
  return widths;
}

int twips_to_px(int twips, double zoom)
{
  const double px = static_cast<double>(twips) * kScreenPageWidth * zoom / kScreenPageTwips;
  return static_cast<int>(std::lround(px));
}

}  // namespace writeit
