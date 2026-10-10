/* SPDX-License-Identifier: Unlicense */

// The status bar's "Page n of m". Until M3 lays out real pages, a page is
// the A4 sheet's height on the screen page's scale, less the page view's top
// and bottom insets, and the count is how many of those the laid-out text
// fills. Both the text and the page scale with the zoom, so the count does
// not depend on it.

#include "check.hpp"
#include "view.hpp"

#include <cmath>

namespace {

using writeit::page_count;
using writeit::page_label;
using writeit::page_text_height;

void heights()
{
  // A4 is 16838 twips tall: 764 px at 100% on the 540 px screen page,
  // less the 36 px inset at the top and the bottom.
  CHECK(page_text_height(1.0) == 692);
  CHECK(page_text_height(0.5) == 382 - 36);
  CHECK(page_text_height(2.0) == 1527 - 144);
  CHECK(page_text_height(0.75) > page_text_height(0.5));
  CHECK(page_text_height(0.01) >= 1);
}

void sheets()
{
  // The Page view's sheet is A4 at every zoom: its least height is the A4
  // proportion of its width, and a page of the count is that sheet less the
  // page's top and bottom insets, so the sheet and the count agree.
  for (double z : {0.5, 0.75, 1.0, 2.0, 1.7333}) {
    const auto g = writeit::view_geometry(writeit::ViewMode::Page, z);
    CHECK(std::abs(g.page_height - g.page_width * 29.7 / 21.0) <= 1.5);
    CHECK(page_text_height(z) == g.page_height - 2 * g.margin_y);
  }
}

void counts()
{
  // An empty or a short document is one page, with the caret on it.
  CHECK(page_count(0, 0, 1.0).page == 1);
  CHECK(page_count(0, 0, 1.0).pages == 1);
  CHECK(page_count(20, 3, 1.0).pages == 1);
  // A page holds exactly its text height; one pixel more starts another.
  CHECK(page_count(692, 0, 1.0).pages == 1);
  CHECK(page_count(693, 0, 1.0).pages == 2);
  CHECK(page_count(692 * 4, 0, 1.0).pages == 4);
  // The caret's page.
  CHECK(page_count(692 * 4, 691, 1.0).page == 1);
  CHECK(page_count(692 * 4, 692, 1.0).page == 2);
  CHECK(page_count(692 * 4, 692 * 4 - 1, 1.0).page == 4);
  // Never past the last page, never before the first.
  CHECK(page_count(692 * 4, 692 * 9, 1.0).page == 4);
  CHECK(page_count(692 * 4, -5, 1.0).page == 1);
  CHECK(page_count(-5, 0, 1.0).pages == 1);
}

void zoom_free()
{
  // The same text at any zoom is the same count: 2.5 pages, caret on page 2.
  for (double z : {0.5, 0.75, 1.0, 1.5, 2.0}) {
    const int per = page_text_height(z);
    const auto c = page_count(per * 5 / 2, per * 3 / 2, z);
    CHECK(c.pages == 3);
    CHECK(c.page == 2);
  }
}

void labels()
{
  CHECK(page_label({1, 1}) == "Page 1 of 1");
  CHECK(page_label({2, 4}) == "Page 2 of 4");
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 39;

int main()
{
  heights();
  sheets();
  counts();
  zoom_free();
  labels();
  return suite_test::done("page-count", kChecks);
}
