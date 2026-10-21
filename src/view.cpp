/* SPDX-License-Identifier: Unlicense */

#include "view.hpp"

#include <algorithm>
#include <cmath>
#include <string>

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

FootnoteRoom footnote_room(int page_h, int note_h)
{
  FootnoteRoom fit;
  const int per = page_h > 0 ? page_h : 1;
  fit.cut = note_h > 0 ? note_h : 0;
  fit.room = std::max(1, per - fit.cut);
  return fit;
}

bool line_moves_for_note(int text_h, int room, int note_h, int page_h)
{
  if (page_h > 0 && note_h >= page_h)
    return true;
  return text_h <= room;
}

int clamp_note_height(int note_h, int page_text_h)
{
  return std::min(note_h, page_text_h);
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

std::vector<size_t> story_selection(const std::vector<int>& lengths, const std::vector<int>& before,
                                    int from, int to)
{
  std::vector<size_t> out;
  if (lengths.empty())
    return out;
  if (from < 0)
    from = 0;
  if (to < 0)
    to = 0;
  if (to < from)
    std::swap(to, from);
  const int last = to > from ? to - 1 : to;
  int next = 0;
  int offset = 0;
  for (size_t i = 0; i < lengths.size(); ++i) {
    const int gap = i < before.size() ? before[i] : 0;
    offset += gap;
    const int owns_last = offset + std::max(0, lengths[i]);
    if (last >= next && from <= owns_last)
      out.push_back(i);
    next = owns_last + 1;
    offset = owns_last;
  }
  if (out.empty())
    out.push_back(lengths.size() - 1);
  return out;
}

int tab_align_x(TabAlign align, int tab_right, int text_left, int text_right, int decimal_x)
{
  switch (align) {
    case TabAlign::Right:
      return text_right;
    case TabAlign::Center:
      return text_left + (text_right - text_left) / 2;
    case TabAlign::Decimal:
      return decimal_x;
    case TabAlign::Left:
    case TabAlign::Bar:
      break;
  }
  return tab_right;
}

int spacing_line_extra_px(LineRule rule, int value, int box_px, double zoom)
{
  const int box = std::max(1, box_px);
  const double factor = zoom > 0 ? zoom : 1.0;
  switch (rule) {
    case LineRule::OneHalf:
      return std::max(0, (box + 1) / 2);
    case LineRule::Double:
      return box;
    case LineRule::AtLeast:
    case LineRule::Exactly: {
      const int want = static_cast<int>(std::lround(value * 96.0 / 1440.0 * factor));
      return std::max(0, want - box);
    }
    case LineRule::Multiple:
      if (value > 240)
        return static_cast<int>(std::lround((value - 240) / 240.0 * box));
      return 0;
    case LineRule::Single:
      break;
  }
  return 0;
}

int column_display_px(int twips, double zoom)
{
  const int stored = twips > 0 ? twips : kMinCellTwips;
  const double factor = zoom > 0 ? zoom : 1.0;
  return std::max(1, twips_to_px(std::max(kMinCellTwips, stored), factor));
}

PictureSpan picture_display_px(int src_w, int src_h, double zoom, int limit)
{
  if (src_w <= 0 || src_h <= 0 || !(zoom > 0))
    return {1, 1};
  int width = src_w;
  int height = src_h;
  if (width > kPictureCapPx) {
    height = std::max(1, height * kPictureCapPx / width);
    width = kPictureCapPx;
  }
  // Past 100% the body keeps growing. The picture stays at its 100% size:
  // the full factor drew it larger than the text around it.
  const double factor = std::min(zoom, 1.0);
  PictureSpan span;
  span.width = std::max(1, static_cast<int>(std::lround(static_cast<double>(width) * factor)));
  span.height = std::max(1, static_cast<int>(std::lround(static_cast<double>(height) * factor)));
  // The cap is wider than the text column, and a cell is narrower still.
  // A picture does not wrap, so this keeps it inside that width.
  if (limit > 0 && span.width > limit) {
    const double scale = static_cast<double>(limit) / static_cast<double>(span.width);
    span.height =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(span.height) * scale)));
    span.width = limit;
  }
  return span;
}

static_assert(default_page().paper_width == kScreenPageTwips, "default paper is the screen page");
static_assert(default_page().paper_height == kA4Twips, "default paper is A4");

bool is_page_seam(const std::string& name)
{
  const std::string prefix = std::string("page-seam") + '\x1f';
  return name.compare(0, prefix.size(), prefix) == 0;
}

bool is_draft_gap(const std::string& name)
{
  return name == "draft-gap";
}

bool is_spacing_gap(const std::string& name)
{
  const std::string prefix = std::string("gap") + '\x1f';
  return name.compare(0, prefix.size(), prefix) == 0;
}

int page_seam_pixels(const std::string& name)
{
  const std::string prefix = std::string("page-seam") + '\x1f';
  if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
    return 0;
  int pixels = 0;
  for (size_t i = prefix.size(); i < name.size(); ++i) {
    if (name[i] < '0' || name[i] > '9')
      return 0;
    pixels = pixels * 10 + (name[i] - '0');
  }
  return pixels;
}

}  // namespace writeit
