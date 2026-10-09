/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>

namespace writeit {

// View > Page / Draft. Page is the default, and it is the view that prints.
enum class ViewMode { Page, Draft };
constexpr ViewMode kDefaultView = ViewMode::Page;

// The screen page stands for A4, 21 × 29.7 cm, until Page Setup in M3: 540 px
// for its 11906 twips across at 100%, and the same scale down its 16838.
constexpr int kScreenPageWidth = 540;
constexpr int kScreenPageTwips = 11906;
constexpr int kA4Twips = 16838;

// Where the white area sits and how the text is inset in it, in pixels.
struct ViewGeometry {
  int page_width = 0;    // the white area across
  int page_height = 0;   // its least height, A4's (page_sheet_height); 0 lets the text decide
  int margin_left = 0;   // text inset on the left
  int margin_right = 0;  // text inset on the right
  int margin_y = 0;      // text inset, top and bottom
  int gap = 0;           // pasteboard showing above and below the page
  bool chrome = false;   // border, shadow, and the gray pasteboard
  bool centred = false;  // the page centred on the pasteboard
};

ViewGeometry view_geometry(ViewMode mode, double zoom);

// The Page view sheet's least height at a zoom: A4's 29.7 cm on the screen
// page's scale, the A4 proportion of the sheet's width (764 px at 100%).
// Text longer than a page makes the sheet taller. The page count's page is
// this sheet less its top and bottom insets, so the two always agree.
int page_sheet_height(double zoom);

// The status bar's "Page n of m", until M3 lays out real pages. A page holds
// the A4 sheet's height (page_sheet_height) less the page view's top and
// bottom insets: 692 px of laid-out text at 100%. The count is how many of
// those the text fills, and the page is the one the caret's line starts on.
// Text and page both scale with the zoom, so neither the zoom nor the view
// changes the count, short of rounding.

struct PageCount {
  int page = 1;
  int pages = 1;
};

// Pixels of laid-out text one page holds at a zoom.
int page_text_height(double zoom);

// From the text's laid-out height and the caret line's top, both in pixels
// at that zoom.
PageCount page_count(int text_height, int caret_y, double zoom);

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
