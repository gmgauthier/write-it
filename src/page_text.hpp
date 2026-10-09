/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <gtkmm/textview.h>

namespace writeit {

// The document's text view. A GtkTextView asks for the width of the widest
// line it last laid out, and a centred or right-aligned line is as wide as
// the wrap, so once laid out at 200% it would never be allocated less, and
// it would never wrap narrower again. This one asks for the width the window
// gives it from the zoom and the view (page_widths), no more and no less, so
// the allocation, and with it the wrap, follow the zoom down as well as up.
class PageText : public Gtk::TextView {
 public:
  // Width in pixels, margins included; 0 falls back to GtkTextView's own.
  void set_layout_width(int width);
  int layout_width() const
  {
    return width_;
  }

 protected:
  void get_preferred_width_vfunc(int& minimum_width, int& natural_width) const override;

 private:
  int width_ = 0;
};

}  // namespace writeit
