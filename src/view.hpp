/* SPDX-License-Identifier: Unlicense */

#pragma once

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
