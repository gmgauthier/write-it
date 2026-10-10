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

ViewGeometry view_geometry_classic(ViewMode mode, double zoom)
{
  ViewGeometry page;
  page.page_width = std::max(1, scaled(kScreenPageWidth, zoom));
  page.page_height = std::max(1, twips_to_px(kA4Twips, zoom));
  page.margin_left = std::max(8, scaled(kPageInsetPx, zoom));
  page.margin_right = page.margin_left;
  page.margin_y = std::max(8, scaled(36, zoom));
  page.margin_bottom = page.margin_y;
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
  draft.margin_bottom = draft.margin_y;
  return draft;
}

// Paper and margins from the file, on the same twip scale. No floor of 8:
// that floor belongs to the default page the classic path draws.
ViewGeometry view_geometry_custom(ViewMode mode, double zoom, const PageSetup& raw)
{
  const PageSetup page = clamp_page(raw);
  ViewGeometry sheet;
  sheet.page_width = std::max(1, twips_to_px(page.paper_width, zoom));
  sheet.page_height = std::max(1, twips_to_px(page.paper_height, zoom));
  sheet.margin_left = std::max(0, twips_to_px(page.margin_left, zoom));
  sheet.margin_right = std::max(0, twips_to_px(page.margin_right, zoom));
  sheet.margin_y = std::max(0, twips_to_px(page.margin_top, zoom));
  sheet.margin_bottom = std::max(0, twips_to_px(page.margin_bottom, zoom));
  sheet.gap = 18;
  sheet.chrome = true;
  sheet.centred = true;
  if (mode == ViewMode::Page)
    return sheet;
  ViewGeometry draft;
  draft.page_width = sheet.page_width;
  draft.margin_left = std::max(4, scaled(8, zoom));
  const int text = std::max(1, sheet.page_width - sheet.margin_left - sheet.margin_right);
  draft.margin_right = std::max(0, draft.page_width - draft.margin_left - text);
  draft.margin_y = std::max(4, scaled(6, zoom));
  draft.margin_bottom = draft.margin_y;
  return draft;
}

}  // namespace

ViewGeometry view_geometry(ViewMode mode, double zoom, const PageSetup& page)
{
  // Columns do not change the sheet. The classic pixels, including the
  // margin floor of 8, stay while the paper is the default A4.
  if (page_metrics_default(page))
    return view_geometry_classic(mode, zoom);
  return view_geometry_custom(mode, zoom, page);
}

ViewGeometry view_geometry(ViewMode mode, double zoom)
{
  return view_geometry(mode, zoom, default_page());
}

int page_sheet_height(double zoom)
{
  return std::max(1, twips_to_px(kA4Twips, zoom));
}

int page_text_height(const PageSetup& page, double zoom)
{
  if (page_metrics_default(page))
    return page_text_height(zoom);
  const ViewGeometry sheet = view_geometry(ViewMode::Page, zoom, page);
  return std::max(1, sheet.page_height - sheet.margin_y - sheet.margin_bottom);
}

int page_text_height(double zoom)
{
  const ViewGeometry page = view_geometry(ViewMode::Page, zoom);
  return std::max(1, page.page_height - page.margin_y - page.margin_bottom);
}

PageCount page_count(int text_height, int caret_y, int per_page)
{
  const int per = std::max(1, per_page);
  PageCount count;
  count.pages = std::max(1, (std::max(0, text_height) + per - 1) / per);
  count.page = std::clamp(std::max(0, caret_y) / per + 1, 1, count.pages);
  return count;
}

PageCount page_count(int text_height, int caret_y, double zoom)
{
  return page_count(text_height, caret_y, page_text_height(zoom));
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

static_assert(default_page().paper_width == kScreenPageTwips, "default paper is the screen page");
static_assert(default_page().paper_height == kA4Twips, "default paper is A4");

}  // namespace writeit
