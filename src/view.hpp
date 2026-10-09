/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>

namespace writeit {

// View > Page / Draft. Page is the default, and it is the view that prints.
enum class ViewMode { Page, Draft };
constexpr ViewMode kDefaultView = ViewMode::Page;

// The screen page stands for A4, 21 cm across, until Page Setup in M3.
constexpr int kScreenPageWidth = 540;
constexpr int kScreenPageHeight = 470;
constexpr int kScreenPageTwips = 11906;

// Where the white area sits and how the text is inset in it, in pixels.
struct ViewGeometry {
  int page_width = 0;    // the white area across
  int page_height = 0;   // its least height; 0 lets the text decide
  int margin_left = 0;   // text inset on the left
  int margin_right = 0;  // text inset on the right
  int margin_y = 0;      // text inset, top and bottom
  int gap = 0;           // pasteboard showing above and below the page
  bool chrome = false;   // border, shadow, and the gray pasteboard
  bool centred = false;  // the page centred on the pasteboard
};

ViewGeometry view_geometry(ViewMode mode, double zoom);

// The status bar's "Page n of m", until M3 lays out real pages. A page holds
// the A4 sheet's height (16838 twips) on the screen page's scale, less the
// page view's top and bottom insets: 692 px of laid-out text at 100%. The
// count is how many of those the text fills, and the page is the one the
// caret's line starts on. Text and page both scale with the zoom, so neither
// the zoom nor the view changes the count, short of rounding.
constexpr int kA4Twips = 16838;

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

// Twips to screen pixels at a zoom, on the screen page's scale.
int twips_to_px(int twips, double zoom);

}  // namespace writeit
