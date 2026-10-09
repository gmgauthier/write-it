/* SPDX-License-Identifier: Unlicense */

#include "narrow_combo.hpp"

#include <gtkmm/cellrenderertext.h>
#include <gtkmm/entry.h>

#include <algorithm>

namespace writeit {

NarrowCombo::NarrowCombo(int width, bool has_entry)
    : Gtk::ComboBoxText(has_entry),
      width_(width)
{
  if (Gtk::Entry* entry = has_entry ? get_entry() : nullptr)
    entry->set_width_chars(4);
  set_popup_fixed_width(false);
  for (auto* cell : get_cells()) {
    if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cell))
      text->property_ellipsize() = Pango::ELLIPSIZE_END;
  }
}

void NarrowCombo::get_preferred_width_vfunc(int& minimum_width, int& natural_width) const
{
  Gtk::ComboBoxText::get_preferred_width_vfunc(minimum_width, natural_width);
  minimum_width = std::min(minimum_width, width_);
  natural_width = std::max(minimum_width, width_);
}

}  // namespace writeit
