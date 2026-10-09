/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <gtkmm/comboboxtext.h>

namespace writeit {

// A toolbar combo that asks for a set width, not its longest entry's. The
// font list holds family names far wider than the box, and a toolbar sums
// what its items ask for, so one long name would widen the whole row. The
// box shows the start of the chosen name, ellipsized; its list still pops at
// full width.
class NarrowCombo : public Gtk::ComboBoxText {
 public:
  explicit NarrowCombo(int width);

 protected:
  void get_preferred_width_vfunc(int& minimum_width, int& natural_width) const override;

 private:
  int width_;
};

}  // namespace writeit
