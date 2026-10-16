/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "document.hpp"
#include "view.hpp"

#include <cairomm/context.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace writeit {

inline std::string rgb_hex(const Rgb& rgb)
{
  char buf[8];
  std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", std::max(0, std::min(255, rgb.red)),
                std::max(0, std::min(255, rgb.green)), std::max(0, std::min(255, rgb.blue)));
  return buf;
}

inline void paint_rgb(const Cairo::RefPtr<Cairo::Context>& cr, const Rgb& rgb)
{
  cr->set_source_rgb(std::max(0, std::min(255, rgb.red)) / 255.0,
                     std::max(0, std::min(255, rgb.green)) / 255.0,
                     std::max(0, std::min(255, rgb.blue)) / 255.0);
}

inline Rgb border_ink(Ink ink)
{
  return ink == Ink::Auto ? ink_rgb(Ink::Black) : ink_rgb(ink);
}

inline Rgb mix_rgb(const Rgb& from, const Rgb& to, double amount)
{
  const double t = std::max(0.0, std::min(1.0, amount));
  auto channel = [t](int a, int b) {
    return std::max(0, std::min(255, static_cast<int>(std::lround(a * (1.0 - t) + b * t))));
  };
  Rgb out;
  out.red = channel(from.red, to.red);
  out.green = channel(from.green, to.green);
  out.blue = channel(from.blue, to.blue);
  return out;
}

// `line_y` is the top get_line_yrange reports, which includes a page seam.
// The border is measured from the text. Space may sit in this sheet's top
// margin, and it stops at the top of the paper, so a tall seam cannot pull
// the box onto the sheet above.
inline int border_top_y(int line_y, int seam_px, int space_px, int margin_y)
{
  const int text_y = line_y + std::max(0, seam_px);
  const int asked = text_y - std::max(0, space_px);
  if (seam_px <= 0)
    return asked;
  const int sheet_top = text_y - std::max(0, margin_y);
  return std::max(asked, sheet_top);
}

inline double side_px(const BorderSide& side, double zoom)
{
  const BorderSide clamped = clamp_side(side);
  if (clamped.style == BorderStyle::None)
    return 0;
  if (clamped.style == BorderStyle::Hairline)
    return 1;
  return std::max(1.0, static_cast<double>(twips_to_px(clamped.width, zoom)));
}

enum class BorderEdge { Top, Left, Bottom, Right };

inline void stroke_segment(const Cairo::RefPtr<Cairo::Context>& cr, double x1, double y1, double x2,
                           double y2, double width, const Rgb& color, BorderStyle style)
{
  cr->save();
  cr->set_line_width(std::max(1.0, width));
  paint_rgb(cr, color);
  cr->set_line_cap(Cairo::LINE_CAP_BUTT);
  std::vector<double> dashes;
  const double w = std::max(1.0, width);
  if (style == BorderStyle::Dotted) {
    dashes = {w, w * 1.5};
    cr->set_line_cap(Cairo::LINE_CAP_ROUND);
  } else if (style == BorderStyle::Dashed) {
    dashes = {w * 4, w * 2};
  } else if (style == BorderStyle::DotDash) {
    dashes = {w, w * 1.5, w * 4, w * 1.5};
  } else if (style == BorderStyle::DotDotDash) {
    dashes = {w, w * 1.5, w, w * 1.5, w * 4, w * 1.5};
  }
  if (!dashes.empty())
    cr->set_dash(dashes, 0);
  cr->move_to(x1, y1);
  cr->line_to(x2, y2);
  cr->stroke();
  cr->restore();
}

inline Rgb bevel_color(BorderStyle style, BorderEdge edge, Ink ink)
{
  const Rgb base = border_ink(ink);
  const bool leading = edge == BorderEdge::Top || edge == BorderEdge::Left;
  if (style == BorderStyle::Inset)
    return leading ? mix_rgb(base, Rgb{0, 0, 0}, 0.45) : mix_rgb(base, Rgb{255, 255, 255}, 0.55);
  if (style == BorderStyle::Outset)
    return leading ? mix_rgb(base, Rgb{255, 255, 255}, 0.55) : mix_rgb(base, Rgb{0, 0, 0}, 0.45);
  return base;
}

// left, top, right, and bottom are the border's centre lines.
inline void paint_borders(const Cairo::RefPtr<Cairo::Context>& cr, double left, double top,
                          double right, double bottom, const Borders& borders, double zoom)
{
  if (right <= left || bottom <= top)
    return;
  const Borders box = clamp_borders(borders);
  const BorderSide sides[4] = {box.top, box.left, box.bottom, box.right};
  const BorderEdge edges[4] = {BorderEdge::Top, BorderEdge::Left, BorderEdge::Bottom,
                               BorderEdge::Right};
  for (int i = 0; i < 4; ++i) {
    const BorderSide side = sides[i];
    if (side.style == BorderStyle::None)
      continue;
    const double w = side_px(side, zoom);
    if (w <= 0)
      continue;
    double x1 = left;
    double y1 = top;
    double x2 = right;
    double y2 = top;
    if (i == 1) {
      x2 = left;
      y2 = bottom;
    } else if (i == 2) {
      y1 = bottom;
      x2 = right;
      y2 = bottom;
    } else if (i == 3) {
      x1 = right;
      y1 = top;
      x2 = right;
      y2 = bottom;
    }
    const bool horizontal = i == 0 || i == 2;
    auto shift = [horizontal](double delta, double& ax, double& ay, double& bx, double& by) {
      if (horizontal) {
        ay += delta;
        by += delta;
      } else {
        ax += delta;
        bx += delta;
      }
    };
    if (side.shadow && (i == 2 || i == 3)) {
      double sx1 = x1;
      double sy1 = y1;
      double sx2 = x2;
      double sy2 = y2;
      shift(w, sx1, sy1, sx2, sy2);
      stroke_segment(cr, sx1, sy1, sx2, sy2, w, Rgb{128, 128, 128}, BorderStyle::Single);
    }
    const BorderStyle look = side.style;
    if (look == BorderStyle::Double || look == BorderStyle::Triple) {
      const int lines = look == BorderStyle::Triple ? 3 : 2;
      const double stroke = std::max(1.0, w / (lines == 3 ? 5.0 : 3.0));
      const double step = w / lines;
      for (int line = 0; line < lines; ++line) {
        const double delta = (static_cast<double>(line) - (lines - 1) / 2.0) * step;
        double ax = x1;
        double ay = y1;
        double bx = x2;
        double by = y2;
        shift(delta, ax, ay, bx, by);
        stroke_segment(cr, ax, ay, bx, by, stroke, border_ink(side.color), BorderStyle::Single);
      }
      continue;
    }
    if (look == BorderStyle::Emboss || look == BorderStyle::Engrave) {
      const bool engrave = look == BorderStyle::Engrave;
      const Rgb base = border_ink(side.color);
      const Rgb light = mix_rgb(base, Rgb{255, 255, 255}, 0.55);
      const Rgb dark = mix_rgb(base, Rgb{0, 0, 0}, 0.45);
      const bool outer_light = engrave ? (i == 2 || i == 3) : (i == 0 || i == 1);
      const double half = std::max(1.0, w / 2.0);
      const double outward = (i == 0 || i == 1) ? -half / 2.0 : half / 2.0;
      double ox1 = x1;
      double oy1 = y1;
      double ox2 = x2;
      double oy2 = y2;
      double ix1 = x1;
      double iy1 = y1;
      double ix2 = x2;
      double iy2 = y2;
      shift(outward, ox1, oy1, ox2, oy2);
      shift(-outward, ix1, iy1, ix2, iy2);
      stroke_segment(cr, ox1, oy1, ox2, oy2, half, outer_light ? light : dark, BorderStyle::Single);
      stroke_segment(cr, ix1, iy1, ix2, iy2, half, outer_light ? dark : light, BorderStyle::Single);
      continue;
    }
    const bool dashed = look == BorderStyle::Dotted || look == BorderStyle::Dashed ||
                        look == BorderStyle::DotDash || look == BorderStyle::DotDotDash;
    const Rgb color = (look == BorderStyle::Inset || look == BorderStyle::Outset)
                          ? bevel_color(look, edges[i], side.color)
                          : border_ink(side.color);
    stroke_segment(cr, x1, y1, x2, y2, w, color, dashed ? look : BorderStyle::Single);
  }
}

}  // namespace writeit
