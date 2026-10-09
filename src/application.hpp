/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <gtkmm.h>

#include <string>
#include <vector>

namespace writeit {

class MainWindow;

class Application : public Gtk::Application {
 public:
  static Glib::RefPtr<Application> create();

 protected:
  Application();
  void on_activate() override;
  // Files on the command line, from the desktop file's %F, or from a second
  // launch, which hands them to this instance and exits.
  void on_open(const type_vec_files& files, const Glib::ustring& hint) override;

 private:
  MainWindow* new_window();
  void open_paths(const std::vector<std::string>& paths);
};

}  // namespace writeit
