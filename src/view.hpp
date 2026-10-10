/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "document.hpp"

#include <string>

namespace writeit {

// View > Page / Draft. Page is the default, and it is the view that prints.
enum class ViewMode { Page, Draft };
constexpr ViewMode kDefaultView = ViewMode::Page;

// The screen page stands for the document's paper. The default is A4, 21 ×
// 29.7 cm: 540 px for its 11906 twips across at 100%, and the same scale
// down its 16838. Page Setup changes the paper; these constants stay the
// scale, so a default page is still 540 by 764.
constexpr int kScreenPageWidth = 540;
constexpr int kScreenPageTwips = 11906;
constexpr int kA4Twips = 16838;
// Page view's text inset either side at 100%, in screen pixels: the default
// page's left and right margins (926 twips). Page Setup replaces them.
constexpr int kPageInsetPx = 42;
// The width text has between those margins, in twips: A4's 11906 less the
// two insets on the screen page's scale, 456 of its 540 px, rounded. That is
// 10054 twips, 6.98" or 17.73 cm. Draft keeps it, and the zoom scales it
// with everything else. The Paragraph dialog refuses indents that leave no
// room in it.
constexpr int kTextWidthTwips =
    ((kScreenPageWidth - 2 * kPageInsetPx) * kScreenPageTwips + kScreenPageWidth / 2) /
    kScreenPageWidth;
static_assert(kTextWidthTwips == 10054, "A4 less the page view's insets");

// Where the white area sits and how the text is inset in it, in pixels.
struct ViewGeometry {
  int page_width = 0;     // the white area across
  int page_height = 0;    // its least height, A4's (page_sheet_height); 0 lets the text decide
  int margin_left = 0;    // text inset on the left
  int margin_right = 0;   // text inset on the right
  int margin_y = 0;       // text inset at the top
  int margin_bottom = 0;  // text inset at the bottom; the default page uses margin_y
  int gap = 0;            // pasteboard showing above and below the page
  bool chrome = false;    // border, shadow, and the gray pasteboard
  bool centred = false;   // the page centred on the pasteboard
};

// The default page. A document's own paper is the overload below.
ViewGeometry view_geometry(ViewMode mode, double zoom);
ViewGeometry view_geometry(ViewMode mode, double zoom, const PageSetup& page);

// The Page view sheet's least height at a zoom: A4's 29.7 cm on the screen
// page's scale (764 px at 100%). Text longer than a page makes the sheet
// taller. The page count's page is this sheet less its top and bottom
// insets, so the two always agree. Another paper uses view_geometry().
int page_sheet_height(double zoom);

// The status bar's "Page n of m". A page holds the sheet's height less its
// top and bottom margins: 692 px of laid-out text at 100% on the default
// A4 page. The count is how many of those the text fills, and the page is
// the one the caret's line starts on. A page break is kept and written; it
// does not change the count. Text and page both scale with the zoom, so
// neither the zoom nor the view changes the count, short of rounding.

struct PageCount {
  int page = 1;
  int pages = 1;
};

// Pixels of laid-out text one page holds at a zoom, for the default page.
int page_text_height(double zoom);
// The same for a document's page setup. The default page matches the zoom-only
// function, floor of 8 px included.
int page_text_height(const PageSetup& page, double zoom);

// From the text's laid-out height and the caret line's top, both in pixels
// at that zoom, on the default page.
PageCount page_count(int text_height, int caret_y, double zoom);
// `per_page` is page_text_height() for the document's paper.
PageCount page_count(int text_height, int caret_y, int per_page);

// "Page 2 of 4".
std::string page_label(const PageCount& count);

// View > Zoom and the status bar's zoom cell, in percent; 0 is Fit width.
constexpr int kZoomChoices[] = {50, 75, 100, 150, 200, 0};

// The two widths the window lays out at a zoom and view: the white area
// across, and the line width text wraps at inside it. Both follow the zoom
// in either direction and depend on nothing else, whatever came before.
struct PageWidths {
  int page = 0;
  int wrap = 0;
};

PageWidths page_widths(ViewMode mode, double zoom);

// Twips to screen pixels at a zoom, on the screen page's scale.
int twips_to_px(int twips, double zoom);

}  // namespace writeit
