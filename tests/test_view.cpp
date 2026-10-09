/* SPDX-License-Identifier: Unlicense */

// View > Page / Draft. Page is the white sheet on the gray pasteboard and is
// the view that prints. Draft drops the sheet, its shadow, the pasteboard and
// the page height, and shows continuous text. Lines wrap at the same width in
// both, as in Word 97's Normal view, so the line breaks are the printed ones.

#include "check.hpp"
#include "view.hpp"

namespace {

using writeit::ViewMode;
using writeit::view_geometry;

const double kZooms[] = {0.5, 0.75, 1.0, 1.5, 2.0, 1.7333, 0.2222, 3.1};

int text_width(const writeit::ViewGeometry& g)
{
  return g.page_width - 2 * g.margin_x;
}

void page()
{
  // Page keeps the geometry M0 and M1 drew.
  const auto g = view_geometry(ViewMode::Page, 1.0);
  CHECK(g.page_width == 540);
  CHECK(g.page_height == 470);
  CHECK(g.margin_x == 42);
  CHECK(g.margin_y == 36);
  CHECK(g.gap == 18);
  CHECK(g.chrome);
  CHECK(g.centred);
  const auto half = view_geometry(ViewMode::Page, 0.5);
  CHECK(half.page_width == 270);
  CHECK(half.page_height == 235);
  CHECK(half.margin_x == 21);
  CHECK(half.margin_y == 18);
  const auto tiny = view_geometry(ViewMode::Page, 0.1);
  CHECK(tiny.margin_x == 8);
  CHECK(tiny.margin_y == 8);
  CHECK(view_geometry(ViewMode::Page, 2.0).page_width == 1080);
}

void draft()
{
  const auto g = view_geometry(ViewMode::Draft, 1.0);
  CHECK(!g.chrome);
  CHECK(!g.centred);
  CHECK(g.gap == 0);
  // No page height: the text runs on, as long as it is.
  CHECK(g.page_height == 0);
  CHECK(g.margin_x < view_geometry(ViewMode::Page, 1.0).margin_x);
  CHECK(g.margin_y < view_geometry(ViewMode::Page, 1.0).margin_y);
  CHECK(g.margin_x > 0);
  CHECK(g.margin_y > 0);
  for (double z : kZooms) {
    const auto p = view_geometry(ViewMode::Page, z);
    const auto d = view_geometry(ViewMode::Draft, z);
    // The same wrap width at every zoom, so Draft breaks lines where Page does.
    CHECK(text_width(d) == text_width(p));
    CHECK(d.page_width < p.page_width);
    CHECK(d.page_width >= 1);
  }
}

void indents_scale()
{
  // Indents are drawn from twips at the same scale in both views, so a
  // half-inch indent is as wide in Draft as on the page.
  CHECK(writeit::twips_to_px(0, 1.0) == 0);
  CHECK(writeit::twips_to_px(11906, 1.0) == 540);
  CHECK(writeit::twips_to_px(1440, 1.0) == 65);
  CHECK(writeit::twips_to_px(-1440, 1.0) == -65);
  CHECK(writeit::twips_to_px(720, 2.0) == 65);
  CHECK(writeit::twips_to_px(1440, 0.5) == 33);
}

void mode_names()
{
  // The default, and the one View starts on every launch.
  CHECK(writeit::kDefaultView == ViewMode::Page);
}

}  // namespace

int main()
{
  page();
  draft();
  indents_scale();
  mode_names();
  return suite_test::done("view");
}
