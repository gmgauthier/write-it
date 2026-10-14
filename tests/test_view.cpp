/* SPDX-License-Identifier: Unlicense */

// View > Page / Draft. Page is the white sheet on the gray pasteboard and is
// the view that prints. Draft drops the sheet, its shadow, the pasteboard and
// the page height, and shows continuous text. Lines wrap at the same width in
// both, as in Word 97's Normal view, so the line breaks are the printed ones.

#include "check.hpp"
#include "view.hpp"

#include <vector>

namespace {

using writeit::view_geometry;
using writeit::ViewMode;

const double kZooms[] = {0.5, 0.75, 1.0, 1.5, 2.0, 1.7333, 0.2222, 3.1};

int text_width(const writeit::ViewGeometry& g)
{
  return g.page_width - g.margin_left - g.margin_right;
}

void page()
{
  // Page keeps the width M0 and M1 drew. Its least height is A4's, 29.7 cm
  // to the 540 px of 21 cm (16838 twips to 11906).
  const auto g = view_geometry(ViewMode::Page, 1.0);
  CHECK(g.page_width == 540);
  CHECK(g.page_height == 764);
  CHECK(g.margin_left == 42);
  CHECK(g.margin_right == 42);
  CHECK(g.margin_y == 36);
  CHECK(g.gap == 18);
  CHECK(g.chrome);
  CHECK(g.centred);
  const auto half = view_geometry(ViewMode::Page, 0.5);
  CHECK(half.page_width == 270);
  CHECK(half.page_height == 382);
  CHECK(half.margin_left == 21);
  CHECK(half.margin_right == 21);
  CHECK(half.margin_y == 18);
  const auto tiny = view_geometry(ViewMode::Page, 0.1);
  CHECK(tiny.margin_left == 8);
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
  // A small gutter on the left in place of the page margin.
  CHECK(g.margin_left < view_geometry(ViewMode::Page, 1.0).margin_left);
  CHECK(g.margin_y < view_geometry(ViewMode::Page, 1.0).margin_y);
  CHECK(g.margin_left > 0);
  CHECK(g.margin_y > 0);
  for (double z : kZooms) {
    const auto p = view_geometry(ViewMode::Page, z);
    const auto d = view_geometry(ViewMode::Draft, z);
    // The same wrap width at every zoom, so Draft breaks lines where Page does.
    CHECK(text_width(d) == text_width(p));
    // The white area keeps Page's width: a GtkTextView will not shrink below
    // the lines it has laid out, so Draft keeps the width and moves the text
    // left instead.
    CHECK(d.page_width == p.page_width);
    CHECK(d.margin_left < p.margin_left);
    CHECK(d.margin_right > p.margin_right);
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

// The widths the window lays out at each zoom, worked by hand from the M0
// page: 540 px across at 100% with a 42 px margin either side.
struct Expected {
  int percent;
  int page;
  int wrap;
};
const Expected kExpected[] = {
    {50, 270, 228}, {75, 405, 341}, {100, 540, 456}, {150, 810, 684}, {200, 1080, 912},
};

void page_widths()
{
  // Both views, every menu zoom: the page and the wrap scale with the zoom,
  // down as well as up, and Draft wraps where Page does.
  for (const auto& e : kExpected) {
    const double z = e.percent / 100.0;
    for (ViewMode mode : {ViewMode::Page, ViewMode::Draft}) {
      const auto w = writeit::page_widths(mode, z);
      CHECK(w.page == e.page);
      CHECK(w.wrap == e.wrap);
      // The wrap is what the margins leave of the page.
      const auto g = view_geometry(mode, z);
      CHECK(w.page == g.page_width);
      CHECK(w.wrap == text_width(g));
    }
  }
  // Half the zoom is half the wrap, give or take the rounding of a margin.
  const auto full = writeit::page_widths(ViewMode::Draft, 1.0);
  const auto half = writeit::page_widths(ViewMode::Draft, 0.5);
  CHECK(half.wrap * 2 >= full.wrap - 2 && half.wrap * 2 <= full.wrap + 2);
  CHECK(half.wrap < full.wrap);
  CHECK(writeit::page_widths(ViewMode::Draft, 2.0).wrap > full.wrap);
}

void transitions()
{
  // The widths depend on the zoom and the view now, never on the way there.
  // Every ordered pair of states, then the sequence SysAdmin found
  // (Draft 200, Page 100, Draft 50) and its reverse.
  struct State {
    ViewMode mode;
    int percent;
  };
  std::vector<State> states;
  for (const auto& e : kExpected)
    for (ViewMode mode : {ViewMode::Page, ViewMode::Draft})
      states.push_back({mode, e.percent});
  auto expected = [](int percent) {
    for (const auto& e : kExpected)
      if (e.percent == percent)
        return e;
    return Expected{};
  };
  for (const auto& from : states) {
    for (const auto& to : states) {
      (void)writeit::page_widths(from.mode, from.percent / 100.0);
      const auto w = writeit::page_widths(to.mode, to.percent / 100.0);
      CHECK(w.page == expected(to.percent).page);
      CHECK(w.wrap == expected(to.percent).wrap);
    }
  }
  const State sequence[] = {{ViewMode::Draft, 200}, {ViewMode::Page, 100},  {ViewMode::Draft, 50},
                            {ViewMode::Page, 50},   {ViewMode::Draft, 200}, {ViewMode::Page, 100}};
  for (const auto& s : sequence) {
    const auto w = writeit::page_widths(s.mode, s.percent / 100.0);
    CHECK(w.page == expected(s.percent).page);
    CHECK(w.wrap == expected(s.percent).wrap);
  }
}

void mode_names()
{
  // The default, and the one View starts on every launch.
  CHECK(writeit::kDefaultView == ViewMode::Page);
}

void picture_span()
{
  // At 100% a picture no wider than 480 keeps its pixels. Wider ones cap
  // there, then the zoom scales that size. The cap's division truncates.
  const auto native = writeit::picture_display_px(200, 100, 1);
  CHECK(native.width == 200);
  CHECK(native.height == 100);
  const auto three_quarters = writeit::picture_display_px(200, 100, 0.75);
  CHECK(three_quarters.width == 150);
  CHECK(three_quarters.height == 75);
  const auto half = writeit::picture_display_px(200, 100, 0.5);
  CHECK(half.width == 100);
  CHECK(half.height == 50);
  const auto twice = writeit::picture_display_px(200, 100, 2);
  CHECK(twice.width == 400);
  CHECK(twice.height == 200);

  const auto capped = writeit::picture_display_px(800, 400, 1);
  CHECK(capped.width == 480);
  CHECK(capped.height == 240);
  const auto capped_75 = writeit::picture_display_px(800, 400, 0.75);
  CHECK(capped_75.width == 360);
  CHECK(capped_75.height == 180);
  const auto capped_50 = writeit::picture_display_px(640, 320, 0.5);
  CHECK(capped_50.width == 240);
  CHECK(capped_50.height == 120);
  const auto exact = writeit::picture_display_px(480, 240, 1);
  CHECK(exact.width == 480);
  CHECK(exact.height == 240);

  const auto stand_in = writeit::picture_display_px(48, 24, 0.5);
  CHECK(stand_in.width == 24);
  CHECK(stand_in.height == 12);
  const auto tiny = writeit::picture_display_px(1, 1, 0.5);
  CHECK(tiny.width == 1);
  CHECK(tiny.height == 1);

  const auto bad_size = writeit::picture_display_px(0, 10, 1);
  CHECK(bad_size.width == 1);
  CHECK(bad_size.height == 1);
  const auto bad_zoom = writeit::picture_display_px(10, 10, 0);
  CHECK(bad_zoom.width == 1);
  CHECK(bad_zoom.height == 1);
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 342;

int main()
{
  page();
  draft();
  indents_scale();
  page_widths();
  transitions();
  mode_names();
  picture_span();
  return suite_test::done("view", kChecks);
}
