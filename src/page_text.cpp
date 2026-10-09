/* SPDX-License-Identifier: Unlicense */

#include "page_text.hpp"

namespace writeit {

void PageText::set_layout_width(int width)
{
  if (width == width_)
    return;
  width_ = width;
  queue_resize();
}

void PageText::get_preferred_width_vfunc(int& minimum_width, int& natural_width) const
{
  if (width_ <= 0) {
    Gtk::TextView::get_preferred_width_vfunc(minimum_width, natural_width);
    return;
  }
  minimum_width = width_;
  natural_width = width_;
}

}  // namespace writeit
