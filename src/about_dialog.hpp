/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <gtkmm.h>

namespace writeit {

class AboutDialog : public Gtk::Dialog {
 public:
  explicit AboutDialog(Gtk::Window& parent);
};

}  // namespace writeit
