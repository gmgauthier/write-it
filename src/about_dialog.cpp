/* SPDX-License-Identifier: Unlicense */

#include "about_dialog.hpp"
#include "config.hpp"

namespace writeit {

AboutDialog::AboutDialog(Gtk::Window& parent)
    : Gtk::Dialog("About Write-It", parent, true)
{
  set_resizable(false);
  add_button("_Close", Gtk::RESPONSE_CLOSE);
  set_default_response(Gtk::RESPONSE_CLOSE);
  auto* box = get_content_area();
  box->set_border_width(16);
  box->set_spacing(10);
  auto* title = Gtk::manage(new Gtk::Label());
  title->set_markup("<b>Write-It " VERSION "</b>");
  box->pack_start(*title, Gtk::PACK_SHRINK);
  auto* line = Gtk::manage(
      new Gtk::Label("A word processor whose window is Word 97 and whose file is RTF."));
  line->set_line_wrap(true);
  line->set_max_width_chars(48);
  box->pack_start(*line, Gtk::PACK_SHRINK);
  box->pack_start(*Gtk::manage(new Gtk::Label("Released under The Unlicense.")), Gtk::PACK_SHRINK);
  show_all_children();
}

}  // namespace writeit
