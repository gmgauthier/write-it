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
  if (Gtk::Entry* entry = has_entry ? get_entry() : nullptr) {
    // Room for Word's widest sizes, "1638" and "10.5", and no more.
    entry->set_width_chars(4);
    entry->set_max_width_chars(4);
    get_style_context()->add_class("narrow-entry");
  }
  set_popup_fixed_width(false);
  for (auto* cell : get_cells()) {
    if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cell))
      text->property_ellipsize() = Pango::ELLIPSIZE_END;
  }
}

void NarrowCombo::get_preferred_width_vfunc(int& minimum_width, int& natural_width) const
{
  Gtk::ComboBoxText::get_preferred_width_vfunc(minimum_width, natural_width);
  // A box with an entry asks for no less than its entry and arrow need.
  // Given less, GTK lays the arrow over the entry, and its text area
  // shrank to about 12 px: "11" showed as "1".
  if (!get_has_entry())
    minimum_width = std::min(minimum_width, width_);
  natural_width = std::max(minimum_width, width_);
}

}  // namespace writeit
